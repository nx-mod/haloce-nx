#include "guest_host.h"
#include "android_render_policy.h"

int android_geometry_fixes = 1;
int android_texture_fixes = 1;
int android_render_chip = _android_chip_unknown;

void android_render_policy_initialize(void)
{
#ifdef HALO_PS5
	/* (no phone's GPU: the PS5's host has none of Android's imports, and its
	renderer is Vulkan; the defaults keep the texture fixes as before) */
	android_render_chip = _android_chip_unknown;
#else
	android_render_chip = host_android_chip_family();
#endif
	if (android_render_chip < _android_chip_unknown || android_render_chip > _android_chip_other)
		android_render_chip = _android_chip_unknown;
	android_geometry_fixes = android_render_chip == _android_chip_unknown ||
		android_render_chip == _android_chip_snapdragon;
	android_texture_fixes = android_render_chip != _android_chip_other;
}
