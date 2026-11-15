/* MCC bitmap resources. Convert and pack each resource in MCC-owned memory,
 * then stream its native hardware layout. The common rasterizer supplies
 * descriptor size limits; no CE bitmap state or conversion API is used. */
#include "cseries.h"
#include "errors.h"
#include "bitmaps/bitmaps.h"
#include "bitmaps/bitmap_group.h"
#include "interface/unit_hud_interface_definition.h"
#include "rasterizer/rasterizer_swizzle.h"
#include "mcc_runtime.h"
#include "mcc_loading.h"
#include "mcc_resources.h"
#include "mcc_texture_cache.h"
#include "../src/mcc_texture_bridge.h"
#include "../src/port_config.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "mcc_bitmap_kernels.h"
#include "mcc_convert.h"

#define MCC_BITMAP_BASE 0x60000000u
#define MCC_BITMAP_LIMIT 0x1FFFFFFFu
#define MCC_BITMAP_ALLOCATION_LIMIT 0x08000000u
/* XAM's original textures alone exceed 1 GiB. Keep a bounded aggregate
 * budget; individual allocations and per-tag stream offsets stay smaller. */
#define MCC_BITMAP_STORAGE_LIMIT 0x80000000u

struct mcc_texture {
    struct bitmap_data *bitmap;
    uint32_t handle, offset, size;
    unsigned char *pixels;
    unsigned channels;
    uint32_t resource_handle, resource_index;
};
struct mcc_bitmap_clone { uint32_t source, target; unsigned channels; };
struct mcc_textures {
    struct mcc_texture *items;
    struct mcc_bitmap_clone *clones;
    uint32_t count, end, capacity, clone_count, tag_capacity, stream_end;
    /* BC7 bitmaps kept as DXT5 (mcc_bc7_to_dxt5), and the bytes that saved */
    uint32_t transcoded;
    uint64_t transcode_saved;
};

static void *mcc_bitmap_tag(struct mcc_runtime *runtime,uint32_t handle,uint32_t group,uint32_t size)
{
    uint32_t *entry;
    if (handle==0xFFFFFFFFu || (handle&0xFFFF)>=runtime->report.tag_count) return NULL;
    entry=(uint32_t *)(runtime->tag_index+(handle&0xFFFF)*0x20);
    if (entry[0]!=group || entry[3]!=handle) return NULL;
    return mcc_runtime_pointer(runtime,entry[5],size);
}

static void *mcc_bitmap_block(struct mcc_runtime *runtime,struct tag_block const *block,uint32_t size)
{
    if (block->count<0 || (uint32_t)block->count>UINT32_MAX/size) return NULL;
    if (!block->count) return runtime->tags;
    return mcc_runtime_pointer(runtime,(uint32_t)block->address,(uint32_t)block->count*size);
}

static void mcc_bitmap_mark_one(struct mcc_textures *textures,struct bitmap_data *bitmap,unsigned channels)
{
    uint32_t index;
    for (index=0;index<textures->count;index++) {
        if (textures->items[index].bitmap==bitmap) textures->items[index].channels|=channels;
    }
}

static int mcc_bitmap_clone_block(struct mcc_runtime *runtime,struct tag_block *block,uint32_t stride)
{
    void *source,*copy;
    uint32_t bytes;
    if (!block->count) {block->address=NULL;return 1;}
    source=mcc_bitmap_block(runtime,block,stride);
    if (!source) return 0;
    bytes=(uint32_t)block->count*stride;
    copy=mcc_runtime_allocate(runtime,bytes);
    if (!copy) return 0;
    memcpy(copy,source,bytes);block->address=copy;
    return 1;
}

/* A bitmap can be used as both color and a packed material mask. Each
 * specialized consumer gets its own tag and descriptors, preserving the
 * ordinary source tag and every other consumer's channels. */
static int mcc_bitmap_specialize(struct mcc_runtime *runtime,struct mcc_textures *textures,
    uint32_t *handle,unsigned channels)
{
    struct bitmap_group *source,*copy;
    struct bitmap_group_sequence *sequences;
    struct bitmap_data *bitmaps;
    uint32_t i,*entry,new_handle;
    char *name;
    if (*handle==0xFFFFFFFFu || channels==1) return 1;
    for (i=0;i<textures->clone_count;i++) {
        struct mcc_bitmap_clone const *clone=&textures->clones[i];
        if (clone->source==*handle && clone->channels==channels) {*handle=clone->target;return 1;}
    }
    if (runtime->report.tag_count>=textures->tag_capacity) return 0;
    source=mcc_bitmap_tag(runtime,*handle,BITMAP_GROUP_TAG,sizeof(*source));
    if (!source) return 0;
    copy=mcc_runtime_allocate(runtime,sizeof(*copy));
    if (!copy) return 0;
    *copy=*source;
    if (!mcc_bitmap_clone_block(runtime,&copy->sequences,sizeof(*sequences)) ||
        !mcc_bitmap_clone_block(runtime,&copy->bitmaps,sizeof(*bitmaps))) return 0;
    sequences=copy->sequences.address;
    for (i=0;i<(uint32_t)copy->sequences.count;i++) {
        if (!mcc_bitmap_clone_block(runtime,&sequences[i].sprites,sizeof(struct bitmap_group_sprite))) return 0;
    }
    /* Import/pixel tag_data are editor storage. Runtime pixels use each
     * descriptor's file range, so these never alias a cloned tag allocation. */
    memset(&copy->import_bitmap,0,sizeof(copy->import_bitmap));
    memset(&copy->pixel_data,0,sizeof(copy->pixel_data));
    name=mcc_runtime_allocate(runtime,48);
    if (!name) return 0;
    snprintf(name,48,"mcc_runtime\\bitmap_%08lx_%u",(unsigned long)*handle,channels);
    entry=(uint32_t *)(runtime->tag_index+runtime->report.tag_count*32);
    memcpy(entry,runtime->tag_index+(*handle&0xFFFF)*32,32);
    new_handle=(*handle&0xFFFF0000u)|runtime->report.tag_count;
    entry[3]=new_handle;entry[4]=(uint32_t)name;entry[5]=(uint32_t)copy;
    bitmaps=copy->bitmaps.address;
    for (i=0;i<(uint32_t)copy->bitmaps.count;i++) {
        struct mcc_texture *texture;
        if (textures->count>=textures->capacity) return 0;
        texture=&textures->items[textures->count++];
        texture->bitmap=&bitmaps[i];texture->handle=new_handle;texture->channels=channels;
        texture->resource_handle=*handle;texture->resource_index=i;
    }
    textures->clones[textures->clone_count].source=*handle;
    textures->clones[textures->clone_count].target=new_handle;
    textures->clones[textures->clone_count++].channels=channels;
    runtime->report.tag_count++;
    *handle=new_handle;
    return 1;
}

/* Channels is a usage mask: ordinary=1, multipurpose=2, HUD meter=4. */
static int mcc_bitmap_mark(struct mcc_runtime *runtime,struct mcc_textures *textures,
    uint32_t *reference,short sequence,unsigned channels)
{
    struct bitmap_group *group;
    struct bitmap_data *bitmaps;
    uint32_t index;
    uint32_t handle;
    if (!mcc_bitmap_specialize(runtime,textures,reference,channels)) return 0;
    handle=*reference;
    if (handle==0xFFFFFFFFu) return 1;
    group=mcc_bitmap_tag(runtime,handle,BITMAP_GROUP_TAG,sizeof(*group));
    if (!group) return 0;
    bitmaps=mcc_bitmap_block(runtime,&group->bitmaps,sizeof(*bitmaps));
    if (!bitmaps) return 0;
    if (sequence>=0 && sequence<group->sequences.count) {
        struct bitmap_group_sequence *sequences=mcc_bitmap_block(runtime,&group->sequences,sizeof(*sequences));
        struct bitmap_group_sequence *selected;
        if (!sequences) return 0;
        selected=&sequences[sequence];
        if (selected->sprites.count) {
            struct bitmap_group_sprite *sprites=mcc_bitmap_block(runtime,&selected->sprites,sizeof(*sprites));
            if (!sprites) return 0;
            for (index=0;index<(uint32_t)selected->sprites.count;index++) {
                short n=sprites[index].bitmap_index;
                if (n<0 || n>=group->bitmaps.count) return 0;
                mcc_bitmap_mark_one(textures,&bitmaps[n],channels);
            }
        } else {
            if (selected->first_bitmap_index<0 || selected->bitmap_count<0 ||
                selected->first_bitmap_index>group->bitmaps.count-selected->bitmap_count) return 0;
            for (index=0;index<(uint32_t)selected->bitmap_count;index++)
                mcc_bitmap_mark_one(textures,&bitmaps[selected->first_bitmap_index+index],channels);
        }
    } else {
        for (index=0;index<(uint32_t)group->bitmaps.count;index++) mcc_bitmap_mark_one(textures,&bitmaps[index],channels);
    }
    return 1;
}

static int mcc_meter_usage(struct mcc_runtime *runtime,struct mcc_textures *textures,
    struct meter_hud_element_definition *meter)
{
    /* MCC meter flag 5 keeps Xbox fill/shape channels. The native meter
     * already consumes that order; only Gearbox-style meters need a clone
     * with luminance and alpha exchanged. */
    unsigned channels=meter->meter_flags&0x20 ? 1 : 4;
    /* Gearbox meters do not apply the Xbox RGB opacity tint. Descent's
     * authored opacity of one otherwise turns its shield and ammo black.
     * Fade and the explicitly Xbox-style meter convention remain intact. */
    if (channels==4) meter->opacity=0.0f;
    return mcc_bitmap_mark(runtime,textures,(uint32_t *)&meter->meter_bitmap.index,meter->sequence_index,channels);
}

static int mcc_static_usage(struct mcc_runtime *runtime,struct mcc_textures *textures,
    struct static_hud_element_definition *element)
{
    return mcc_bitmap_mark(runtime,textures,(uint32_t *)&element->interface_bitmap.index,element->sequence_index,1);
}

static int mcc_bitmap_model_usage(struct mcc_runtime *runtime,struct mcc_textures *textures,
    unsigned char *shader)
{
    static unsigned const offsets[]={0xB0,0xC8,0xE8,0x170};
    uint16_t flags;
    unsigned n;
    memcpy(&flags,shader+0x28,sizeof(flags));
    /* MCC's model flag 6 selects original Xbox mask channels. These masks
     * already have reflection in red and change color in blue; applying the
     * Gearbox swizzle would turn opaque DXT1 alpha into full-surface tint.
     * Select per reference: another shader may use this same bitmap with
     * the flag clear and must still receive its own reordered clone. */
    for (n=0;n<4;n++) {
        unsigned channels=n==1 && !(flags&0x40) ? 2 : 1;
        if (!mcc_bitmap_mark(runtime,textures,(uint32_t *)(shader+offsets[n]),NONE,channels)) return 0;
    }
    return 1;
}

static int mcc_bitmap_usages(struct mcc_runtime *runtime,struct mcc_textures *textures)
{
    uint32_t index;
    for (index=0;index<runtime->report.tag_count;index++) {
        uint32_t *entry=(uint32_t *)(runtime->tag_index+index*0x20);
        if (entry[0]=='soso') {
            unsigned char *shader=mcc_runtime_pointer(runtime,entry[5],0x174);
            if (!shader || !mcc_bitmap_model_usage(runtime,textures,shader)) return 0;
        } else if (entry[0]==UNIT_HUD_INTERFACE_DEFINITION_TAG) {
            struct unit_hud_interface_definition *hud=mcc_runtime_pointer(runtime,entry[5],sizeof(*hud));
            struct auxilary_meter_definition *meters;
            struct auxilary_overlay_definition *overlays;
            uint32_t n;
            if (!hud || !mcc_meter_usage(runtime,textures,&hud->shield_meter.meter) ||
                !mcc_meter_usage(runtime,textures,&hud->health_meter.meter) ||
                !mcc_static_usage(runtime,textures,&hud->background) ||
                !mcc_static_usage(runtime,textures,&hud->shield_meter.background) ||
                !mcc_static_usage(runtime,textures,&hud->health_meter.background) ||
                !mcc_static_usage(runtime,textures,&hud->motion_sensor.background) ||
                !mcc_static_usage(runtime,textures,&hud->motion_sensor.foreground)) return 0;
            meters=mcc_bitmap_block(runtime,&hud->auxilary_meters,sizeof(*meters));
            overlays=mcc_bitmap_block(runtime,&hud->auxilary_panel.auxilary_overlays,sizeof(*overlays));
            if (!meters || !overlays) return 0;
            for (n=0;n<(uint32_t)hud->auxilary_meters.count;n++) {
                if (!mcc_meter_usage(runtime,textures,&meters[n].panel.meter) ||
                    !mcc_static_usage(runtime,textures,&meters[n].panel.background)) return 0;
            }
            for (n=0;n<(uint32_t)hud->auxilary_panel.auxilary_overlays.count;n++)
                if (!mcc_static_usage(runtime,textures,&overlays[n].static_element)) return 0;
        } else if (entry[0]=='wphi') {
            unsigned char *hud=mcc_runtime_pointer(runtime,entry[5],0x78);
            unsigned which;
            if (!hud) return 0;
            for (which=0;which<2;which++) {
                struct tag_block *block=(struct tag_block *)(hud+0x60+which*12);
                unsigned char *elements=mcc_bitmap_block(runtime,block,0xB4);
                uint32_t n;
                if (!elements) return 0;
                for (n=0;n<(uint32_t)block->count;n++) {
                    void *element=elements+n*0xB4+0x24;
                    if (!(which ? mcc_meter_usage(runtime,textures,element) : mcc_static_usage(runtime,textures,element))) return 0;
                }
            }
        }
    }
    return 1;
}

/* Recent MCC campaign lightmaps carry bit 12 in addition to the environment
 * flag. Its wider MCC meaning is not assumed here: admit only the observed
 * lightmap-group RGB565 layout with a complete, tightly packed pixel range.
 * The normal descriptor verifier still checks every other bit and field. */
static int mcc_bitmap_lightmap_flags(struct bitmap_data *bitmap,short usage)
{
    if (!(bitmap->flags&0x1000)) return 1;
    if (usage!=4 || bitmap->flags!=0x1281 || bitmap->type!=0 || bitmap->format!=6 ||
        bitmap->width<=0 || bitmap->height<=0 || bitmap->depth!=1 || bitmap->mipmap_count ||
        (uint64_t)(unsigned)bitmap->width*(unsigned)bitmap->height*2!=(uint32_t)bitmap->pixels_size) return 0;
    bitmap->flags&=~0x1000;
    return 1;
}

/* Some MCC HUD textures declare a complete DXT1 chain but size its sub-4x4
 * tail as width*height/2 instead of whole compression blocks. Recognize only
 * that exact size formula and retain the complete prefix. Arbitrary short
 * ranges, missing base levels and other formats remain load failures. */
static void mcc_bitmap_complete_mips(struct bitmap_data *bitmap)
{
    uint64_t packed=0;
    unsigned level,maximum,complete=0;
    if (bitmap->type!=0 || bitmap->format!=14 || bitmap->flags!=0x81 || bitmap->depth!=1 ||
        bitmap->width<4 || bitmap->height<4 ||
        (bitmap->width&(bitmap->width-1)) || (bitmap->height&(bitmap->height-1))) return;
    maximum=(unsigned)floor_log2(MAX(bitmap->width,bitmap->height));
    if (bitmap->mipmap_count!=(short)maximum) return;
    for (level=0;level<=maximum;level++) {
        unsigned width=MAX(bitmap->width>>level,1),height=MAX(bitmap->height>>level,1);
        packed+=(uint64_t)width*height/2;
        if (width>=4 && height>=4) complete=level;
    }
    if (packed==(uint32_t)bitmap->pixels_size) bitmap->mipmap_count=(short)complete;
}

/* A texture on its way: planned (its shape, sizes and stream decided, in the
 * map's order, before anything is converted), its raw pixels read, and its
 * pixel work (mcc_bitmap_kernels.h) done on the host's threads or here; then
 * taken in the same order, its descriptor written as one thread converting
 * in turn wrote it. */
struct mcc_texture_entry {
    uint32_t index;
    struct mcc_texture *texture;
    struct bitmap_data normalized;
    struct mcc_convert_bitmap description;
    struct mcc_convert_job request;
    unsigned char *raw, *hardware;
    uint32_t allocation, stream_offset, bytes;
    uint64_t raw_size, decoded_size;
    int job, result, non_power_two, transcode;
};

/* at most this many in hand, and this many bytes of the guest's memory
(their raw pixels and packed ones) */
#define MCC_TEXTURE_QUEUE 32
#define MCC_TEXTURE_QUEUE_BYTES 0x6000000u

struct mcc_texture_queue {
    struct mcc_texture_entry entries[MCC_TEXTURE_QUEUE];
    uint32_t first, count, bytes;
    int host;
    uint32_t *axes;
};

/* the texture planned and read: its entry filled. 0 where it cannot be
converted (said, as one thread converting in turn said it) */
static int mcc_texture_plan(struct mcc_runtime *runtime,struct mcc_textures *textures,struct mcc_texture *texture,
    uint32_t stream_offset,struct mcc_texture_entry *planned)
{
    struct bitmap_data *bitmap=texture->bitmap, normalized=*bitmap;
    unsigned level,faces=bitmap->type==2 ? 6 : 1,maximum;
    uint64_t raw_size=0,decoded_size=0;
    unsigned char *raw=NULL;
    int bytes_per_pixel=mcc_bitmap_bytes(bitmap->format);
    int non_power_two=bitmap->type==0 && ((bitmap->width&(bitmap->width-1)) || (bitmap->height&(bitmap->height-1)));
    /* BC7 as DXT5 (mcc_bc7_to_dxt5) where it would be drawn as it is: a 2D
    or cube texture of whole blocks, not linear and not one whose channels
    are moved on the CPU, which must still be decoded */
    int transcode=bitmap->format==18 && texture->channels!=2 && texture->channels!=4 &&
        !(bitmap->flags&16) && !non_power_two && (bitmap->type==0 || bitmap->type==2) &&
        bitmap->width>=4 && bitmap->height>=4;
    int convert=(bitmap->format==18 && !transcode) || texture->channels==2 || texture->channels==4 ||
        ((bitmap->flags&16) && bitmap->format>=14 && bitmap->format<=17);
    uint32_t hardware_size,allocation;
    if (bitmap->width<=0 || bitmap->height<=0 || bitmap->depth<=0 || bitmap->type<0 || bitmap->type>2 ||
        bitmap->width>16384 || bitmap->height>16384 || bitmap->depth>512 || bitmap->mipmap_count<0 || bitmap->mipmap_count>13 ||
        bitmap->pixels_size<=0 || (!bytes_per_pixel && (bitmap->format<14 || bitmap->format>18))) return 0;
    if ((bitmap->width>8192 || bitmap->height>8192) && !non_power_two && !(bitmap->flags&16)) return 0;
    if ((bitmap->type==1 && (bitmap->width>512 || bitmap->height>512 || bitmap->depth>256)) ||
        (bitmap->type!=1 && bitmap->depth!=1)) return 0;
    if (texture->channels && (texture->channels&(texture->channels-1))) {
        error(_error_silent,"mcc bitmap: tag %08lx has conflicting pixel-channel uses",texture->handle);
        return 0;
    }
    mcc_bitmap_complete_mips(&normalized);
    for (level=0;level<=(unsigned)normalized.mipmap_count;level++) {
        unsigned w=MAX(bitmap->width>>level,1),h=MAX(bitmap->height>>level,1),d=MAX(bitmap->depth>>level,1);
        uint64_t slice=bytes_per_pixel ? (uint64_t)w*h*bytes_per_pixel :
            (uint64_t)((w+3)/4)*((h+3)/4)*(bitmap->format==14 ? 8 : 16);
        raw_size+=slice*d*faces;
        decoded_size+=(uint64_t)w*h*d*faces*4;
    }
    if (raw_size>(uint32_t)bitmap->pixels_size || raw_size>MCC_BITMAP_ALLOCATION_LIMIT ||
        (convert && decoded_size>MCC_BITMAP_ALLOCATION_LIMIT)) return 0;
    if (non_power_two) {
        /* Preserve arbitrary MCC HUD dimensions using the engine's linear
         * ARGB layout. Validate the entire declared source chain above,
         * then retain its base level: Xbox linear textures have no mips. */
        if (decoded_size>MCC_BITMAP_ALLOCATION_LIMIT) return 0;
        convert=1;normalized.flags=(normalized.flags&~1u)|16u;
        normalized.mipmap_count=0;
        decoded_size=(uint64_t)(unsigned)bitmap->width*(unsigned)bitmap->height*4;
    }
    if (normalized.flags&0x1000) {
        struct bitmap_group *group=mcc_bitmap_tag(runtime,texture->handle,BITMAP_GROUP_TAG,sizeof(*group));
        if (!group || !mcc_bitmap_lightmap_flags(&normalized,group->usage)) return 0;
    }
    if (convert) {
        normalized.format=11;
        normalized.flags&=~(2|4|8|32|0x100);
    } else normalized.flags&=~(8|0x100);
    if (transcode) normalized.format=16;
    /* CEA bit 9 marks environment/lightmap use (Invader's public bitmap
     * schema). It is not a pixel-layout flag and has no Xbox counterpart. */
    normalized.flags&=~0x200;
    normalized.flags=(normalized.flags&~64)|128;
    /* MCC raw flags are not an Xbox hardware descriptor. Derive the
     * compressed/palette bits from the normalized pixel format. */
    normalized.flags&=~(2|4);
    if (normalized.format>=14 && normalized.format<=16) normalized.flags|=2;
    if (normalized.format==17) normalized.flags|=4;
    if (!bitmap_verify(&normalized,FALSE)) return 0;
    if (!(normalized.flags&16) &&
        ((normalized.width&(normalized.width-1)) || (normalized.height&(normalized.height-1)) ||
         (normalized.depth&(normalized.depth-1)))) return 0;
    if ((normalized.flags&16) && (normalized.type!=0 || normalized.mipmap_count || (normalized.flags&2))) return 0;
    hardware_size=(uint32_t)rasterizer_xbox_bitmap_get_pixel_data_size(&normalized);
    /* The generic bitmap verifier includes the tiny DXT mip tail, whereas
     * native hardware stops at a 4-texel dimension. Own enough storage for
     * both contracts, including the 2D tail sampled by CPU object lighting. */
    allocation=(MAX(hardware_size,(uint32_t)(convert ? decoded_size : raw_size))+127u)&~127u;
    /* The cache read contract includes the bitmap tag handle. Give each tag
     * its own bounded virtual stream; the aggregate decoded storage can then
     * exceed the old 512 MiB offset window without signed offsets or aliases
     * between descriptors within one tag. Reads still require exact handles. */
    if (!hardware_size || allocation>MCC_BITMAP_ALLOCATION_LIMIT ||
        stream_offset>MCC_BITMAP_LIMIT || textures->end>MCC_BITMAP_STORAGE_LIMIT ||
        allocation>MCC_BITMAP_LIMIT-stream_offset || allocation>MCC_BITMAP_STORAGE_LIMIT-textures->end) {
        error(_error_silent,"mcc bitmap: storage limit (used %lu, requested %lu)",textures->end,allocation);
        return 0;
    }
    /* CPU object lighting can select the final 2x2/1x1 DXT mips even though
     * the GPU descriptor stops at 4 texels. A 2D chain has identical CPU/GPU
     * prefix layout, so retain its complete source tail in owned storage. */
    maximum=rasterizer_xbox_bitmap_get_max_mipmap_count(&normalized);
    if (normalized.type==0 && (normalized.flags&2)) maximum=normalized.mipmap_count;
    raw=malloc((size_t)raw_size);
    if (!raw) {
        error(_error_silent,"mcc bitmap: out of memory for tag %08lx (stored %lu, texture %lu bytes)",
            texture->handle,textures->end,allocation);
        return 0;
    }
    if (bitmap->flags&0x100) {
        uint32_t *entry=(uint32_t *)(runtime->tag_index+(texture->resource_handle&0xFFFF)*32);
        char const *name=mcc_runtime_pointer(runtime,entry[4],256);
        char resource_name[272],group_name[272];
        if (!name || !memchr(name,0,256)) {free(raw);return 0;}
        snprintf(resource_name,sizeof(resource_name),"%s_%lu",name,(unsigned long)texture->resource_index);
        snprintf(group_name,sizeof(group_name),"%s__pixels",name);
        if (!mcc_resources_read(runtime,1,resource_name,bitmap->pixels_offset,(uint32_t)raw_size,raw,group_name)) {
            error(_error_silent,"mcc bitmap: external '%s' does not match the selected MCC bitmaps.map",resource_name);
            free(raw);
            return 0;
        }
    } else if (!mcc_runtime_read(runtime,bitmap->pixels_offset,(uint32_t)raw_size,raw)) {free(raw);return 0;}
    memset(planned,0,sizeof(*planned));
    planned->texture=texture;planned->normalized=normalized;planned->raw=raw;
    planned->allocation=allocation;planned->stream_offset=stream_offset;
    planned->raw_size=raw_size;planned->decoded_size=decoded_size;
    planned->non_power_two=non_power_two;planned->transcode=transcode;planned->job=-1;
    planned->bytes=(uint32_t)raw_size+allocation;
    /* the pixel work's description (mcc_convert_job.h) */
    planned->description.source_width=(uint32_t)bitmap->width;
    planned->description.source_height=(uint32_t)bitmap->height;
    planned->description.source_depth=(uint32_t)bitmap->depth;
    planned->description.source_type=(uint32_t)bitmap->type;
    planned->description.source_format=(uint32_t)bitmap->format;
    planned->description.source_flags=(uint32_t)(unsigned short)bitmap->flags;
    planned->description.source_mipmaps=(uint32_t)bitmap->mipmap_count;
    planned->description.packed_width=(uint32_t)normalized.width;
    planned->description.packed_height=(uint32_t)normalized.height;
    planned->description.packed_depth=(uint32_t)normalized.depth;
    planned->description.packed_type=(uint32_t)normalized.type;
    planned->description.packed_format=(uint32_t)normalized.format;
    planned->description.packed_flags=(uint32_t)(unsigned short)normalized.flags;
    planned->description.packed_mipmaps=(uint32_t)normalized.mipmap_count;
    planned->description.maximum=maximum;
    planned->description.usage=texture->channels;
    planned->description.faces=faces;
    planned->description.levels=(uint32_t)normalized.mipmap_count+1;
    planned->description.transcode=(uint32_t)transcode;
    planned->description.convert=(uint32_t)convert;
    planned->description.raw_size=(uint32_t)raw_size;
    planned->description.decoded_size=(uint32_t)decoded_size;
    planned->description.target_size=normalized.type==0 && (normalized.flags&2) ? allocation : hardware_size;
    planned->description.bytes_per_pixel=(uint32_t)bytes_per_pixel;
    planned->description.palette=(uint32_t)(uintptr_t)global_vector_palette;
    /* (its place in the stream and the storage, decided now: the next
    texture's plan depends on them, not on this one's conversion) */
    texture->size=allocation;
    textures->end+=allocation;
    textures->stream_end=MAX(textures->stream_end,stream_offset+allocation);
    return 1;
}

/* the planned texture's pixel work started: on the host's threads, or done
here and now. 0 where it could not be (said) */
static int mcc_texture_start(struct mcc_textures *textures,struct mcc_texture_queue *queue,
    struct mcc_texture_entry *entry)
{
    entry->hardware=malloc(entry->allocation);
    if (!entry->hardware) {
        error(_error_silent,"mcc bitmap: out of memory for tag %08lx (stored %lu, texture %lu bytes)",
            entry->texture->handle,textures->end,entry->allocation);
        return 0;
    }
    if (queue->host) {
        entry->request.kind=MCC_CONVERT_BITMAP;
        entry->request.input=(uint32_t)(uintptr_t)entry->raw;
        entry->request.input_size=(uint32_t)entry->raw_size;
        entry->request.output=(uint32_t)(uintptr_t)entry->hardware;
        entry->request.output_size=entry->allocation;
        entry->request.parameters[0]=(uint32_t)(uintptr_t)&entry->description;
        entry->job=mcc_convert_submit(&entry->request);
        if (entry->job>=0) return 1;
    }
    {
        struct mcc_bitmap_work work;
        unsigned char *decoded=NULL;
        mcc_bitmap_work_from(&entry->description,&work);
        if (work.convert && !(decoded=malloc(work.decoded_size ? work.decoded_size : 1))) {
            error(_error_silent,"mcc bitmap: out of memory for tag %08lx (stored %lu, texture %lu bytes)",
                entry->texture->handle,textures->end,entry->allocation);
            return 0;
        }
        entry->result=mcc_bitmap_work_run(&work,entry->raw,decoded,entry->hardware,queue->axes,mcc_loading_pulse) ? 1 :
            MCC_CONVERT_FAILED_DECODE;
        if (decoded) free(decoded);
    }
    return 1;
}

static void mcc_texture_free(struct mcc_texture_entry *entry)
{
    mcc_convert_release(entry->job);
    entry->job=-1;
    if (entry->raw) free(entry->raw);
    if (entry->hardware) free(entry->hardware);
    entry->raw=entry->hardware=NULL;
}

/* the queue's first taken: its descriptor written. 0 where it failed (said) */
static int mcc_texture_commit(struct mcc_textures *textures,struct mcc_texture_queue *queue)
{
    struct mcc_texture_entry *entry=&queue->entries[queue->first];
    struct mcc_texture *texture=entry->texture;
    struct bitmap_data *bitmap=texture->bitmap, *normalized=&entry->normalized;
    int result=entry->job>=0 ? mcc_convert_finish(entry->job) : entry->result;
    queue->first=(queue->first+1)%MCC_TEXTURE_QUEUE;
    queue->count--;
    queue->bytes-=entry->bytes;
    if (result!=1) {
        error(_error_silent,"mcc bitmaps: cannot convert texture %lu of tag %08lx; map refused",
            (unsigned long)entry->index,texture->handle);
        mcc_texture_free(entry);
        return 0;
    }
    mcc_convert_release(entry->job);
    entry->job=-1;
    free(entry->raw);entry->raw=NULL;
    if (!(normalized->flags&(2|16))) normalized->flags|=8;
    if (entry->transcode) {
        textures->transcoded++;
        textures->transcode_saved+=entry->decoded_size>entry->raw_size ? entry->decoded_size-entry->raw_size : 0;
    }
    if (bitmap->mipmap_count!=normalized->mipmap_count)
        error(_error_silent,entry->non_power_two ? "mcc bitmap: tag %08lx retained linear base level (omitted levels %d through %d)" :
            "mcc bitmap: tag %08lx omitted incomplete DXT1 mip tail (levels %d through %d)",
            texture->handle,normalized->mipmap_count+1,bitmap->mipmap_count);
    bitmap->format=normalized->format;
    bitmap->flags=normalized->flags;
    bitmap->mipmap_count=normalized->mipmap_count;
    bitmap->pixels_offset=MCC_BITMAP_BASE+entry->stream_offset;
    bitmap->pixels_size=entry->allocation;
    bitmap->tag_index=texture->handle;
    bitmap->cache_block_index=NONE;
    bitmap->hardware_format=NULL;
    bitmap->base_address=NULL;
    texture->offset=bitmap->pixels_offset;texture->size=entry->allocation;texture->pixels=entry->hardware;
    entry->hardware=NULL;
    mcc_loading_update(MCC_LOADING_BITMAPS, entry->index + 1, textures->count);
    return 1;
}

/* every texture in hand let go, unconverted (the map is refused) */
static void mcc_texture_abandon(struct mcc_texture_queue *queue)
{
    while (queue->count) {
        mcc_texture_free(&queue->entries[queue->first]);
        queue->first=(queue->first+1)%MCC_TEXTURE_QUEUE;
        queue->count--;
    }
    queue->bytes=0;
}

/* every texture in hand taken; where one fails, what follows it let go */
static int mcc_texture_drain(struct mcc_textures *textures,struct mcc_texture_queue *queue)
{
    while (queue->count)
        if (!mcc_texture_commit(textures,queue)) {mcc_texture_abandon(queue);return 0;}
    return 1;
}

/* the map's textures converted, in its order, through the queue */
static int mcc_textures_convert(struct mcc_runtime *runtime,struct mcc_textures *textures)
{
    struct mcc_texture_queue *queue=malloc(sizeof(*queue));
    uint32_t index,stream_offset=0;
    int ok=1;
    if (!queue) return 0;
    memset(queue,0,sizeof(*queue));
    queue->host=mcc_convert_available();
    queue->axes=malloc(MCC_BITMAP_AXES*sizeof(*queue->axes));
    if (!queue->axes) {free(queue);return 0;}
    /* Original groups and fresh-handle clones are appended as complete runs.
     * Each runtime handle has its own stream, including clones that share a
     * resource handle. Only successful conversions advance its cursor. */
    for (index=0;index<textures->count && ok;index++) {
        struct mcc_texture_entry *entry;
        if (!index || textures->items[index].handle!=textures->items[index-1].handle) stream_offset=0;
        /* (room for it: the oldest taken first, as many as that needs) */
        while (queue->count && (queue->count==MCC_TEXTURE_QUEUE || queue->bytes>MCC_TEXTURE_QUEUE_BYTES))
            if (!(ok=mcc_texture_commit(textures,queue))) break;
        if (!ok) break;
        entry=&queue->entries[(queue->first+queue->count)%MCC_TEXTURE_QUEUE];
        if (!mcc_texture_plan(runtime,textures,&textures->items[index],stream_offset,entry)) {
            /* (what came before it first: a failure there is the one one
            thread converting in turn would have met first) */
            if (mcc_texture_drain(textures,queue))
                error(_error_silent,"mcc bitmaps: cannot convert texture %lu of tag %08lx; map refused",
                    (unsigned long)index,textures->items[index].handle);
            ok=0;
            break;
        }
        entry->index=index;
        queue->count++;
        queue->bytes+=entry->bytes;
        if (!mcc_texture_start(textures,queue,entry)) {
            queue->count--;
            queue->bytes-=entry->bytes;
            mcc_texture_free(entry);
            if (mcc_texture_drain(textures,queue))
                error(_error_silent,"mcc bitmaps: cannot convert texture %lu of tag %08lx; map refused",
                    (unsigned long)index,textures->items[index].handle);
            ok=0;
            break;
        }
        stream_offset+=textures->items[index].size;
        /* (whatever is done already taken) */
        while (ok && queue->count>1 && (queue->entries[queue->first].job<0 ||
            mcc_convert_ready(queue->entries[queue->first].job)))
            ok=mcc_texture_commit(textures,queue);
    }
    if (ok) ok=mcc_texture_drain(textures,queue);
    else mcc_texture_abandon(queue);
    free(queue->axes);
    free(queue);
    return ok;
}

int mcc_bitmaps_prepare(struct mcc_runtime *runtime)
{
    struct mcc_textures *textures;
    uint32_t index,original_count=runtime->report.tag_count;
    unsigned char *new_index;
    textures=malloc(sizeof(*textures));
    if (!textures) return 0;
    memset(textures,0,sizeof(*textures));
    runtime->bitmaps=textures;
    /* three entries a bitmap: its own and its channel clones' */
    if (runtime->report.bitmap_count>SIZE_MAX/3/sizeof(*textures->items)) return 0;
    textures->capacity=runtime->report.bitmap_count*3;
    textures->tag_capacity=MIN(original_count*3,0xFFFFu);
    new_index=mcc_runtime_allocate(runtime,textures->tag_capacity*32);
    if (!new_index) return 0;
    memcpy(new_index,runtime->tag_index,original_count*32);
    runtime->tag_index=new_index;
    textures->clones=malloc(textures->tag_capacity*sizeof(*textures->clones));
    if (!textures->clones) return 0;
    if (runtime->report.bitmap_count) {
        textures->items=malloc(textures->capacity*sizeof(*textures->items));
        if (!textures->items) return 0;
        memset(textures->items,0,textures->capacity*sizeof(*textures->items));
    }
    for (index=0;index<original_count;index++) {
        uint32_t *entry=(uint32_t *)(runtime->tag_index+index*0x20), n;
        struct bitmap_group *group;
        struct bitmap_data *bitmaps;
        if (entry[0]!=BITMAP_GROUP_TAG) continue;
        group=mcc_runtime_pointer(runtime,entry[5],sizeof(*group));
        if (!group || !(bitmaps=mcc_bitmap_block(runtime,&group->bitmaps,sizeof(*bitmaps)))) return 0;
        for (n=0;n<(uint32_t)group->bitmaps.count;n++) {
            struct mcc_texture *texture;
            if (textures->count>=runtime->report.bitmap_count) return 0;
            texture=&textures->items[textures->count++];
            texture->bitmap=&bitmaps[n];texture->handle=entry[3];
            texture->resource_handle=entry[3];texture->resource_index=n;
        }
    }
    if (!mcc_bitmap_usages(runtime,textures)) {
        error(_error_silent,"mcc bitmaps: invalid bitmap reference in shader or HUD"); return 0;
    }
    if (!mcc_textures_convert(runtime,textures)) return 0;
    error(_error_silent,"mcc bitmaps: %lu textures converted to %lu bytes",textures->count,textures->end);
    if (textures->transcoded)
        error(_error_silent,"mcc bitmaps: %lu BC7 bitmaps kept as DXT5, %lu KB less than as 32-bit",
            (unsigned long)textures->transcoded,(unsigned long)(textures->transcode_saved>>10));
    return 1;
}

int mcc_bitmaps_read(struct mcc_runtime *runtime,long tag,uint32_t offset,uint32_t bytes,void *out)
{
    struct mcc_textures *textures=runtime->bitmaps;
    uint32_t index;
    if (offset<MCC_BITMAP_BASE) return -1;
    for (index=0;textures && index<textures->count;index++) {
        struct mcc_texture *texture=&textures->items[index];
        uint32_t position=offset-texture->offset;
        if ((uint32_t)tag==texture->handle && offset>=texture->offset && position<=texture->size && bytes<=texture->size-position) {
            memcpy(out,texture->pixels+position,bytes); return 1;
        }
    }
    return 0;
}

uint32_t mcc_bitmaps_stream_end(struct mcc_runtime *runtime)
{
    struct mcc_textures *textures=runtime->bitmaps;
    return textures && textures->stream_end ? MCC_BITMAP_BASE+textures->stream_end : (uint32_t)runtime->source.size;
}

int mcc_bitmaps_contains(struct mcc_runtime *runtime,uint32_t offset,uint32_t bytes)
{
    struct mcc_textures *textures=runtime->bitmaps;
    uint32_t index;
    for (index=0;textures && index<textures->count;index++) {
        struct mcc_texture const *texture=&textures->items[index];
        if (texture->pixels && offset>=texture->offset && offset-texture->offset<=texture->size &&
            bytes<=texture->size-(offset-texture->offset)) return 1;
    }
    return 0;
}

static struct mcc_texture const *mcc_bitmap_valid_texture(struct mcc_textures const *textures,
    struct bitmap_data const *bitmap)
{
    uint32_t index;
    for (index=0;textures && index<textures->count;index++) {
        struct mcc_texture const *texture=&textures->items[index];
        if (texture->bitmap!=bitmap) continue;
        return texture->pixels && (uint32_t)bitmap->tag_index==texture->handle &&
            (uint32_t)bitmap->pixels_offset==texture->offset && (uint32_t)bitmap->pixels_size==texture->size &&
            bitmap_verify(texture->bitmap,FALSE) &&
            rasterizer_xbox_bitmap_get_pixel_data_size(texture->bitmap)<=(long)texture->size ? texture : NULL;
    }
    return NULL;
}

int mcc_bitmaps_valid(struct mcc_runtime *runtime,struct bitmap_data *bitmap)
{
    return mcc_bitmap_valid_texture(runtime->bitmaps,bitmap)!=NULL;
}

/* MCC pixels are owned allocations, not the legacy file stream (whose
 * per-bitmap budget is smaller). Structural validation precedes this call. */
int mcc_bitmaps_group_valid(struct mcc_runtime *runtime,void *address)
{
    struct bitmap_group *group=address;
    long i;
    if (group->pixel_data.file_offset!=0) return 0;
    for (i=0;i<group->bitmaps.count;i++)
        if (!mcc_bitmaps_valid(runtime,(struct bitmap_data *)group->bitmaps.address+i)) return 0;
    for (i=0;i<group->sequences.count;i++) {
        struct bitmap_group_sequence *sequence=(struct bitmap_group_sequence *)group->sequences.address+i;
        if (sequence->first_bitmap_index<0 || sequence->bitmap_count<0 ||
            sequence->first_bitmap_index>group->bitmaps.count ||
            sequence->bitmap_count>group->bitmaps.count-sequence->first_bitmap_index) {
            error(_error_silent,"mcc bitmap: sequence %ld outside converted bitmap group; disabled",i);
            sequence->first_bitmap_index=sequence->bitmap_count=0;
        }
    }
    return 1;
}

/* The renderer may bind the immutable MCC allocation directly. A copied
 * descriptor, another map's bitmap, or a changed stream range cannot borrow
 * this storage. The caller receives bytes only after ownership validation. */
void const *mcc_bitmaps_pixels(struct mcc_runtime *runtime,struct bitmap_data const *bitmap,uint32_t *bytes,int *coverage)
{
    struct mcc_textures const *textures=runtime ? runtime->bitmaps : NULL;
    struct mcc_texture const *texture;
    if (bytes) *bytes=0;
    if (coverage) *coverage=0;
    if (!textures || !bitmap) return NULL;
    texture=mcc_bitmap_valid_texture(textures,bitmap);
    if (!texture) return NULL;
    if (bytes) *bytes=texture->size;
    if (coverage) *coverage=texture->channels==4;
    return texture->pixels;
}

#ifndef HALO_GLES
static int mcc_bitmap_prepare_order(void const *left,void const *right)
{
    struct mcc_texture const *a=*(struct mcc_texture const *const *)left;
    struct mcc_texture const *b=*(struct mcc_texture const *const *)right;
    return a->size>b->size ? -1 : a->size<b->size ? 1 : 0;
}
#endif

/* Only this module can enumerate the validated, converted MCC allocations.
 * Register through the existing adapter; no Xbox/CE bitmap cache is touched. */
void mcc_bitmaps_prepare_gpu(struct mcc_runtime *runtime)
{
#ifndef HALO_GLES
    struct mcc_textures *textures=runtime ? runtime->bitmaps : NULL;
    struct mcc_texture **ordered;
    long megabytes=config_integer("display.mcc_texture_prepare_mb");
    unsigned long budget,spent=0,prepared=0;
    uint32_t index,count=0;
    if (!textures || megabytes<=0 || config_boolean("debug.null_renderer") || config_server_mode_enabled()) return;
    if (megabytes>512) megabytes=512;
    budget=(unsigned long)megabytes*1024*1024;
    if (textures->count>SIZE_MAX/sizeof(*ordered)) return;
    ordered=malloc(textures->count*sizeof(*ordered));
    if (!ordered) return;
    for (index=0;index<textures->count;index++) {
        struct mcc_texture *texture=&textures->items[index];
        if (texture->size>=256*1024 && texture->bitmap->format!=17) ordered[count++]=texture;
    }
    qsort(ordered,count,sizeof(*ordered),mcc_bitmap_prepare_order);
    for (index=0;index<count && budget-spent>=256*1024;index++) {
        unsigned long charge;
        if (!mcc_texture_cache_get(ordered[index]->bitmap,TRUE)) continue;
        charge=mcc_texture_bridge_prepare(ordered[index]->bitmap,budget-spent);
        spent+=charge;
        if (charge) prepared++;
        mcc_loading_pulse();
    }
    free(ordered);
    error(_error_silent,"mcc textures: prepared %lu large textures (%lu KiB estimated GPU storage)",prepared,spent/1024);
#else
    (void)runtime;
#endif
}

void mcc_bitmaps_dispose(struct mcc_runtime *runtime)
{
    struct mcc_textures *textures=runtime->bitmaps;
    uint32_t index;
    if (!textures) return;
    for (index=0;index<textures->count;index++) {
        struct mcc_texture *texture=&textures->items[index];
        if (texture->bitmap && texture->bitmap->base_address==texture->pixels)
            texture->bitmap->base_address=NULL;
        if (texture->pixels) free(texture->pixels);
    }
    if (textures->items) free(textures->items);
    if (textures->clones) free(textures->clones);
    free(textures);runtime->bitmaps=NULL;
}
