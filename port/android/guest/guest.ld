/*
GUEST.LD

Layout of the guest image (tools/android_build.py): linked to run at
HALO_GUEST_IMAGE_BASE (port/android/include/halo_android_abi.h), which the
link gives as __guest_image_base (tools/guest_arch.py, image_base), with the
image header first. The host copies the loadable segments to these
addresses and makes the code read-only.

The constructors are the converted Mach-O ones (.guest_init) or, for a guest
that clang compiles to ELF itself, .init_array's, in their priority order:
both 4-byte pointers, which __guest_start calls. Which of them a guest has
is its architecture's guest_init_array.ld, in the folder the link names
(-L, tools/android_build.py: ld/elf or ld/macho). A guest compiled to Mach-O
takes no .init_array: what it would come from is the 64-bit runtime library
(compiler-rt's AArch64 builtins), whose 8-byte entries __guest_start would
read as two, and it is discarded. tools/guest_image_check.py checks that
every entry is the address of code. Nothing is ever finalized
(the game ends the process), and there is no thread-local storage: the
guest's thread pointer belongs to the host (-femulated-tls).
*/

ENTRY(__guest_header)

SECTIONS
{
	. = __guest_image_base;
	.guest_header : { KEEP(*(.guest_header)) }
	.text : { *(.text .text.*) }
	.rodata : { *(.rodata .rodata.*) }
	. = ALIGN(0x10000);
	/* the variables of the game's simulation and of its netcode's client
	that are not in the game state, together, so that a replay's keyframes
	copy them whole with it (port/linux/game/network_replay_playback.c): the
	simulation's folders, the netcode's client, and the port's modules of
	either. Listed twice, the same: here for their data, below for their
	zeroed variables. Ahead of .data and .bss, which take the rest */
	.replay_data : {
		__replay_data_start = .;
		*/source/game/*.o(.data .data.*)
		*/source/objects/*.o(.data .data.*)
		*/source/units/*.o(.data .data.*)
		*/source/items/*.o(.data .data.*)
		*/source/effects/*.o(.data .data.*)
		*/source/physics/*.o(.data .data.*)
		*/source/devices/*.o(.data .data.*)
		*/source/networking/network_client_manager.o(.data .data.*)
		*/source/networking/network_client_message_handler.o(.data .data.*)
		*/source/networking/network_game_globals.o(.data .data.*)
		*/source/networking/network_game_manager.o(.data .data.*)
		*/port/linux/game/network_distributed.o(.data .data.*)
		*/port/linux/game/network_objects.o(.data .data.*)
		*/port/linux/game/network_actors.o(.data .data.*)
		*/port/linux/game/network_damage.o(.data .data.*)
		*/port/linux/game/network_shield_effects.o(.data .data.*)
		*/port/linux/game/network_aim_assist.o(.data .data.*)
		*/port/linux/game/biped_ragdoll.o(.data .data.*)
		*/port/linux/game/infection.o(.data .data.*)
		*/port/linux/game/gun_game.o(.data .data.*)
		*/port/linux/game/game_modes.o(.data .data.*)
		__replay_data_end = .;
	}
	.data : { *(.data .data.*) }
	.guest_init : {
		__guest_init_start = .;
		KEEP(*(.guest_init))
		INCLUDE guest_init_array.ld
		__guest_init_end = .;
	}
	.replay_bss : {
		__replay_bss_start = .;
		*/source/game/*.o(.bss .bss.* COMMON)
		*/source/objects/*.o(.bss .bss.* COMMON)
		*/source/units/*.o(.bss .bss.* COMMON)
		*/source/items/*.o(.bss .bss.* COMMON)
		*/source/effects/*.o(.bss .bss.* COMMON)
		*/source/physics/*.o(.bss .bss.* COMMON)
		*/source/devices/*.o(.bss .bss.* COMMON)
		*/source/networking/network_client_manager.o(.bss .bss.* COMMON)
		*/source/networking/network_client_message_handler.o(.bss .bss.* COMMON)
		*/source/networking/network_game_globals.o(.bss .bss.* COMMON)
		*/source/networking/network_game_manager.o(.bss .bss.* COMMON)
		*/port/linux/game/network_distributed.o(.bss .bss.* COMMON)
		*/port/linux/game/network_objects.o(.bss .bss.* COMMON)
		*/port/linux/game/network_actors.o(.bss .bss.* COMMON)
		*/port/linux/game/network_damage.o(.bss .bss.* COMMON)
		*/port/linux/game/network_shield_effects.o(.bss .bss.* COMMON)
		*/port/linux/game/network_aim_assist.o(.bss .bss.* COMMON)
		*/port/linux/game/biped_ragdoll.o(.bss .bss.* COMMON)
		*/port/linux/game/infection.o(.bss .bss.* COMMON)
		*/port/linux/game/gun_game.o(.bss .bss.* COMMON)
		*/port/linux/game/game_modes.o(.bss .bss.* COMMON)
		__replay_bss_end = .;
	}
	.bss : { *(.bss .bss.*) *(COMMON) }
	. = ALIGN(0x1000);
	__guest_image_end = .;
	/DISCARD/ : { *(.comment) *(.note.*) *(.guest_fini) *(.fini_array .fini_array.*) *(.eh_frame)
		*(.init_array .init_array.*) }
}
