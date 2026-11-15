/* MCC textures' pixel work: a texture's pixels decoded to 32-bit BGRA, BC7
 * blocks encoded again as DXT5, and the result packed into the Xbox's
 * hardware layout. No game state: compiled into the guest (mcc_bitmaps.c,
 * which converts on the game's thread where the host cannot) and into the
 * hosts (port/android/host/host_mcc_convert.c, which convert on their own
 * threads). The engine's answers the guest asks for (how many mips the
 * hardware takes, the P8 palette) are passed in. */
#ifndef MCC_BITMAP_KERNELS_H
#define MCC_BITMAP_KERNELS_H
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
/* Unmodified iOrange/bcdec, MIT option (see third_party/bcdec/LICENSE).
 * Revision 80859ed3b7afb1c527a2a99d70c61457bea72d0c.
 * https://github.com/iOrange/bcdec
 * Header SHA-256: 134520764d96f70a27db814173616c89ad85db95fbd3328417c7a8c77e7ca189. */
#define BCDEC_STATIC
#define BCDEC_IMPLEMENTATION
#include "../../third_party/bcdec/bcdec.h"
#include "mcc_convert_job.h"

#define MCC_BITMAP_KERNEL_MAX(a, b) ((a) > (b) ? (a) : (b))
/* the axis tables mcc_bitmap_pack takes, as one */
#define MCC_BITMAP_AXES (8192 + 8192 + 512)

/* a texture's shape, as packing needs it (a bitmap_data's fields) */
struct mcc_bitmap_layout {
    int width, height, depth;
    short type, format;
    unsigned flags;
    short mipmap_count;
};

static int mcc_bitmap_bytes(short format)
{
    switch (format) {
        case 0: case 1: case 2: case 17: return 1;
        case 3: case 6: case 8: case 9: return 2;
        case 10: case 11: return 4;
        default: return 0;
    }
}

static uint32_t mcc_pixel(unsigned char const *source,int format,uint32_t const *palette)
{
    unsigned v=source[0], a=255,r=v,g=v,b=v;
    if (format==0) {a=v; r=g=b=255;}
    else if (format==2) a=v;
    else if (format==3) a=source[1];
    else if (format==6 || format==8 || format==9) {
        v|=(unsigned)source[1]<<8;
        if (format==6) {r=(v>>11)*255/31; g=((v>>5)&63)*255/63; b=(v&31)*255/31;}
        else if (format==8) {a=(v&0x8000)?255:0; r=((v>>10)&31)*255/31; g=((v>>5)&31)*255/31; b=(v&31)*255/31;}
        else {a=(v>>12)*17; r=((v>>8)&15)*17; g=((v>>4)&15)*17; b=(v&15)*17;}
    } else if (format==10 || format==11) {
        b=source[0];g=source[1];r=source[2];a=format==10 ? 255 : source[3];
    } else if (format==17) return palette ? palette[v] : 0;
    return b|(g<<8)|(r<<16)|(a<<24);
}

static uint32_t mcc_channels(uint32_t color,unsigned usage)
{
    unsigned b=color&255,g=(color>>8)&255,r=(color>>16)&255,a=color>>24;
    if (usage==2) return a|(g<<8)|(b<<16)|(r<<24);
    /* Native meters read fill order from blue and shape from alpha. Keep
     * shape in green too for the renderer's existing coverage blend, so
     * antialiased edges retain the scene instead of becoming black. */
    if (usage==4) return a|(r<<8)|(a<<16)|(r<<24);
    return color;
}

/* Decode one slice to BGRA. Compressed blocks remain 4x4 even for tiny mips.
 * pulse, where given, is called every 16 block rows. */
static void mcc_bitmap_decode(unsigned char const *input,uint32_t *output,
    unsigned width,unsigned height,short format,unsigned usage,uint32_t const *palette,void (*pulse)(void))
{
    unsigned x,y;
    if (format>=14 && format!=17) {
        unsigned step=format==14 ? 8 : 16;
        for (y=0;y<height;y+=4) for (x=0;x<width;x+=4) {
            unsigned char rgba[64];
            unsigned dx,dy;
            if (pulse && !x && !(y & 63u)) pulse();
            if (format==14) bcdec_bc1(input,rgba,16);
            else if (format==15) bcdec_bc2(input,rgba,16);
            else if (format==16) bcdec_bc3(input,rgba,16);
            else bcdec_bc7(input,rgba,16);
            input+=step;
            for (dy=0;dy<4 && y+dy<height;dy++) for (dx=0;dx<4 && x+dx<width;dx++) {
                unsigned char const *p=rgba+(dy*4+dx)*4;
                uint32_t color=p[2]|((uint32_t)p[1]<<8)|((uint32_t)p[0]<<16)|((uint32_t)p[3]<<24);
                output[(y+dy)*width+x+dx]=mcc_channels(color,usage);
            }
        }
    } else {
        unsigned step=mcc_bitmap_bytes(format);
        for (x=0;x<width*height;x++) output[x]=mcc_channels(mcc_pixel(input+x*step,format,palette),usage);
    }
}

/* ---------- BC7 as DXT5
 *
 * A BC7 bitmap (MCC's "high quality compression", format 18) was decoded to
 * 32-bit ARGB at the map's load: four times its own size (BC7 is a byte a
 * texel), for every BC7 bitmap of the map, for as long as it is loaded. A
 * BC7 block is 16 bytes, as a DXT5 one is, so each is decoded and encoded
 * again as DXT5 where it lies, and the bitmap goes on as an ordinary DXT5
 * one - which every renderer draws already. DXT5 keeps less than BC7 (its
 * colors are two endpoints and two points between them, a block's), the
 * price of a quarter of the memory.
 *
 * The encoder: the colors' endpoints from the principal axis of the block's
 * texels (the texels furthest along it), then refined twice by least
 * squares over the indices they chose, whichever errs least kept; alpha from
 * its smallest and largest values, eight steps between them. */
static unsigned mcc_dxt_565(float const color[3])
{
    int r=(int)(color[0]*31.0f/255.0f+0.5f),g=(int)(color[1]*63.0f/255.0f+0.5f),b=(int)(color[2]*31.0f/255.0f+0.5f);
    r=r<0 ? 0 : r>31 ? 31 : r;g=g<0 ? 0 : g>63 ? 63 : g;b=b<0 ? 0 : b>31 ? 31 : b;
    return (unsigned)(r<<11|g<<5|b);
}

static void mcc_dxt_expand(unsigned color,int out[3])
{
    int r=(color>>11)&31,g=(color>>5)&63,b=color&31;
    out[0]=r<<3|r>>2;out[1]=g<<2|g>>4;out[2]=b<<3|b>>2;
}

/* each texel's nearest of the four colors; the summed squared error */
static uint32_t mcc_dxt_indices(unsigned char const *rgba,unsigned c0,unsigned c1,uint32_t *error)
{
    int palette[4][3],i,j,k;
    uint32_t indices=0,total=0;
    mcc_dxt_expand(c0,palette[0]);mcc_dxt_expand(c1,palette[1]);
    for (k=0;k<3;k++) {
        palette[2][k]=(2*palette[0][k]+palette[1][k])/3;
        palette[3][k]=(palette[0][k]+2*palette[1][k])/3;
    }
    for (i=0;i<16;i++) {
        uint32_t best=0xffffffffu;
        int chosen=0;
        for (j=0;j<4;j++) {
            int dr=rgba[i*4]-palette[j][0],dg=rgba[i*4+1]-palette[j][1],db=rgba[i*4+2]-palette[j][2];
            uint32_t distance=(uint32_t)(dr*dr+dg*dg+db*db);
            if (distance<best) {best=distance;chosen=j;}
        }
        indices|=(uint32_t)chosen<<(2*i);
        total+=best;
    }
    *error=total;
    return indices;
}

static void mcc_dxt_color(unsigned char const *rgba,unsigned char *out)
{
    static float const weight[4]={1.0f,0.0f,2.0f/3.0f,1.0f/3.0f};
    float mean[3]={0,0,0},covariance[6]={0,0,0,0,0,0},axis[3],low=1e30f,high=-1e30f,color[3];
    int i,k,iteration,minimum[3]={255,255,255},maximum[3]={0,0,0},lowest=0,highest=0;
    unsigned c0,c1;
    uint32_t indices,error;
    for (i=0;i<16;i++) for (k=0;k<3;k++) {
        mean[k]+=rgba[i*4+k];
        if (rgba[i*4+k]<minimum[k]) minimum[k]=rgba[i*4+k];
        if (rgba[i*4+k]>maximum[k]) maximum[k]=rgba[i*4+k];
    }
    for (k=0;k<3;k++) mean[k]/=16.0f;
    for (i=0;i<16;i++) {
        float d0=rgba[i*4]-mean[0],d1=rgba[i*4+1]-mean[1],d2=rgba[i*4+2]-mean[2];
        covariance[0]+=d0*d0;covariance[1]+=d0*d1;covariance[2]+=d0*d2;
        covariance[3]+=d1*d1;covariance[4]+=d1*d2;covariance[5]+=d2*d2;
    }
    for (k=0;k<3;k++) axis[k]=(float)(maximum[k]-minimum[k]);
    for (iteration=0;iteration<8;iteration++) {
        float x=covariance[0]*axis[0]+covariance[1]*axis[1]+covariance[2]*axis[2];
        float y=covariance[1]*axis[0]+covariance[3]*axis[1]+covariance[4]*axis[2];
        float z=covariance[2]*axis[0]+covariance[4]*axis[1]+covariance[5]*axis[2];
        float largest=fabsf(x);
        if (fabsf(y)>largest) largest=fabsf(y);
        if (fabsf(z)>largest) largest=fabsf(z);
        if (largest<1e-6f) break;
        axis[0]=x/largest;axis[1]=y/largest;axis[2]=z/largest;
    }
    for (i=0;i<16;i++) {
        float t=(rgba[i*4]-mean[0])*axis[0]+(rgba[i*4+1]-mean[1])*axis[1]+(rgba[i*4+2]-mean[2])*axis[2];
        if (t<low) {low=t;lowest=i;}
        if (t>high) {high=t;highest=i;}
    }
    for (k=0;k<3;k++) color[k]=rgba[highest*4+k];
    c0=mcc_dxt_565(color);
    for (k=0;k<3;k++) color[k]=rgba[lowest*4+k];
    c1=mcc_dxt_565(color);
    indices=mcc_dxt_indices(rgba,c0,c1,&error);
    for (iteration=0;iteration<2 && error;iteration++) {
        float aa=0,bb=0,ab=0,ax[3]={0,0,0},bx[3]={0,0,0},determinant,a[3],b[3];
        unsigned r0,r1;
        uint32_t refined,refined_error;
        for (i=0;i<16;i++) {
            float w=weight[(indices>>(2*i))&3];
            aa+=w*w;bb+=(1-w)*(1-w);ab+=w*(1-w);
            for (k=0;k<3;k++) {ax[k]+=w*rgba[i*4+k];bx[k]+=(1-w)*rgba[i*4+k];}
        }
        determinant=aa*bb-ab*ab;
        if (fabsf(determinant)<1e-6f) break;
        for (k=0;k<3;k++) {
            a[k]=(ax[k]*bb-bx[k]*ab)/determinant;
            b[k]=(bx[k]*aa-ax[k]*ab)/determinant;
        }
        r0=mcc_dxt_565(a);r1=mcc_dxt_565(b);
        refined=mcc_dxt_indices(rgba,r0,r1,&refined_error);
        if (refined_error>=error) break;
        c0=r0;c1=r1;indices=refined;error=refined_error;
    }
    /* (four colors whatever the order, in a DXT5 block; the larger first
    all the same, as DXT1's four-color blocks are, for any decoder that asks) */
    if (c0<c1) {unsigned swap=c0;c0=c1;c1=swap;indices^=0x55555555u;}
    else if (c0==c1) indices=0;
    out[0]=(unsigned char)c0;out[1]=(unsigned char)(c0>>8);
    out[2]=(unsigned char)c1;out[3]=(unsigned char)(c1>>8);
    for (i=0;i<4;i++) out[4+i]=(unsigned char)(indices>>(8*i));
}

static void mcc_dxt_alpha(unsigned char const *rgba,unsigned char *out)
{
    int i,j,low=255,high=0,palette[8];
    uint64_t bits=0;
    for (i=0;i<16;i++) {
        if (rgba[i*4+3]<low) low=rgba[i*4+3];
        if (rgba[i*4+3]>high) high=rgba[i*4+3];
    }
    out[0]=(unsigned char)high;out[1]=(unsigned char)low;
    memset(out+2,0,6);
    if (high==low) return;
    /* the first larger: eight steps, not six and the two extremes */
    palette[0]=high;palette[1]=low;
    for (j=1;j<7;j++) palette[j+1]=((7-j)*high+j*low)/7;
    for (i=0;i<16;i++) {
        int best=256,chosen=0;
        for (j=0;j<8;j++) {
            int distance=abs(rgba[i*4+3]-palette[j]);
            if (distance<best) {best=distance;chosen=j;}
        }
        bits|=(uint64_t)chosen<<(3*i);
    }
    for (i=0;i<6;i++) out[2+i]=(unsigned char)(bits>>(8*i));
}

static void mcc_bc7_to_dxt5(unsigned char *block)
{
    unsigned char rgba[64];
    bcdec_bc7(block,rgba,16);
    mcc_dxt_alpha(rgba,block);
    mcc_dxt_color(rgba,block+8);
}

static uint32_t mcc_morton_axis(unsigned value,unsigned width,unsigned height,unsigned depth,unsigned axis)
{
    unsigned dimensions[3]={width,height,depth},bit,position=0,n;
    uint32_t address=0;
    for (bit=1;bit<width || bit<height || bit<depth;bit<<=1) {
        for (n=0;n<3;n++) if (bit<dimensions[n]) {
            if (n==axis && (value&bit)) address|=1u<<position;
            position++;
        }
    }
    return address;
}

/* MCC input is mip-major. Native textures have Morton-ordered uncompressed
 * texels, six face-major cube chains, and explicit row/face alignment. Pack
 * directly into an owned, zeroed buffer so allocation failure is observable.
 * maximum is the last mip packed (the guest's: the engine's
 * rasterizer_xbox_bitmap_get_max_mipmap_count, or a 2D DXT chain's whole
 * tail); the caller sets the descriptor's flags afterwards. The axis tables
 * are the caller's (8192, 8192 and 512 entries), off the stack. */
static int mcc_bitmap_pack(struct mcc_bitmap_layout const *bitmap,unsigned maximum,unsigned char const *source,
    uint32_t source_size,unsigned char *target,uint32_t target_size,
    uint32_t *x_address,uint32_t *y_address,uint32_t *z_address)
{
    static unsigned const cube_face[6]={0,2,1,3,4,5};
    uint32_t out=0;
    unsigned face,faces=bitmap->type==2 ? 6 : 1;
    unsigned bytes=mcc_bitmap_bytes(bitmap->format);
    memset(target,0,target_size);
    for (face=0;face<faces;face++) {
        unsigned level;
        uint32_t input=0;
        for (level=0;level<=maximum;level++) {
            unsigned width=MCC_BITMAP_KERNEL_MAX(bitmap->width>>level,1);
            unsigned height=MCC_BITMAP_KERNEL_MAX(bitmap->height>>level,1);
            unsigned depth=MCC_BITMAP_KERNEL_MAX(bitmap->depth>>level,1);
            uint32_t size=bytes ? width*height*depth*bytes : ((width+3)/4)*((height+3)/4)*depth*(bitmap->format==14 ? 8 : 16);
            uint32_t at=input+(faces==6 ? cube_face[face] : 0)*size;
            unsigned x,y,z;
            if (at>source_size || size>source_size-at || out>target_size) return 0;
            if (bitmap->flags&16) {
                unsigned pitch=(width*bytes+63)&~63u;
                if (pitch*height>target_size-out) return 0;
                for (y=0;y<height;y++) memcpy(target+out+y*pitch,source+at+y*width*bytes,width*bytes);
                out+=pitch*height;
            } else {
                if (size>target_size-out) return 0;
                if (!bytes) memcpy(target+out,source+at,size);
                else {
                    if (width>8192 || height>8192 || depth>512) return 0;
                    for (x=0;x<width;x++) x_address[x]=mcc_morton_axis(x,width,height,depth,0);
                    for (y=0;y<height;y++) y_address[y]=mcc_morton_axis(y,width,height,depth,1);
                    for (z=0;z<depth;z++) z_address[z]=mcc_morton_axis(z,width,height,depth,2);
                    for (z=0;z<depth;z++) for (y=0;y<height;y++) for (x=0;x<width;x++) {
                        uint32_t address=x_address[x]|y_address[y]|z_address[z];
                        memcpy(target+out+address*bytes,source+at+((z*height+y)*width+x)*bytes,bytes);
                    }
                }
                out+=size;
            }
            input+=size*faces;
        }
        out=(out+127)&~127u;
    }
    return out==target_size;
}

/* A texture's whole pixel work, as one job (mcc_convert_bitmap_job in the
 * host, mcc_texture_prepare in the guest): its BC7 blocks encoded as DXT5 in
 * place (transcode), its levels decoded to BGRA into decoded (convert, of
 * decoded_size bytes), and the result packed into target. */
struct mcc_bitmap_work {
    struct mcc_bitmap_layout source, packed;
    unsigned maximum, usage, faces, levels;
    int transcode, convert;
    uint32_t raw_size, decoded_size, target_size, bytes_per_pixel;
    uint32_t const *palette;
};

/* the work a job's description says (mcc_convert_job.h); its palette where
the description's guest address is in this process (the host reads the
guest's memory where it lies) */
static void mcc_bitmap_work_from(struct mcc_convert_bitmap const *description,struct mcc_bitmap_work *work)
{
    memset(work,0,sizeof(*work));
    work->source.width=(int)description->source_width;
    work->source.height=(int)description->source_height;
    work->source.depth=(int)description->source_depth;
    work->source.type=(short)description->source_type;
    work->source.format=(short)description->source_format;
    work->source.flags=description->source_flags;
    work->source.mipmap_count=(short)description->source_mipmaps;
    work->packed.width=(int)description->packed_width;
    work->packed.height=(int)description->packed_height;
    work->packed.depth=(int)description->packed_depth;
    work->packed.type=(short)description->packed_type;
    work->packed.format=(short)description->packed_format;
    work->packed.flags=description->packed_flags;
    work->packed.mipmap_count=(short)description->packed_mipmaps;
    work->maximum=description->maximum;
    work->usage=description->usage;
    work->faces=description->faces;
    work->levels=description->levels;
    work->transcode=(int)description->transcode;
    work->convert=(int)description->convert;
    work->raw_size=description->raw_size;
    work->decoded_size=description->decoded_size;
    work->target_size=description->target_size;
    work->bytes_per_pixel=description->bytes_per_pixel;
    work->palette=(uint32_t const *)(uintptr_t)description->palette;
}

/* 1 if it was done; 0 if the source does not fit what it says */
static int mcc_bitmap_work_run(struct mcc_bitmap_work const *work,unsigned char *raw,unsigned char *decoded,
    unsigned char *target,uint32_t *axes,void (*pulse)(void))
{
    uint32_t input_offset=0,output_offset=0;
    unsigned level;
    if (work->transcode) {
        uint32_t block;
        for (block=0;block<work->raw_size/16;block++) {
            if (pulse && !(block&4095u)) pulse();
            mcc_bc7_to_dxt5(raw+(size_t)block*16);
        }
    }
    if (work->convert) {
        memset(decoded,0,work->decoded_size);
        for (level=0;level<work->levels;level++) {
            unsigned w=MCC_BITMAP_KERNEL_MAX(work->source.width>>level,1);
            unsigned h=MCC_BITMAP_KERNEL_MAX(work->source.height>>level,1);
            unsigned d=MCC_BITMAP_KERNEL_MAX(work->source.depth>>level,1),slice;
            unsigned source_bytes=work->bytes_per_pixel ? w*h*work->bytes_per_pixel :
                ((w+3)/4)*((h+3)/4)*(work->source.format==14 ? 8 : 16);
            for (slice=0;slice<work->faces*d;slice++) {
                if ((uint64_t)input_offset+source_bytes>work->raw_size ||
                    (uint64_t)output_offset+(uint64_t)w*h*4>work->decoded_size) return 0;
                mcc_bitmap_decode(raw+input_offset,(uint32_t *)(decoded+output_offset),w,h,work->source.format,
                    work->usage,work->palette,pulse);
                input_offset+=source_bytes;output_offset+=w*h*4;
            }
        }
    }
    return mcc_bitmap_pack(&work->packed,work->maximum,work->convert ? decoded : raw,
        work->convert ? work->decoded_size : work->raw_size,target,work->target_size,
        axes,axes+8192,axes+16384);
}

#endif
