# Dynamic Lighting Performance: Plan

Written 2026-11-06, after a code audit of the `dynamic-lighting` branch, and revised the same day with measurements from a Switch Lite. **Phases 1 and 2 are done and running on the Switch Lite, and one item of Phase 3 is done. None of this has run on the Nokia 8 or the PS5.** [What's next](#whats-next) has the order of the remaining work, which replaces the order of the phases below.

**Target devices:** the Switch Lite and the Nokia 8 (Snapdragon 835, Vulkan), the slowest phone at hand.

**Goal:** with all lighting on, a scene holds as close as possible to the frame rate it has with lighting off. A user reported a firefight falling from 60 fps to the 30s on a Switch, with one thread pinned at 100% even when nothing visibly lights up.

**Approach:** cut every cost we can, then measure what is left. The Switch profiler (`debug.profiler`), `debug.gpu_stats` and Android's `debug.profile_run` say where the time goes.

Costs in the phases below come from reading the code, and from symbol sizes in the Switch build, unless they say otherwise. The measurements are in [What we measured](#what-we-measured).

## What we measured

All on the Switch Lite at 720p with the profiler on. Each comparison is two passes played by hand, so treat small differences with caution.

**Pillar of Autumn firefight, from a checkpoint, 35 seconds of each pass:**

| Moving lights | Average fps | Lowest 5 seconds | Game thread, average and peak | Rays per view |
| --- | --- | --- | --- | --- |
| 1,536 rays | 57.1 | 50.8 | 16.0 and 19.0 ms | 1,208 |
| 384 rays | 59.6 | 58.1 | 14.6 and 16.2 ms | 547 |

**Blood Gulch, 60 seconds, steering by hand (`debug.benchmark_manual`):**

| Run | Average fps | 1% low | Frames over 33 ms |
| --- | --- | --- | --- |
| Hosting 48 bots, lighting on (384 rays for the last 13 seconds or so) | 32.6 | 17.3 | 739 of 1,954 |
| Hosting 48 bots, lighting off | 45.7 | 25.7 | 147 of 2,743 |
| Joining a phone's match, 47 other players, lighting on with 384 rays (about 95 seconds, not 60) | 29.1 | Not recorded | Not recorded |

**Share of the game thread's time in those Blood Gulch runs:**

| Work | Hosting, lighting off | Hosting, lighting on | Joining, lighting on |
| --- | --- | --- | --- |
| Game update | 43.3% | 39.8% | 46.6% |
| Object updates (part of the game update) | 30.9% | 28.1% | 39.3% |
| Light reconnects (part of the object updates) | 6.2% | 8.4% | 11.7% |
| Rendering | 42.0% | 48.6% | 38.6% |
| Lighting setup (part of rendering) | 5.7% | 17.4% | 16.6% |
| Occlusion (part of lighting setup) | None | 11.8% | 11.2% |

**What that means for the plan:**

- **A moving light's rays were the first thing to cut,** and 384 rays did that on the Switch. With them, tracing was about 8% of the Pillar of Autumn run, down from 13.5%. `update_field` stayed at about 10%, which rays don't touch.
- **In a 48-player match, the simulation is the limit, not lighting.** Lighting costs about 13 fps there, and with it off the Switch still only averaged 45.7 fps. 60 fps at that player count isn't a realistic goal for lighting work. The goal there is to close the gap to lighting off.
- **Bot AI is cheap,** about 2% of the game thread. Hosting the bots elsewhere didn't help, because every machine updates every object.
- **Light reconnects are a large cost that nothing here has touched yet.** They happen on every tick for every attached light, lit or not.
- **Each lookup of a setting used to walk the table by name:** 5.9% of the game thread with lighting off, 2.7% with it on. With the fix, `strcmp` is 0.5% with lighting on. It hasn't been measured with lighting off.

**Not measured yet:** whether the picture holds up in a firefight with weapon lights taking turns. Past three weapon lights, some wait more than one view (2.2 waits a view at the Pillar of Autumn, with 384 rays).

## What's next

1. **Check the Nokia 8.** It is a target, and nothing has run on it. Its 4 ms budget is a guess, and a result there could change the rest of this list. Android's other phones and the PS5 get checked for the picture at the same time, once their images are built.
2. **Fewer light reconnects** (Phase 5, item 2). The per-tick path is done, not yet measured. See that item for what's left.
3. **A limit on promoted lights' reach** (Phase 5, item 3). Built, off by default, with overlay rows to compare. Not measured: `debug.gpu_stats` now counts submitted, promoted and shrunk lights, which says how much it can do.
4. **NEON** (Phase 4). Estimated at 0.5 to 1 ms a frame on both targets, as `update_field` is about 1.6 ms in a firefight. Most useful in campaign firefights.
5. **Android's limits adapt** (Phase 5, items 4 and 5), after the Nokia 8 shows what they need to do.
6. **The rest of Phase 3, by what a profile still shows.** Tag names are now a small part of what's left. PS5's profile (item 8) stays on the list.
7. **Phase 6, only if that isn't enough.** Moving field jobs off the game thread could save the most, but it is the riskiest work here, and the Switch's other two cores are shared.

Once measuring is done, `debug.profiler` goes back off on the test Switch.

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

4. **A batch overflow stops costing double.** Past 512 objects (`plasma_collision.c`), the objects it has still block and the BSP still counts for every ray, with no rerun through the stock query. Only malformed geometry falls back to that now.

**How the budget behaves:**

- Fields counts lights at work in a view, not lights with a field: a still light whose fade has settled costs only a scene check and doesn't count, until it moves, changes size or its scene changes. Otherwise three still lights would keep a shot from having a field at all, and it would draw with no occlusion.
- Weapon lights choose first, and three of them would fill the limit and leave every other light with no occlusion. When another light is at work or asking, they get one slot fewer. The one updated most lately sits out and keeps last view's field, so none waits two views running. If it has moved more than a quarter of its extent from that field, it asks like any other light. With no other light wanting a slot, they can use all of them.
- Rays are only held for a refused light that can get in. With every slot taken, they would go unused.
- On the Switch, a moving light traces 384 rays, and goes back to the full grid when it stops. At a Pillar of Autumn firefight that was 59.6 fps on average to 1,536's 57.1, and new lights had room to get occlusion. The overlay's `Moving Lights: 384 Rays` row switches it for a run, to compare. Android and PS5 keep 1,536.
- A limit only changes who traces. With no limit (PS5), the planning step doesn't run.
- Draws can't pick the order, or the first light drawn would always win, so the rays are given out before a view's first field.
- A light in view, or one a draw asked for, that was refused gets room held for it on the next view. Lights out of view with no draw asking never reach that list, so they can't take the room. It fades in as overflow lights already did.
- A light short of rays offsets the depths it has. Past half its extent from them, it draws with no occlusion that view.
- On Android, once the time is spent, a light with no new depths reuses last view's field if it is within a quarter of its extent, and otherwise draws with no occlusion. That also bounds the rebuild's cost, not only the rays'.

**Done when:** ray counts stay within budget, and walls still block light on the Switch and on Android.

## Phase 2: Fix the upload path

This matters most on the Nokia 8, where every upload now splits a render pass.

1. **Update fields once per view, at the end of `lights_preprocess_scene`,** for lights that pass a frustum test, not lazily during draws. The on-demand path stays as a fallback for anything missed. A light in view that no draw has read for 4 views stops being updated, until one does. Short lights choose their fields first, so a held weapon doesn't lose out to a far light.
2. **Upload them together,** so a view has one barrier pair on deko3d, not one per texture.
3. **Light setup moves before `render_sky`,** which is the first draw, so under Vulkan the uploads interrupt no rendering. It draws nothing itself. The sky queues lens flares for it to submit, so `lights_submit_queued_lens_flares` runs just after the sky, still after the scene's own flares. A copy into a texture, and its barrier, also no longer make a waiting clear run in a rendering of its own.
4. **Build only the texture passes the light used last view.** A new field gets a guess, the world's diffuse pass and models. A pass that turns out to be needed is built on demand.
5. **Add a direct field upload on both backends:** linear pixels straight into the stream (`halo_field_texture_write`). This removes:
   - the swizzle and the unswizzle
   - `reach_note` (a known constant replaces it, for radial fields)
   - the `malloc` on Vulkan, which copies into its ring
   - the texture cache invalidation (`xbox_textures_dk.c`, and the same in `xbox_textures_vk.c`)
   - Android's `mprotect` calls
6. **Round the AR and prong direction** to 5 degree steps, here and in `ammo_counter_falloff.c`, so that a small sway doesn't rebuild the mask each frame.

**What can still interrupt a pass under Vulkan:** a field missed by the frustum test or the pass guess, an image's first creation, and every view after a frame's first, which has already drawn when its uploads come.

**Done when:** barriers and pass splits per view no longer grow with the field count, and the picture matches Phase 1 on both backends.

## Phase 3: Remove the repeated CPU work

Item 1 is done, and item 5 is mostly done: a settled light skips its fade and copies, but still checks the scene. As written, the remaining items are worth little in a firefight, so they wait for a profile that asks for them.

1. **Config lookups:** done, with a small table from a name's address to its setting, so call sites and harness stubs stay as they are. Each source file has its own copy of a literal, so a setting has several addresses. With lighting off they took 5.9% of the game thread in a 48-bot Blood Gulch, more than with it on (2.7%), because a check then walks every setting it depends on.
2. **Tag names:** a per-map table from tag to lighting profile, replacing the per-frame `strcmp` calls in the emitter, Covenant, rocket and muzzle modules.
3. **Object walks:** one shared walk per view fills candidate lists for all the modules and the scene checks, replacing up to 5 walks plus 1 per field.
4. **Ray batch:** generation stamps in place of the 307 KB memset (`plasma_collision.c:278`) and the 98 KB one at its end.
5. **Stationary lights:** a light skips the fade and its copies once its field has settled (done). Still to do: check the scene less often.
6. **Moving lights:** skip the scene check (`plasma_occlusion.c:477`) until the light stops.
7. **Lighting off:** stop the frame-end dispose from running every frame (`plasma_lighting.c:156`).

8. **PS5 profile:** at 4K 120 fps the whole frame is 8.3 ms, so 12,288 rays could matter even on that CPU. Profile it here, and set its limit from that. It can use the time limit if a fixed one doesn't fit.

**Done when:** the harnesses pass, and the forced-off switches and menu behave as before.

## Phase 4: NEON

Port the SSE field rebuild (`plasma_occlusion.c:295`) to NEON, and vectorize the fade and pixel conversion. A harness compares the output with the scalar path. Both targets are ARM, and both run the scalar path today.

## Phase 5: Limit firefight lights

1. **Constant-time attachment lookup,** removing the quadratic scans (up to 128 attachments).
2. **Fewer reconnects:** the audit found only one simulation read of the light lists, a unit's `ambient_illumination`. Only a debug line uses it, and it already depends on what that machine drew, so this can't affect sync. An attached light now always takes its new position on a tick, and only leaves and joins its clusters once it is a quarter of a unit from where it joined (or a quarter of its radius, if that is less), or its radius changes. Promoted lights go the same way at each view and at the frame's end (`light_port_refresh_attachment`, 29% of reconnect time when joining a 48-player match): a light in the clusters of a larger sphere stays in them, so going back to its stock radius costs nothing. Filtering by lit or near the view wasn't needed.
3. **A limit on how many promoted lights keep their full reach,** with no per-platform default chosen yet. The nearest in view keep it. Past those, a light's added reach goes with distance, from full at 20 units to its authored radius at 60 units and beyond, so far lights still light, on fewer surfaces. They don't go back to stock. The overlay's `Full Reach: Nearest` rows (8, 6 and 4) choose it for a run. It only covers `plasma_lighting.c`'s attachments: muzzle flashes, Covenant and rocket flight lights and shield glows have their own lists.

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
- After each step, the Switch Lite is tried in a firefight, and Nokia 8 too once it has had its first run. PS5 is checked for the picture, since most of this code is shared. You run those, or tell me when I can use a device.
- Each phase can be reverted on its own, so a visual regression stays easy to isolate.
