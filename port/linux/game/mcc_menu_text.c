/* MCC catalog text is authored by package makers, not to the fixed line
 * lengths of the original menus. Wrap a draw-local copy in every style. */
#include "cseries.h"
#include "math/integer_math.h"
#include "math/real_math.h"
#include "tag_files/tag_groups.h"
#include "text/draw_string.h"
#include "rasterizer/rasterizer.h"
#include "mcc_maps.h"
#include "mcc_menu_text.h"
#include <string.h>

static boolean field(long tag, short index)
{
    char const *name, *leaf;
    if (tag == NONE) return FALSE;
    name=tag_get_name(tag); leaf=strrchr(name,'\\'); leaf=leaf ? leaf+1 : name;
    if (!strcmp(name,"pc\\main_menu\\settings_select\\player_setup\\player_profile_edit\\network_setup\\mcc_download\\description") ||
        !strcmp(name,"pc\\main_menu\\settings_select\\player_setup\\player_profile_edit\\network_setup\\mcc_downloads\\description")) return TRUE;
    if (!strncmp(name,"pc\\mcc_xbox\\",12) && !strcmp(leaf,"status")) return TRUE;
    if (!strcmp(leaf,"mcc_folder_name") || !strcmp(leaf,"mcc_folder_help")) return TRUE;
    if (index < MCC_MAPS_FIRST_DISPLAY_INDEX || index >= MCC_MAPS_FIRST_DISPLAY_INDEX+MCC_MAPS_MAXIMUM) return FALSE;
    return !strcmp(leaf,"mp_map_right_name") || !strcmp(leaf,"mp_map_right_data") || !strcmp(leaf,"lobby_map_name");
}

/* Measure from the left with the horizontal clamp removed. The native
 * renderer clips a long line; it does not wrap it for Classic PC or Stock. */
static int width_of(wchar_t const *text)
{
    rectangle2d measure, ink, cursor;
    set_rectangle2d(&measure,0,0,SHORT_MAX,SHORT_MAX);
    draw_unicode_string_compute_bounds(&measure,text,&ink,&cursor);
    return MAX(cursor.x0,ink.x1);
}

/* Laying a text out measures it again and again: a row's name after every
 * character taken off it, a description after every character added to a
 * line (wrap) and every one taken off its end, each measure the whole line
 * or text again, in the game's own measure. That was done every frame, for
 * every MCC row and text shown, and an MCC map's description held a
 * Switch's map list near 7 fps. The result depends only on the text, the
 * font and the box, so the last few are kept, and a frame draws them as
 * they are. (Taken from the heap at their first use, not the image's.) */
#define MCC_TEXT_ROWS 24
#define MCC_TEXT_LAYOUTS 8
#define MCC_TEXT_CACHED 512

struct mcc_text_row { long font; unsigned stamp; wchar_t text[64], fitted[64]; };
struct mcc_text_layout {
    long font, fitted_font;
    int width, height, used;
    unsigned stamp;
    wchar_t text[MCC_TEXT_CACHED], wrapped[512];
};
static struct mcc_text_row *mcc_text_rows;
static struct mcc_text_layout *mcc_text_layouts;
static unsigned mcc_text_stamp;

static int mcc_text_same(wchar_t const *a, wchar_t const *b, int limit)
{
    int i;
    for (i=0; i<limit; i++) {
        if (a[i]!=b[i]) return FALSE;
        if (!a[i]) return TRUE;
    }
    return FALSE;
}

/* the text's length, or NONE where it is longer than limit-1 */
static int mcc_text_length(wchar_t const *text, int limit)
{
    int length;
    for (length=0; length<limit; length++) if (!text[length]) return length;
    return NONE;
}

static void mcc_text_row_fit(wchar_t text[64], long font)
{
    real_argb_color color={1,1,1,1};
    int used;
    draw_string_set_draw_mode(font,NONE,0,0,&color);
    if (width_of(text)<=310) return;
    for (used=0; used<60 && text[used]; used++) {}
    do {
        text[used]='.'; text[used+1]='.'; text[used+2]='.'; text[used+3]=0;
    } while (width_of(text)>310 && used-- > 0);
}

/* MCC's PC-style list callbacks provide a draw-only label, not the catalog
 * title. Fit the narrowest row (Modern: 312, Stock/Classic PC: 330), keeping
 * its native single-line large font and leaving selection arrows unobscured. */
void mcc_menu_text_row(wchar_t text[64])
{
    long font=tag_loaded('font',"ui\\large_ui");
    struct mcc_text_row *row, *oldest;
    int i;
    if (font==NONE) return;
    if (!mcc_text_rows && (mcc_text_rows=malloc(MCC_TEXT_ROWS*sizeof(*mcc_text_rows)))!=NULL)
        memset(mcc_text_rows,0,MCC_TEXT_ROWS*sizeof(*mcc_text_rows));
    if (!mcc_text_rows || mcc_text_length(text,64)==NONE) {mcc_text_row_fit(text,font);return;}
    oldest=&mcc_text_rows[0];
    for (i=0; i<MCC_TEXT_ROWS; i++) {
        row=&mcc_text_rows[i];
        if (row->stamp && row->font==font && mcc_text_same(row->text,text,64)) {
            row->stamp=++mcc_text_stamp;
            memcpy(text,row->fitted,sizeof(row->fitted));
            return;
        }
        if (row->stamp<oldest->stamp) oldest=row;
    }
    memcpy(oldest->text,text,sizeof(oldest->text));
    mcc_text_row_fit(text,font);
    memcpy(oldest->fitted,text,sizeof(oldest->fitted));
    oldest->font=font;
    oldest->stamp=++mcc_text_stamp;
}

static int wrap(wchar_t const *text, wchar_t output[512], int width)
{
    int used=0, start=0, space=NONE, i;
    for (i=0; text[i] && used<509; i++) {
        wchar_t c=text[i];
        if (c=='\n' && i && text[i-1]=='\r') continue;
        if (c=='\n') c='\r';
        output[used++]=c; output[used]=0;
        if (c=='\r') { start=used; space=NONE; continue; }
        if (c==' ') space=used-1;
        if (width_of(output+start)>width) {
            if (space>=start) { output[space]='\r'; start=space+1; space=NONE; }
            else if (used-start>1) { output[used-1]='\r'; output[used++]=c; output[used]=0; start=used-1; }
        }
    }
    output[used]=0; return used;
}

static int text_height(wchar_t const *text)
{
    rectangle2d measure, ink, cursor;
    set_rectangle2d(&measure,0,0,SHORT_MAX,SHORT_MAX);
    draw_unicode_string_compute_bounds(&measure,text,&ink,&cursor);
    return MAX(ink.y1,cursor.y1);
}

boolean mcc_menu_text_draw(long tag, short index, wchar_t const *text, long font, short justification,
    real_argb_color const *color, rectangle2d const *bounds, rectangle2d const *parent_clip)
{
    wchar_t wrapped[512];
    rectangle2d area=*bounds, clip;
    int width=bounds->x1-bounds->x0-2, height=bounds->y1-bounds->y0, used;
    long small;
    char const *name;
    if (!field(tag,index)) return FALSE;
    name=tag_get_name(tag);
    /* Xbox cards reserve their lower row for earned badges. Keep the native
     * text origin, but never let an authored description draw over that row. */
    if (!strncmp(name,"pc\\mcc_xbox\\",12) && strstr(name,"\\mcc_folder_help")) {
        area.y1=(short)MIN(area.y1,area.y0+83);
        height=area.y1-area.y0;
    }
    clip=area;
    if (width<=0 || height<=0) return TRUE;
    {
        /* (laid out once for this text, font and box: mcc_text_layouts) */
        struct mcc_text_layout *layout=NULL, *oldest=NULL;
        int i, cacheable=mcc_text_length(text,MCC_TEXT_CACHED)!=NONE;
        if (cacheable && !mcc_text_layouts &&
            (mcc_text_layouts=malloc(MCC_TEXT_LAYOUTS*sizeof(*mcc_text_layouts)))!=NULL)
            memset(mcc_text_layouts,0,MCC_TEXT_LAYOUTS*sizeof(*mcc_text_layouts));
        if (cacheable && mcc_text_layouts) {
            oldest=&mcc_text_layouts[0];
            for (i=0; i<MCC_TEXT_LAYOUTS; i++) {
                struct mcc_text_layout *entry=&mcc_text_layouts[i];
                if (entry->stamp && entry->font==font && entry->width==width && entry->height==height &&
                    mcc_text_same(entry->text,text,MCC_TEXT_CACHED)) {layout=entry;break;}
                if (entry->stamp<oldest->stamp) oldest=entry;
            }
        }
        if (layout) {
            layout->stamp=++mcc_text_stamp;
            memcpy(wrapped,layout->wrapped,sizeof(wrapped));
            used=layout->used;
            font=layout->fitted_font;
        } else {
            long given=font;
            draw_string_set_draw_mode(font,NONE,0,0,color);
            used=wrap(text,wrapped,width);
            small=tag_loaded('font',"ui\\small_ui");
            if (text_height(wrapped)>height && small!=NONE && small!=font) {
                font=small; draw_string_set_draw_mode(font,NONE,0,0,color);
                used=wrap(text,wrapped,width);
            }
            /* Arbitrarily long authored text cannot fit a fixed panel. Keep complete
             * visible lines and signal truncation, instead of cutting glyphs in half. */
            if (text_height(wrapped)>height) {
                while (used && text_height(wrapped)>height) wrapped[--used]=0;
                while (used && (wrapped[used-1]==' ' || wrapped[used-1]=='\r')) wrapped[--used]=0;
                while (used) {
                    int start=used;
                    while (start && wrapped[start-1]!='\r') start--;
                    wrapped[used]='.'; wrapped[used+1]='.'; wrapped[used+2]='.'; wrapped[used+3]=0;
                    if (width_of(wrapped+start)<=width && text_height(wrapped)<=height) break;
                    wrapped[--used]=0;
                }
            }
            if (oldest) {
                memset(oldest,0,sizeof(*oldest));
                for (i=0; i<MCC_TEXT_CACHED && text[i]; i++) oldest->text[i]=text[i];
                memcpy(oldest->wrapped,wrapped,sizeof(wrapped));
                oldest->font=given;oldest->fitted_font=font;
                oldest->width=width;oldest->height=height;oldest->used=used;
                oldest->stamp=++mcc_text_stamp;
            }
        }
    }
    if (parent_clip) {
        clip.x0=MAX(clip.x0,parent_clip->x0); clip.y0=MAX(clip.y0,parent_clip->y0);
        clip.x1=MIN(clip.x1,parent_clip->x1); clip.y1=MIN(clip.y1,parent_clip->y1);
    }
    draw_string_set_draw_mode(font,NONE,justification,0,color);
    if (clip.x1>clip.x0 && clip.y1>clip.y0) rasterizer_draw_unicode_string(&area,&clip,NULL,0,wrapped);
    return TRUE;
}
