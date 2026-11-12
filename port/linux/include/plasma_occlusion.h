/* Bounded, render-only collision visibility for plasma lights. */
#ifndef PLASMA_OCCLUSION_H
#define PLASMA_OCCLUSION_H
void plasma_occlusion_reset(void);
/* (port: the frame's end) traces on helper threads, collected into their fields */
void plasma_occlusion_collect(void);
void plasma_occlusion_frame_begin(void);
void plasma_occlusion_window(void);
void plasma_occlusion_submit(long light, long rasterizer_index);
/* this view's lights are known, and nothing has been drawn */
void plasma_occlusion_prepare_view(void);
void plasma_occlusion_note_promoted(int promoted, int shrunk);
/* the bolts' lights a view had, whether or not they could reach it, and of those
promoted, how many are projectiles' bolts and weapons' glows (the log's split) */
void plasma_occlusion_note_kinds(int bolts_seen, int bolts, int glows);
int plasma_occlusion_light_visible(long rasterizer_index, float radius);
void plasma_occlusion_model_slot(long rasterizer_index, short slot);
void *plasma_occlusion_texture(long rasterizer_index, int specular);
void *plasma_occlusion_model_texture(int slot, float transform[4]);
unsigned long plasma_occlusion_view_serial(void);
float plasma_occlusion_specular_scale(long rasterizer_index);
/* port: a field's texels, in linear order, straight to the renderer's image
   (xbox_textures_dk.c, xbox_textures_vk.c); 0 if it could not be done. Reach:
   how far from the box's centre, of its 0.866, a texel can be set (negative: not
   said). Forget it before releasing the texture. */
int halo_field_texture_write(const void *texture, const unsigned long *pixels, float reach);
void halo_field_texture_forget(const void *texture);
/* the sphere a radial pass fills (half the box), and a texel's filtered margin */
#define PLASMA_FIELD_RADIAL_REACH (0.5f + 0.10826f)
#endif
