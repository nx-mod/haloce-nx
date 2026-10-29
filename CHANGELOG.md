# Changelog

## Build 127

- Replays: you can watch through any player's eyes, not only your own; in a replay you recorded as a client, the other players' aim is smoothed between the host's updates and the top line says EYES (SMOOTHED). (thelinkin3000)
- Replays: the view through a player's eyes turns smoothly between the game's ticks and glides over the host's corrections instead of jumping. (thelinkin3000)
- Replays: the d-pad's right skips to the next 10-second mark at 50 times speed, holding a dimmed frame with SKIPPING and pausing the sound until it gets there, then plays on at normal speed. (thelinkin3000)
- Replays: a seek bar at the bottom of the screen shows where the replay is, with the time on its left and the replay's length on its right. (thelinkin3000)
- Replays: the free camera moves slowly with a slight tilt of the left stick, speeds up and stops smoothly, and the Switch's ZL and ZR raise and lower it in small steps when tapped. (thelinkin3000)
- Replays: the free camera no longer flickers into a wall when it flies against one. (thelinkin3000)
- Replays: L slows a replay down even when it is bound to push-to-talk. (thelinkin3000)
- Theater: RECORD says ON when replays are being recorded; it always said OFF. (thelinkin3000)
- Android (Vulkan) and Switch: a light's glow no longer shows or hides with another light's visibility. (thelinkin3000)

### From OpenCE

- Vehicles' headlights light again with Smooth Motion on: the Warthog's, the Ghost's and the Scorpion's went dark while their guns were idle. (Alastair)
- Parts of a level no longer flash out of view where a portal's corner lies on the view's edge. (PeterRichardsonGolf)
- Split screen: each player's window keeps its own fog. (PeterRichardsonGolf)
- The updater refuses a release with more files than it can install, instead of installing it partly. (PeterRichardsonGolf)

## Build 126

- Hotfix: Android: the touch controls' General settings scroll, so every one of them can be reached in portrait or on a short screen; the last ones were cut off, with no way to scroll to them. (thelinkin3000)
- Theater, below Multiplayer in the main menu: your replays, newest first, a row each with its map, when it was played and how long it lasted; A watches one, and RECORD, DELETE (press it twice) and BACK are under the list. Android also has EXPORT, which copies the replays out through the system's folder picker. (thelinkin3000)
- Replays: turn on RECORD in the Theater, or Record Replays in the overlay's System folder (network.record_replays), and every network game you play is recorded whole, from its first tick. A host records what everyone sees; a client records what its host sends it and its own input. Recordings under 5 seconds are not kept, and the newest 30 are (network.replays_kept, 0 keeps them all). (thelinkin3000)
- Watching a replay: Y changes the view, behind the player, through their eyes (with their arms and weapon), or a free camera. A watches the next player, X the one before. The free camera flies with the sticks and triggers, slides along walls, and follows the player until you move it; behind the player, the right stick orbits round them (R3 turns back with them) and the left stick zooms. (thelinkin3000)
- Watching a replay: the shoulder buttons play it faster (2, 4, 8 times) or slower (half, quarter, paused); the d-pad's left goes back to the last 10-second mark and its right on to the next; Back held shows the scores; B hides the text on the screen; Start pauses, and B in the pause menu leaves. A bar at the bottom shows the buttons as your controller's own: the Xbox's, the Switch's or the PS5's. (thelinkin3000)
- Watching a replay: you hear the game where the camera is, the announcer speaks to the player you are watching, and effects and particles are drawn as in the game. (thelinkin3000)
- Switch and PS5: while the Theater is open, a page on the local network (its QR code and address are in the Theater) lists the replays to download and takes replays uploaded from a phone or a computer; network.link_page = false turns it off, with Direct Link's. (thelinkin3000)
- A host can send every client every player on every tick, and every damage event, for the most detailed recordings and spectating (network.full_detail_to_clients, off by default). (thelinkin3000)
- Android: buttons at the edges of the screen, where the navigation bar is, press again: Android keeps its swipe areas there while the bars are hidden. Only a swipe that starts on no button is still left to Android. (thelinkin3000)
- Android: the cheat menu opens with three fingers or four, set in the touch controls' Options, General. (thelinkin3000)
- Android: Held buttons don't aim (Options, General; off by default): a finger that slides from a button holds it without turning the view. (thelinkin3000)
- Android: Edit buttons layout moves the Options button too, and its Save, Import and Export are in the middle of the screen. Reset puts Options back in its corner. (thelinkin3000)
- The server browser lists the games with the most players first; dedicated servers come first only when you choose to sort by them (network.browser_sort). (thelinkin3000)

### From OpenCE

- King of the Hill: MULTI-HILL (OFF, 2 to 5) puts that many hills in play at once, each with its own boundary and crown; standing on any of them scores, and with Moving Hill they all move together each minute. (s3anyboy)
- Capture the Flag: Kills Score adds a point for each kill (a team kill takes one away), with Kills per Capture setting what a capture is worth; it replaces the CTF/Slayer gametype. (s3anyboy)
- FORCE TEAM SPAWNS in Teamplay Options: in team games other than Capture the Flag, players spawn only at their own team's Capture the Flag spawn points, where the map has them. (s3anyboy)
- Co-op: a dead player always sees the cutscene while the level's scripts hold the camera, even when spectating a teammate in an AI-flown Pelican. (s3anyboy)
- A host checks a shot's line of sight from where it left when that is not the camera (a VR player's hand), as far as a hand can reach; network.fire_origins = false checks from the camera only. (Stefan Maier)
- Weapon lights, markers and scope ticks move with the picture between the game's ticks, another player's aim too, and the plasma pistol's charge meter does as well. (justinfilip)
- On a client, a vehicle nobody is driving moves smoothly from one of the host's updates to the next. (justinfilip, bnunu)
- Objects keep their lighting when more lights are on the screen than the renderer has room for. (Antimomentum)
- MATCH TIMER moved from Video Setup's Graphics to the Multiplayer HUD. (medic-code)
- The console no longer prints its messages on the screen by default (game.console_log = "important" brings back bans, cheaters and refused commands). (justinfilip)
- Settings screens: after backing out of a screen, the d-pad and the keys change the setting you return to again, not only the mouse. (bnunu)
- Dedicated servers: a setting too long for the web API to read is refused instead of being saved as empty, which could clear a pasted server password. (Alastair)

## Build 125

- Theater, below Multiplayer in the main menu: your replays, newest first, a row each with its map, when it was played and how long it lasted; A watches one, and RECORD, DELETE (press it twice) and BACK are under the list. Android also has EXPORT, which copies the replays out through the system's folder picker. (thelinkin3000)
- Replays: turn on RECORD in the Theater, or Record Replays in the overlay's System folder (network.record_replays), and every network game you play is recorded whole, from its first tick. A host records what everyone sees; a client records what its host sends it and its own input. Recordings under 5 seconds are not kept, and the newest 30 are (network.replays_kept, 0 keeps them all). (thelinkin3000)
- Watching a replay: Y changes the view, behind the player, through their eyes (with their arms and weapon), or a free camera. A watches the next player, X the one before. The free camera flies with the sticks and triggers, slides along walls, and follows the player until you move it; behind the player, the right stick orbits round them (R3 turns back with them) and the left stick zooms. (thelinkin3000)
- Watching a replay: the shoulder buttons play it faster (2, 4, 8 times) or slower (half, quarter, paused); the d-pad's left goes back to the last 10-second mark and its right on to the next; Back held shows the scores; B hides the text on the screen; Start pauses, and B in the pause menu leaves. A bar at the bottom shows the buttons as your controller's own: the Xbox's, the Switch's or the PS5's. (thelinkin3000)
- Watching a replay: you hear the game where the camera is, the announcer speaks to the player you are watching, and effects and particles are drawn as in the game. (thelinkin3000)
- Switch and PS5: while the Theater is open, a page on the local network (its QR code and address are in the Theater) lists the replays to download and takes replays uploaded from a phone or a computer; network.link_page = false turns it off, with Direct Link's. (thelinkin3000)
- A host can send every client every player on every tick, and every damage event, for the most detailed recordings and spectating (network.full_detail_to_clients, off by default). (thelinkin3000)
- Android: buttons at the edges of the screen, where the navigation bar is, press again: Android keeps its swipe areas there while the bars are hidden. Only a swipe that starts on no button is still left to Android. (thelinkin3000)
- Android: the cheat menu opens with three fingers or four, set in the touch controls' Options, General. (thelinkin3000)
- Android: Held buttons don't aim (Options, General; off by default): a finger that slides from a button holds it without turning the view. (thelinkin3000)
- Android: Edit buttons layout moves the Options button too, and its Save, Import and Export are in the middle of the screen. Reset puts Options back in its corner. (thelinkin3000)
- The server browser lists the games with the most players first; dedicated servers come first only when you choose to sort by them (network.browser_sort). (thelinkin3000)

### From OpenCE

- King of the Hill: MULTI-HILL (OFF, 2 to 5) puts that many hills in play at once, each with its own boundary and crown; standing on any of them scores, and with Moving Hill they all move together each minute. (s3anyboy)
- Capture the Flag: Kills Score adds a point for each kill (a team kill takes one away), with Kills per Capture setting what a capture is worth; it replaces the CTF/Slayer gametype. (s3anyboy)
- FORCE TEAM SPAWNS in Teamplay Options: in team games other than Capture the Flag, players spawn only at their own team's Capture the Flag spawn points, where the map has them. (s3anyboy)
- Co-op: a dead player always sees the cutscene while the level's scripts hold the camera, even when spectating a teammate in an AI-flown Pelican. (s3anyboy)
- A host checks a shot's line of sight from where it left when that is not the camera (a VR player's hand), as far as a hand can reach; network.fire_origins = false checks from the camera only. (Stefan Maier)
- Weapon lights, markers and scope ticks move with the picture between the game's ticks, another player's aim too, and the plasma pistol's charge meter does as well. (justinfilip)
- On a client, a vehicle nobody is driving moves smoothly from one of the host's updates to the next. (justinfilip, bnunu)
- Objects keep their lighting when more lights are on the screen than the renderer has room for. (Antimomentum)
- MATCH TIMER moved from Video Setup's Graphics to the Multiplayer HUD. (medic-code)
- The console no longer prints its messages on the screen by default (game.console_log = "important" brings back bans, cheaters and refused commands). (justinfilip)
- Settings screens: after backing out of a screen, the d-pad and the keys change the setting you return to again, not only the mouse. (bnunu)
- Dedicated servers: a setting too long for the web API to read is refused instead of being saved as empty, which could clear a pasted server password. (Alastair)

## Build 124

- The OpenCE overlay, on the Switch, the PS5 and Android: hold L and R (L1 and R1) and press the d-pad's down, or on Android tap the screen with three fingers, for a panel at the left of the screen with cheats, bots, the game's speed, the HUD, the camera and the renderer's switches, in the menus and in a game. While it is open the game gets none of your input, but it is not paused; game.overlay_menu = false stops the buttons from opening it. Its first entries are hardran3's OpenCE-Ultrahand and HaloDrones packages'. (thelinkin3000)
- The overlay's Bots folder adds and drops bots in a game in progress (on either team), and sets their difficulty, whether they take vehicles and their routes, at once. (thelinkin3000)
- Entries of your own in the overlay: a .toml file each (a command, a toggle or a choice) in the overlay folder beside maps/, its folders the overlay's folders, one of a built-in folder's name adding to it; a file that is not a whole entry shows in red. The README's "The OpenCE overlay" says how. (thelinkin3000)
- No cheats in multiplayer unless you are the host: in a game someone else hosts, the overlay refuses its cheats, as the console does. (thelinkin3000)
- Switch and PS5: display.xbox_buttons = true draws the Xbox controller's buttons in the menus, the game's prompts and the text instead of the console's own (the overlay's System folder has it too; it applies at the next restart). (thelinkin3000)
- Switch: the overlay replaces the Ultrahand overlay's command file: the game no longer reads /switch/.packages/halo/overlay/command.txt nor writes its result and flags there, and game.overlay_commands is gone. (thelinkin3000)

## Build 123

- Main menu: the X button (Y on the Switch, Square on the PS5), or a tap on NEWS, opens the news: this release's notes in a window that scrolls. (thelinkin3000)
- PS5: Join Game > Direct Link shows a QR code: scan it with a phone (or open the address under it) and paste an invite link there, and the PS5 joins its game. The page is served on the local network only while Direct Link is open (network.link_page). (thelinkin3000)
- PS5: Server Setup's invite row opens the game's invite link as a QR code, for a phone to read; B closes it. (thelinkin3000)
- Bots drive and ride vehicles: a bot whose goal is far off takes an empty Warthog, Ghost or Scorpion, rides as a Warthog's gunner when a teammate drives, backs off when stuck, and mans turrets near an enemy (bots.vehicles). (thelinkin3000, from BirchWoodGod's halo-ce-vita)
- Bots go for health packs when hurt, and for an overshield or camouflage nearby. (thelinkin3000, from BirchWoodGod's halo-ce-vita)
- Bots hold vantage points: a bot with a sniper rifle or a rocket launcher (or a pistol in Slayer) holds a place overlooking the map a while, and leaves it when an enemy comes near, it runs low on ammunition, it is hurt, or its flag needs it. (thelinkin3000, from BirchWoodGod's halo-ce-vita)
- Bots find their way better: through teleporters when a goal cannot be walked to, through them as shortcuts when they save a long walk, and up ladders. (thelinkin3000, from BirchWoodGod's halo-ce-vita)
- Bots can spread over a map's side lanes instead of all going down the middle (bots.routes: direct, lanes or mixed). (thelinkin3000, from BirchWoodGod's halo-ce-vita)
- Switch and PS5: the server browser lists the master servers' dedicated servers, as the other platforms do. (thelinkin3000)

### From OpenCE

- Network version 39: this build plays with OpenCE 39 and later, not with Build 122 or older.
- The server browser has a DEDICATED tab for the master servers' dedicated servers, and opens on ALL GAMES, dedicated servers first.
- The server browser asks the official master server, master.opencommunityedition.com, when masters.txt names none.
- A server that refuses a player's name says so: "The host refused: your player name".
- Fishsticks and Recon controller layouts.
- The host refuses a reported hit with no line of sight to the target.
- Gun Game: the plasma rifle and plasma pistol can fire.
- A client keeps its inventory until its weapons have arrived, so it no longer spawns empty-handed.

## Build 122

- Switch: Join Game > Direct Link shows a QR code: scan it with a phone (or open the address under it) and paste an invite link there, and the Switch joins its game. The page is served on the local network only while Direct Link is open (network.link_page). (thelinkin3000)
- Switch: Server Setup's invite row opens the game's invite link as a QR code, for a phone to read; B closes it. (thelinkin3000)
- Bots play Capture the Flag, by Quake III Arena's bots' rules: defenders and attackers, carriers run home, the team's flag is chased and returned. (thelinkin3000)
- Bots play King of the Hill, fixed or moving: they take the hill and hold it, and fight whoever stands on it first. (thelinkin3000)
- Bots in Infection hunt as zombies and hold together as humans; in SWAT they aim for the head. (thelinkin3000)
- Bots aim at the head with the pistol and the sniper rifle once shields are down, and at the feet with rockets and the fuel rod: legendary always, easy seldom. (thelinkin3000)
- Bots can join private internet games (hosted, not listed in the server browser), as well as split screen and system link ones. (thelinkin3000)
- Switch: on the carnage report, A picks the next game and B quits, as in the menus. (thelinkin3000)

## Build 121

- Bots can join and leave a Slayer game in progress, one at a time, from the console or the Switch's overlay: add_bot and drop_bot in free for all, add_bot_red, add_bot_blue, drop_bot_red and drop_bot_blue in team games. Only the host adds and drops them. (thelinkin3000)
- Bots play in system link games too, not only split screen: the host plays them and the other machines see them as any player (bots.count fills the lobby, and the commands above work in game). Not in games hosted for internet play. (thelinkin3000)
- Switch: pressing + and − together (or Home and +) no longer quits the game at once, mid-match. (thelinkin3000)

## Build 120

- Switch: in the menus, the button marked A (on the right) chooses and B (at the bottom) goes back, as in the console's own menus; in a game the buttons are as before. controls.nintendo_menu_buttons = false in config.toml brings back the Xbox layout in the menus. (thelinkin3000)
- Switch and PS5: the controller buttons on screen are the console's own, from Kenney's Input Prompts: the menus' buttons, the "Press (A)" icons in text and the game's prompts, with the shoulders, triggers and sticks. (thelinkin3000)
- Switch: the Ultrahand overlay (hardran3's OpenCE-Ultrahand package) works only with the package installed, and the main menu says "Ultrahand Overlay present and enabled". Its command file is now /switch/.packages/halo/overlay/command.txt, and the game keeps an empty file in that folder's flags for each true/false command that is on (cheat_infinite_ammo.flag, display.vsync.flag), so the overlay can show its toggles' state. (thelinkin3000)

## Build 119

- Switch: with a mod of high-resolution textures (Uncompressed Textures, say), textures the game lets go of are kept in the console's memory outside the game's own, 512 MB of them, and read back from there instead of the SD card: most of the hitches in busy matches with the mod. Blood Gulch and Battle Creek with 63 bots went from 53.9 and 48.6 to 59.3 and 53.8 frames a second. (thelinkin3000)
- Switch: Custom Edition maps' textures are kept there too. (thelinkin3000)
- Switch: Custom Edition maps load faster: Lumoria A from 79 to 50 seconds. (thelinkin3000)
- Mods' files stay open between reads and are no longer checked for changes at each read, so their textures load without hitching. (thelinkin3000)
- The sound mixer lets the game in between voices, rather than holding it off for a whole mix, which removed stutters in busy fights: Chill Out stays at 60 frames a second on the Switch. (thelinkin3000)
- Switch: no more "Restart to get a 512 MB memory window" message at startup: the texture memory above makes up for the smaller window. (thelinkin3000)
- Switch: an Ultrahand overlay can control the game: a console command or a "config <setting> <value>" line written to switch/halo/overlay/command.txt is run within a second (game.overlay_commands, on by default). (thelinkin3000)

### From OpenCE

- Network version 38: this build plays online only with other builds of this version or newer. (MrBruh)
- The server browser names OpenCE gametypes (SWAT, HC CTF), for LAN and internet games. (MrBruh)
- A host can leave the shield effects to each player's own setting (SHIELD EFFECTS: PLAYERS' OWN); they stay the host's by default. (MrBruh)

## Build 118

- The game no longer stays on a black screen at startup with interpolation turned off (Video Setup, or display.interpolation in config.toml): its loading screen waited for a vertical blank that had not started yet. Seen on the Switch; the fix is in every renderer. (thelinkin3000)

## Build 117

- Switch: 64-player matches run much faster: Damnation from 44 to about 60 frames a second, Chill Out from 38 to about 59, Blood Gulch from 51 to 60, with far fewer stutters (Chill Out's frames over 33 ms from 90 to 5 in a 45-second fight). (thelinkin3000)
- Android: busy matches are faster too: Chill Out with 64 players from 92 to 109 frames a second on a Galaxy Z Fold. (thelinkin3000)
- Particles that are not attached to anything (sparks, casings, debris) move 30 times a second, as on the Xbox, drawn smoothly between their steps, and those off the screen less often: their collision tests were the largest cost of a big firefight on every platform. debug.original_particles moves them every frame. (thelinkin3000)
- At most 4096 particles are alive at once (2048 on the Switch), the oldest making way for the new ones, so fresh impacts always have their sparks; the Xbox's limit was 1024. display.particle_limit sets it, 0 for the full 8192. (thelinkin3000)
- The 8 biggest dynamic lights on the screen (muzzle flashes, plasma, explosions) light the level, and the 2 biggest add their shine to it, the first-person weapon's first; every light still lights the players and objects near it. display.dynamic_light_limit and display.specular_light_limit set them, 0 for every light as on the Xbox. (thelinkin3000)
- Switch: online and local games ask the console's network service far less often: the game no longer waits on it once or twice a frame to learn there is nothing to read. (thelinkin3000)
- The game no longer waits for the sound mixer every frame, nor for long when a sound starts or stops, which made busy fights hitch. (thelinkin3000)
- A bullet's mark on a wall no longer costs a check of every mark in the game, which only debug builds now make. (thelinkin3000)
- Player names over heads test their line of sight once a tick, and only for players in range and on the screen. (thelinkin3000)
- Switch: Custom Edition maps no longer fail to load now and then at startup ("Custom Edition's tag cache range is taken"). (thelinkin3000)

## Build 116

- Android: anti-aliasing (MSAA) costs much less on phones whose driver can keep the samples in the GPU's own tile memory (Adreno, Mali): with MSAA 4x, busy multiplayer maps run about 15% faster (Damnation 85 → 97 frames a second, Chill Out 64 → 73, with 64 players on a Galaxy Z Fold). debug.original_multisample turns it off. (thelinkin3000)
- Android and PS5: lens flares and the sun's glow no longer stall the GPU under Vulkan: the opening of The Pillar of Autumn, which ran at about 10 frames a second on a Galaxy Z Fold, now runs at 120. (thelinkin3000)
- Android: the frame breaks off less often under Vulkan, each break costing a phone's GPU about a millisecond: the sun's glow and the water's ripples are made at the end of the frame, screen clears come free with the next drawing, and a shadow map is made readable as soon as it is drawn. (thelinkin3000)
- Android and PS5: the GPU savings of Build 114 (shadows and lights drawn only where they change the screen) now fully take effect; a missing setting had left part of them off. (thelinkin3000)
- Android: the high-res HUD is prepared while a map loads, as on the Switch, instead of during play. (thelinkin3000)
- The steam from the cryo tube at the start of the campaign, and other blurs drawn on a small copy of the screen, are smooth again instead of blocky. (thelinkin3000)
- Up to 63 bots in a local game. (thelinkin3000)
- A built-in benchmark on every platform: debug.benchmark = "standard" in config.toml plays five campaign levels and four multiplayer maps with 63 bots, and writes the frame rates to benchmark.txt. (thelinkin3000)
- Android: debug.profile_run = true records a profile of each run (CPU, GPU time, logs, benchmark) into its own folder in Download/halo_profiles, ready to send with a report. (thelinkin3000)

### From OpenCE

- Network version 36: this build plays online only with other builds of this version or newer. (bnunu)
- New gametypes: SWAT, Infection, Gun Game and Hardcore, in Select Gametype's OPENCE bank, and an OPENCE OPTIONS screen in the gametype editor. (MrBruh)
- In network games, the shield effects are the host's choice. (bnunu)
- Co-op: the host can make the players' AI allies immune to their fire. (Guven)
- Co-op: clients break, and see broken, the map's scenery shields as the host does, and their tripmine hits and explosions count on the host. (Guven)
- Co-op: a client taken by a teleport lands with their feet on the floor. (Jonas Volman)
- Custom Edition HUDs: replacements, ammo counters, meters and prompts are drawn right, with or without the high-res HUD (OpenGL renderers). (chestahh)
- Android, OpenGL ES: no more stretched or missing geometry on affected phones, and Custom Edition maps keep their textures. (chestahh)
- Android, OpenGL ES: phones without DXT1 texture support get those textures as ASTC. (Jonas Volman)
- The PC vehicle set shows its name and help instead of <missing string>. (bnunu)

## Build 115

- Switch: a console whose clock is wrong is told so at the start and in the server browser, with how far off it is: the server browser shows no internet games while the clock is more than an hour off, which homebrew consoles that never reach Nintendo often are. (thelinkin3000)
- The server browser says how many games it hears but does not show, and why: on a newer or test version, or with a wrong clock. (thelinkin3000)
- The maps and Custom Edition maps can be read from other folders: game.maps.path and game.custom_maps.path in config.toml. On the Switch, a maps folder named there without the maps stops the game at the start with an error. (thelinkin3000)
- The map lists open much faster with many Custom Edition maps, and opened again, they only look at the maps that are new or changed. (thelinkin3000)
- Switch: a start that cleans up a large shader cache takes a fraction of a second instead of half a minute. (thelinkin3000)

## Build 114

- Switch: shaders compile on two threads, about three times as fast: a new console compiles the shared shaders in the background in a fraction of the time, and the menus stay at 60 frames a second meanwhile. (thelinkin3000)
- Switch: a new console's first start waits about 3 seconds, not 30, before the menus. (thelinkin3000)
- Switch: a level's first 10 seconds wait for its shaders instead of drawing the world a piece at a time; the first frames may hitch briefly, but every frame is whole. (thelinkin3000)
- Switch: a faster start: the console's own shader list no longer holds the shaders the shared list has, so fewer are loaded before the menus. (thelinkin3000)
- Switch: new shared shader keys, the campaign's and the stock multiplayer maps', so a new console compiles only what those maps need from its first start. (puginatux)
- Android and PS5: shadows, dynamic lights, the sniper and pistol zoom's blur and cutscene blurs cost the GPU much less under Vulkan, as they already did on the Switch: shadows and lights are drawn only where they change the screen, and the blurs on a small copy of it. (thelinkin3000)
- PS5: clicking the touch pad is Back, which the pad had none of (the scores in a match, Back in the menus). (thelinkin3000)

### From OpenCE

- Co-op: the Pillar of Autumn no longer softlocks. (eltanschauung)
- Co-op: the gravity lift on The Silent Cartographer takes every player, not only the first two. (eltanschauung)
- A little less work per frame: the high-res HUD setting is read once per change instead of at every texture. (bnunu)

## Build 113

- Switch: hosting an internet game works: the console ran out of room for network connections and refused its own player's. (thelinkin3000)
- Switch: UPnP port forwarding for internet play, as on the other platforms: a router with UPnP on forwards the console's port, so players behind strict networks can reach a game it hosts. (thelinkin3000)
- Switch: hosts up to 48 players. A LAN game held 48 players at a steady 60 frames a second (it took 22 before, and stuttered with many players): the console's network work is now done in batches and off the game's thread. Server Setup's MAX PLAYERS goes up to 48 on the Switch. (thelinkin3000)
- The gametype list in Create Game and in Settings > Gametypes no longer reads the gametypes' names from the card every frame, which held it under 20 frames a second on the Switch. (thelinkin3000)

## Build 112

- Switch: creating a LAN game after opening the server browser works again: leaving the browser now closes its connections to the internet play servers, which had used up the console's room for network connections. (thelinkin3000)
- Switch: the Mods menu keeps which mods are on and off after the game restarts. Every save after the first had failed, because the console would not replace the old file (profile saves were affected the same way). (thelinkin3000)
- The gametype list in Create Game and in Settings > Gametypes no longer reads the chosen gametype from the card every frame, which kept the Switch busy for as long as the screen was open. (thelinkin3000)

## Build 111

- Switch and PS5: the main menu has no QUIT: the console's own Home or PS button leaves the game. (thelinkin3000)
- PS5: each build has its own version number on the console, so stores and the console can tell that a newer build is an update. (thelinkin3000)

## Build 110

- Switch and Android: Halo's loading screen draws, the light sweeping over the Xbox's picture with its glowing trail; maps extracted by earlier builds get the picture from the disc image at the next start (on every platform). (thelinkin3000)
- Switch: the first seconds after starting no longer freeze or cut the music, and the menus appear whole once their shaders are ready, behind the loading screen. (thelinkin3000)
- Switch: when it looks for an update, the Switch also downloads the shaders of the campaign and of every multiplayer map (the shared shader keys), so a console compiles them in the background from its first start instead of as each map is first played. (thelinkin3000)
- Switch: a map's load waits for its shaders under the loading screen, ten seconds at most, so a level's first moments are drawn whole; the loading screen's light now sweeps across the whole wait. Going back to the menus after a level no longer waits four seconds. (thelinkin3000)
- Switch, Android and PS5: maps load sooner: the map is read from the card while it is unpacked (a campaign level about two seconds sooner on the Switch). (thelinkin3000)
- Android and PS5: no more objects missing for the first frames of a map or a new effect: shaders compile on all the cores but two, and a draw whose shader is not ready compiles it itself, for up to a frame's time each frame. (thelinkin3000)

## Build 109

- Switch: fixes a freeze in The Library's opening cutscene, and pauses of a few seconds while loading: a part of a frame the game waited for was never sent to the GPU. (thelinkin3000)
- Switch: no more drops to 25 frames a second where The Library's Sentinels fire their beams behind the crystal: the game waited for the GPU in the middle of each frame. (thelinkin3000)
- Switch: steady 60 frames a second. Frames were started in bursts and then held back, which dropped frames even with the CPU and GPU at 40%; each frame now starts one refresh after the last. (thelinkin3000)
- Switch: no more hitch the first time a high-res HUD element shows (the damage arrows, the meters, the scopes): the console decodes them as the map loads. (thelinkin3000)
- Switch: Dynamic Resolution in Video Setup drops one step while the GPU falls behind; Resolution Limit is 720 by default; fewer large particles at once and a cheaper zoom keep heavy scenes at 60. (thelinkin3000)
- Android: Maximum Resolution in Video Setup, the screen's own by default, down to 480; Vulkan draws cost less; the restart notice in Video Setup no longer covers FOV and Viewmodels. (thelinkin3000)
- Switch and Android: the high-res button prompts draw. (thelinkin3000)

### From OpenCE

- Network version 32: plays only with builds of the same version.
- OpenCE is now under the AGPL 3.0 or later. (MrBruh)
- Bots in local Slayer games. (BirchWoodGod)
- A latency meter for network games. (BirchWoodGod)
- HDR. (hkmodd)
- Model Detail in the Graphics settings. (Paul Hontz)
- Anisotropic filtering. (Paul Hontz)
- 5.1 surround on desktop. (frankyfife)
- A third-person view for Warthog and Scorpion passengers. (biu788)
- A match clock on the multiplayer HUD. (medic-code)
- Video Setup's Extra Effects screen. (jcompto95-tech)
- An overshield effect. (jcompto95-tech)
- Extra weapons, pickups and vehicles for co-op hosts. (eltanschauung)
- Host aim assist options. (eltanschauung)
- Friendly fire chosen by each co-op player. (Jonas Volman)
- The Bumper Jumper button layout. (Ari Wilson)
- Halo's loading screen. (FractumSeraph)
- The PC menus in a Custom Edition's language. (hkmodd)
- Culling of the level's hidden triangles. (Mootjuh)

## Build 108

- Switch: fixes a freeze in The Library's opening cutscene, and pauses of a few seconds while loading: a part of a frame the game waited for was never sent to the GPU. (thelinkin3000)
- Switch: steady 60 frames a second. Frames were started in bursts and then held back, which dropped frames even with the CPU and GPU at 40%; each frame now starts one refresh after the last. (thelinkin3000)
- Switch: no more hitch the first time a high-res HUD element shows (the damage arrows, the meters, the scopes): the console decodes them as the map loads. (thelinkin3000)
- Switch: Dynamic Resolution in Video Setup drops one step while the GPU falls behind; Resolution Limit is 720 by default; fewer large particles at once and a cheaper zoom keep heavy scenes at 60. (thelinkin3000)
- Android: Maximum Resolution in Video Setup, the screen's own by default, down to 480; Vulkan draws cost less; the restart notice in Video Setup no longer covers FOV and Viewmodels. (thelinkin3000)
- Switch and Android: the high-res button prompts draw. (thelinkin3000)

### From OpenCE

- Network version 32: plays only with builds of the same version.
- OpenCE is now under the AGPL 3.0 or later. (MrBruh)
- Bots in local Slayer games. (BirchWoodGod)
- A latency meter for network games. (BirchWoodGod)
- HDR. (hkmodd)
- Model Detail in the Graphics settings. (Paul Hontz)
- Anisotropic filtering. (Paul Hontz)
- 5.1 surround on desktop. (frankyfife)
- A third-person view for Warthog and Scorpion passengers. (biu788)
- A match clock on the multiplayer HUD. (medic-code)
- Video Setup's Extra Effects screen. (jcompto95-tech)
- An overshield effect. (jcompto95-tech)
- Extra weapons, pickups and vehicles for co-op hosts. (eltanschauung)
- Host aim assist options. (eltanschauung)
- Friendly fire chosen by each co-op player. (Jonas Volman)
- The Bumper Jumper button layout. (Ari Wilson)
- Halo's loading screen. (FractumSeraph)
- The PC menus in a Custom Edition's language. (hkmodd)
- Culling of the level's hidden triangles. (Mootjuh)

## Build 107

- Switch: steady 60 frames a second. Frames were started in bursts and then held back, which dropped frames even with the CPU and GPU at 40%; each frame now starts one refresh after the last. (thelinkin3000)
- Switch: no more hitch the first time a high-res HUD element shows (the damage arrows, the meters, the scopes): the console decodes them as the map loads. (thelinkin3000)
- Switch: Dynamic Resolution in Video Setup drops one step while the GPU falls behind; Resolution Limit is 720 by default; fewer large particles at once and a cheaper zoom keep heavy scenes at 60. (thelinkin3000)
- Android: Maximum Resolution in Video Setup, the screen's own by default, down to 480; Vulkan draws cost less; the restart notice in Video Setup no longer covers FOV and Viewmodels. (thelinkin3000)
- Switch and Android: the high-res button prompts draw. (thelinkin3000)

### From OpenCE

- Network version 32: plays only with builds of the same version.
- OpenCE is now under the AGPL 3.0 or later. (MrBruh)
- Bots in local Slayer games. (BirchWoodGod)
- A latency meter for network games. (BirchWoodGod)
- HDR. (hkmodd)
- Model Detail in the Graphics settings. (Paul Hontz)
- Anisotropic filtering. (Paul Hontz)
- 5.1 surround on desktop. (frankyfife)
- A third-person view for Warthog and Scorpion passengers. (biu788)
- A match clock on the multiplayer HUD. (medic-code)
- Video Setup's Extra Effects screen. (jcompto95-tech)
- An overshield effect. (jcompto95-tech)
- Extra weapons, pickups and vehicles for co-op hosts. (eltanschauung)
- Host aim assist options. (eltanschauung)
- Friendly fire chosen by each co-op player. (Jonas Volman)
- The Bumper Jumper button layout. (Ari Wilson)
- Halo's loading screen. (FractumSeraph)
- The PC menus in a Custom Edition's language. (hkmodd)
- Culling of the level's hidden triangles. (Mootjuh)

## Build 106

- PS5: fixes a bug with the updater. It installed a new release's files
  without the permission to run them, so the game did not start again
  after an update; it now installs them as FTP does. Build 105's updater
  still has the bug: on Build 105, install this build by hand, once: over
  FTP, delete the PPSA99500 folder in /data/homebrew, then copy this
  build's in its place (copying over the old files keeps the ones an
  update wrote unable to run). Your saves and settings, in /data/halo,
  stay. Updates after it install themselves. (thelinkin3000)
- The README opens with a trailer: the game on the Nintendo Switch
  (puginatux's video).

## Build 105

- PS5: the game as a homebrew title for a jailbroken PS5 (system software
  13.60 or lower, with ShadowMountPlus and an FTP server):
  halo-ps5-release.zip, whose PPSA99500 folder goes in /data/homebrew and
  the disc image in /data/halo. Up to native 4K at 120 frames a second on
  a TV that takes it, split screen with a second pad, aiming with the
  DualSense's gyroscope, voice chat through the pad's microphone, and
  system link and internet play with every other build. It draws with the
  Android port's Vulkan renderer on RADV, and updates itself from the
  releases while you play. The first visit to each place on a new install
  can stutter while its shaders are made. (thelinkin3000)

## Build 104

- Switch and Android (Vulkan): Custom Edition maps and mods draw their
  armor, visors and HUD meters as on PC. Halo PC keeps a multipurpose
  map's masks in other channels than the Xbox, and these renderers read
  them in the Xbox's order: a modded cyborg's visor did not reflect, and
  team colors and glow came out wrong. (thelinkin3000)
- From OpenCE: a text chat for network games: T to everyone and Y to the
  team on a keyboard, D-pad Up opens an on-screen keyboard for a
  controller, Tab sends to everyone, the team or the vehicle; the host can
  turn it off and mute players, players can mute one another from the
  scoreboard, and Network Setup's new MULTIPLAYER HUD page holds its
  settings (Jonas Volman, MrBruh). Streamer mode, on Network Setup's
  STREAMER MODE page, hides slurs, swearing and insults in other players'
  names and text, and in maps and mods (MrBruh). A host shares maps at up
  to 20 MB/s, and a player helping it at up to 10 MB/s (MrBruh).

## Build 103

- A level no longer shows a frame of itself before its intro's fade and
  cinematic: with interpolation, no frame of a map is drawn before its
  first tick. (thelinkin3000)
- Switch: a level started in the first seconds after launch waits under
  the loading screen for its shaders, as later ones do, instead of starting
  with draws missing for a second or two (the intro's fade and letterbox
  bars among them). (thelinkin3000)
- From OpenCE: mods can replace a map's models whose nodes, markers and
  regions match (MrBruh); Audio Setup's new SOUND PROCESSING screen holds
  Reverb and the resampler, SINC (the default) or LINEAR, which is brighter
  (audio.resampler), and turning reverb off no longer muffles every sound
  (WhiteBulL, MrBruh).

## Build 102

- Switch: the game's GPU commands are handed to the GPU on a thread of
  their own, so the game goes on with the next frame meanwhile; a full
  40-player match ran without a fault. debug.render_thread in config.toml
  turns it off. (thelinkin3000)
- Switch: cutscene blurs (Pillar of Autumn, Two Betrayals) are drawn on a
  small copy of the screen, as the zoom's are: from 18-39 ms of the GPU's
  frame to about 2.6 ms. (thelinkin3000)
- Switch: dynamic lights (the flashlight, explosions, plasma) are drawn
  only within their reach, and object shadows only within their box on
  the screen: shadows cost about half as much, lights about a tenth less.
  (thelinkin3000)
- Switch: transparent draws (particles, decals, glass) skip the pixels
  their blend leaves as they were. (thelinkin3000)
- Object shadows are limited to the largest 16 on screen
  (display.shadow_limit), the others fading out, for crowded games.
  (thelinkin3000)
- Crowded games spend less time on bodies settling onto the ground: their
  collision tests pass by objects they cannot hit before looking them up.
  (thelinkin3000)
- Switch: draws given a vertex at a time send only what each vertex sets:
  a decal's vertices are a fifth of the data they were. (thelinkin3000)
- Switch: 900p is a choice of Video Setup's resolution. (thelinkin3000)
- New Game starts the level from its beginning, not from the profile's
  last checkpoint. (thelinkin3000)
- Settings > Gameplay: how players' and the AI's bodies fall (the death
  animations, or a ragdoll), and the main menu's background keeps playing
  on the settings screens. (WataNoge)
- Settings > Gameplay > Terminal output: the game's messages at the bottom
  left are shown in the main menu only by default, or never, or always
  (game.terminal_output). (thelinkin3000)
- From OpenCE: client-side mods, with a Mods menu, load order and checks
  for mods that cannot be played together (MrBruh); the complete
  high-resolution Xbox HUD, with Classic, Gems and Menu Gems button
  styles, the pause menu's artwork and timer digits restored, and HUD and
  text settings applied without reloading the map (chestahh); a vehicle
  nobody drives follows the host's positions, and screen fog stays on the
  canopy at a wider field of view (justinfilip); voice chat drops damaged
  packets (Paul Hontz).

Thanks to puginatux for testing the performance work so thoroughly: the
profiles of cutscenes, flashlights, snow, explosions and crowded matches
they recorded showed where each of these improvements was needed, and
proved each one.

Network version 29

## Build 101

- Switch: the maps the game plays are kept unpacked in memory instead of
  in cache files on the SD card: maps load in a second or two, the game no
  longer writes some 730 MB of cache files to the card, or a map to it each
  time a cache file takes a new one, and the old cache files are deleted.
  (thelinkin3000)
- Switch: zooming in keeps the middle of the view sharp, where you aim; the
  blur stays at the edges. (thelinkin3000)
- Switch: gyroscope aiming turns 50% further at every sensitivity.
  (thelinkin3000)

Network version 29

## Build 100

- Switch: zooming in costs about 3.7 ms less of the GPU's frame at 720p:
  the zoom's blur is drawn on a small copy of the screen and blended over
  it once, with the same look. debug.original_zoom in config.toml draws it
  as before. (thelinkin3000)

Network version 29

## Build 99

- Switch: crowded games no longer freeze the picture with a GPU fault.
  deko3d wrote a few words past the end of a piece of command memory, and
  the next command overwrote them before the GPU read them. (thelinkin3000)
- Switch: the level is drawn about a quarter faster: the pixel shaders
  leave out the clamps their values cannot need, with the same picture.
  (thelinkin3000)
- Switch: shadows cast on the level cost about half as much on the GPU,
  each one shaded only over its own map. (thelinkin3000)
- Switch: drawing into a render target waits for the GPU only when that
  target was sampled, not after every shadow. (thelinkin3000)
- Switch: the shader cache drops the shaders of earlier versions, which
  could fill it and leave the game drawing nothing. (thelinkin3000)
- Switch: GPU diagnostics for bug reports, all off unless set in
  config.toml's [debug]: gpu_trace (the last commands before a GPU fault),
  gpu_profile and gpu_capture (where the GPU's time goes). (thelinkin3000)

Network version 29

## Build 97

- Custom Edition maps keep their own names; a game names its map's
  version, which a client finds its copy by, and maps an earlier build
  renamed get their names back. A download beside another version of its
  name is listed as "(2)". (MrBruh)
- Map sharing and Custom Edition map loading are hardened against damaged
  or hostile files. (MrBruh)
- custom_maps subfolders work with the above: a client in a network game
  finds its copy of the game's map in a subfolder too. (thelinkin3000)

Network version 29

## Build 96

- Switch: the program's main thread sleeps instead of spinning on the
  game's core, which kept the CPU busy for nothing. (nx-mod)
- Network games catch up at most 4 ticks a frame, so one slow frame no
  longer snowballs into frames of several seconds. (nx-mod)

Network version 28

## Build 95

- Switch: a GPU fault no longer ends the game; it is logged and the
  picture stops. (thelinkin3000)
- Android and Switch: text that starts with an underscore keeps it, so
  shared Custom Edition maps are named once, not again at every start.
  (thelinkin3000)
- A config.toml line that cannot be read is set aside, instead of every
  setting going back to its default. (thelinkin3000)
- Switch: CPU clock in Video Setup, Performance: 1.7 GHz (default), 1 GHz
  or stock. (thelinkin3000)
- Switch: Resolution Limit in Video Setup: 480p (default), 720p or 1080p,
  shown at 720p in handheld and 1080p docked. (thelinkin3000)

Network version 28

## Build 94

- Bodies in multiplayer fall as ragdolls: game.ragdoll "woody" (default),
  "halo2" or "none". A host keeps stock body physics, so it plays with
  builds without ragdolls. (thelinkin3000)
- Gyro aim on the Switch's controllers, in a profile's Gamepads, Gyro Aim.
  (WataNoge)
- Gyro aim ONLY ON ZOOM, and the phone's gyroscope set in Gyro Aim on
  Android. (thelinkin3000)
- custom_maps subfolders, listed under singleplayer or multiplayer.
  (WataNoge)
- Split screen on the Switch: each view's map, clears and pause menu drawn
  correctly. (WataNoge, thelinkin3000)
- A lobby with everyone on one machine starts, and START NOW works.
  (WataNoge)
- Switch settings are saved to the SD card when the game exits. (WataNoge)
- Fixes to profile editing in the pause menu and Back after a game.
  (WataNoge)

Network version 28
