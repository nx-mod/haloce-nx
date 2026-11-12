#ifndef PLASMA_LIGHTING_H
#define PLASMA_LIGHTING_H

/* the most any promoted light's radius is scaled: authored_emitter_lighting.c's
   profiles, covenant_lighting.c's and plasma_lighting_prepare's */
#define PLASMA_LIGHTING_MAXIMUM_SCALE 3.0f

void plasma_lighting_prepare(long light_index, long object_index, long definition_index);
void plasma_lighting_rank(void);
float plasma_lighting_radius(long light_index, long object_index, float radius);
/* a promoted light's drawn color at Glow Intensity */
void plasma_lighting_color(long light_index, long object_index, float *red, float *green, float *blue);
int light_port_attachment_radius(long light_index, long object_index, float *radius, int restore);
int plasma_lighting_vehicle_occlusion(long light_index);
void plasma_lighting_frame_begin(void);
void plasma_lighting_frame_end(void);
void plasma_lighting_reset(void);
void plasma_lighting_window_begin(void);
void plasma_lighting_window_end(void);
void plasma_lighting_submitted(long light_index, long rasterizer_index);
int plasma_lighting_selected(long rasterizer_index);
int plasma_lighting_prong_cone(long rasterizer, float axis[3], float origin[3], float *radius);
void plasma_falloff_begin(long rasterizer_index);
void plasma_falloff_specular_begin(long rasterizer_index);
void plasma_specular_begin(long rasterizer_index);
unsigned long plasma_specular_color(unsigned long material);
void plasma_falloff_end(void);
void plasma_falloff_dispose(void);

/* object_lights.c: validate the owner before refreshing an attachment. */
int light_port_refresh_attachment(long light_index, long object_index);
int light_port_dynamic_set(long light_index, long object_index, int enabled);
float light_port_view_distance(long light_index);
float light_port_camera_distance(long light_index);
int light_port_can_reach_view(long light_index);

#endif
