# Dynamic Lighting Performance: Plan

Written 2026-11-06, after a code audit of the `dynamic-lighting` branch. No fix here has been started.

**Target devices:** the Switch Lite and the Nokia 8 (Snapdragon 835, Vulkan), the slowest phone at hand.

**Goal:** with all lighting on, a scene holds as close as possible to the frame rate it has with lighting off. A user reported a firefight falling from 60 fps to the 30s on a Switch, with one thread pinned at 100% even when nothing visibly lights up.

**Approach:** cut every cost we can, without a measured baseline first. The Switch profiler (`debug.profiler`) and Android's (`debug.profile_run`) stay available if a result is surprising.

The costs in this document come from reading the code and from symbol sizes in the Switch build. None was measured on a device.

## What differs per target

| | Switch Lite | Nokia 8 (Vulkan) |
| --- | --- | --- |
| Who records GPU commands | A render thread on a helper core | The game thread (`host_vk_shaders.c:23`) |
| What a field upload does | 2 full GPU barriers (`host_dk.c:2625`) | Ends the render pass (`host_vk_texture.c:520`), which is expensive on a tiled GPU |
| What the write tracking does on an upload | Marks pages in a table | A real `mprotect` per watched page, then more to protect them again |
| Game thread's core | Its own; the other two are shared by everything else | Not pinned; the scheduler places threads across 4 big and 4 little cores |

The Android GL image links the lighting stub, so a phone that fell back to `display.renderer = "gl"` tests none of this. Check the renderer before each Nokia 8 run.

## Phase 1: Cut the rays

1. **Held-weapon lights use the 96-ray grid** in place of 1,536, and keep occlusion. That covers the AR screen, Needler crystals, held fuel rods, and first-person and hand-held muzzle flashes. Vehicle guns keep the full grid, as their radius is 5 to 7 units.
2. **A per-frame budget for occlusion.** The 12 ms limit (`plasma_occlusion.c:460`) never limits anything at 60 fps, so it stays only as the shortest gap between traces, which matters at 120 fps. New lights go first, then the most out-of-date. The rest reuse their depths, which the field rebuild can already offset for a moved light, up to a limit on how far it has moved. Each frame always traces at least one light.
3. **A per-platform budget table** goes in `lighting_config.c`. The Switch and PS5 count rays. Android counts time, because its phones differ too much for one ray count:

   | Platform | Fields | Limit per frame |
   | --- | --- | --- |
   | Switch | 3 | 1,800 rays |
   | Android | 8 | 4 ms of occlusion work (fixed for now: Phase 5 makes it adapt) |
   | PS5 | 8 | None yet (Phase 3 profiles it) |

4. **A batch overflow stops costing double.** Past 512 objects (`plasma_collision.c:163,364`), keep the BSP results and skip the objects, without rerunning every ray through the stock query.

**Done when:** ray counts stay within budget, and walls still block light on the Switch and on Android.

## Phase 2: Fix the upload path

This matters most on the Nokia 8, where every upload now splits a render pass.

1. **Update fields once per view, at the end of `lights_preprocess_scene`,** for lights that pass a frustum test, not lazily during draws. The on-demand path stays as a fallback for anything missed.
2. **Upload them together,** so a view has one barrier pair on deko3d and one render pass split on Vulkan, not one per texture.
3. **Try moving `render_sky` after light setup** (`render.c`). The sky is drawn first today, so the pass has already begun when fields upload. If nothing in the sky depends on the order, there is no split at all.
4. **Build only the texture passes the light used last view.** A pass that turns out to be needed is built on demand.
5. **Add a direct field upload on both backends:** linear pixels straight into the stream. This removes:
   - the swizzle and the unswizzle
   - `reach_note` (a known constant replaces it, for radial fields)
   - the `malloc`
   - the texture cache invalidation (`xbox_textures_dk.c:1056`, and the same in `xbox_textures_vk.c`)
   - Android's `mprotect` calls
6. **Round the AR and prong direction,** here and in `ammo_counter_falloff.c:87`, so that a small sway doesn't rebuild the mask each frame.

**Done when:** barriers and pass splits per view no longer grow with the field count, and the picture matches Phase 1 on both backends.

## Phase 3: Remove the repeated CPU work

1. **Config lookups:** enum-indexed flags in place of string comparisons (`lighting_config.c:67`), refreshed when settings change. Forced-off and `platform_off` keep working.
2. **Tag names:** a per-map table from tag to lighting profile, replacing the per-frame `strcmp` calls in the emitter, Covenant, rocket and muzzle modules.
3. **Object walks:** one shared walk per view fills candidate lists for all the modules and the scene checks, replacing up to 5 walks plus 1 per field.
4. **Ray batch:** generation stamps in place of the 307 KB memset (`plasma_collision.c:278`) and the 98 KB one at its end.
5. **Stationary lights:** skip the fade and its copies once a field has settled, and check the scene less often.
6. **Moving lights:** skip the scene check (`plasma_occlusion.c:477`) until the light stops.
7. **Lighting off:** stop the frame-end dispose from running every frame (`plasma_lighting.c:156`).

8. **PS5 profile:** at 4K 120 fps the whole frame is 8.3 ms, so 12,288 rays could matter even on that CPU. Profile it here, and set its limit from that. It can use the time limit if a fixed one doesn't fit.

**Done when:** the harnesses pass, and the forced-off switches and menu behave as before.

## Phase 4: NEON

Port the SSE field rebuild (`plasma_occlusion.c:295`) to NEON, and vectorize the fade and pixel conversion. A harness compares the output with the scalar path. Both targets are ARM, and both run the scalar path today.

## Phase 5: Limit firefight lights

1. **Constant-time attachment lookup,** removing the quadratic scans (up to 128 attachments).
2. **Fewer reconnects:** only attachments that are lit and near the view reconnect to the map. Whether glows can stay connected between frames depends on what simulation queries see, so that gets audited first, and is left alone if it isn't safe.
3. **A per-platform cap on promoted lights per view,** ranked by importance, with a per-platform radius scale (today 1.75 to 2.75 times, so about 3 to 7.5 times the lit area). Lights over the cap stay stock. Starting caps, to tune by eye: Switch 6, Android 8, PS5 none.

4. **Android's time limit adapts.** N becomes what's left of the frame after everything else on the game thread, smoothed over about half a second, with a floor that keeps a held-weapon light working on the Nokia 8 and a ceiling of 4 to 5 ms, so fast phones don't heat up until they throttle. It uses the game thread's busy time, excluding the wait for present, against the display's real refresh rate. A debug key pins N, so test runs stay comparable.
5. **The promoted-light cap adapts on Android.** The time limit can't help if the GPU is the bottleneck, so the cap steps down and up like the Switch's `dynamic_resolution` (`d3d8_dk.c:112`): drop quickly, rise slowly, with a hold in between. Vulkan's per-frame GPU time outside its profiler still needs checking.

This phase visibly changes the picture, so you judge it.

## Phase 6: Split the work across cores

1. **A small pool of guest threads,** made through `CreateThread` (`xbox_kernel.c:525`). The render thread isn't used: it is host code, delays the GPU if it falls behind, and Android doesn't have one.
2. **One job per field** (rays, rebuild and pixels), handed out at the end of light setup. The game thread takes jobs too, so a slow little core can't hold up the frame for long. All jobs finish before `render_objects`, then the game thread uploads.
3. **No background lag and no object snapshots.** Nothing else runs on the game thread during the jobs, so the simulation and renderer can't change what they read. A lagging field isn't an option anyway: world-pass fields are light-local, so one would be misplaced on a fast bolt.
4. **A per-thread ray batch,** and an audit of the collision path for shared state. Known so far: `collision_bsp_usage_times` and the `warned_about_*` flags. Still to check: cluster iteration and `collision_model_instance_new`.
5. **A config switch** to go back to single-threaded, for comparison.

**Done when:** the output is identical either way, with no audio or render-thread starvation on the Switch.

## How the work runs

- Each step is its own commit, in phase order. Nothing is pushed unless you ask.
- Harnesses show no new failures against the 159 that already fail.
- After each phase, the Switch Lite and Nokia 8 are tried in a firefight, and PS5 is checked for the picture, since most of this code is shared. You run those, or tell me when I can use a device.
- Each phase can be reverted on its own, so a visual regression stays easy to isolate.
