/* MCC immutable textures have identities independent of guest physical memory. */
#ifndef MCC_TEXTURE_BRIDGE_H
#define MCC_TEXTURE_BRIDGE_H

void *mcc_texture_bridge_find(void const *bitmap);
void mcc_texture_bridge_meter(void const *bitmap);
void *mcc_texture_bridge_register(void const *bitmap, unsigned long format,
    unsigned long size, void const *pixels, unsigned long bytes);
void *mcc_texture_bridge_register_linear(void const *bitmap, unsigned long format,
    unsigned long width, unsigned long height, unsigned long pitch, void const *pixels, unsigned long bytes);
void mcc_texture_bridge_dispose(void);
/* A registered header's pixels, description and stock HUD replacement (-1:
 * none), for the renderer to make its copy from. FALSE for any other header.
 * (struct xgpu_texture_description: xgpu.h) */
struct xgpu_texture_description;
int mcc_texture_bridge_source(unsigned long const *resource, void const **pixels, unsigned long *bytes,
    struct xgpu_texture_description *description, long *hud_asset);
/* Returns estimated GPU bytes charged, or zero when skipped/unsuccessful. */
unsigned long mcc_texture_bridge_prepare(void const *bitmap, unsigned long budget);

#endif
