/*
STEAM_SEAL.H

Where the Steam sign-in is kept, and how it is kept unreadable off the
device (steam_downloads.c's session file holds the refresh token, which is
as good as the account to whoever has it, until it is revoked):

- Android: in the app's private storage (SDL_GetAndroidInternalStoragePath:
  not the data folder, which a file manager, a USB cable or another app with
  storage access can read), sealed with an AES-GCM key of the Android
  Keystore that never leaves it (HaloActivity.steamSeal): a copy of the file
  opens on no other phone, nor on this one once the app is uninstalled;
- Switch and PS5: in the data root's steam folder (the SD card, the PS5's
  data folder: there is nowhere else), sealed with a key made from what is
  this console's alone (the Switch's serial number and device id, the PS5's
  OpenPsId): a copied SD card or folder opens on no other console. This is
  binding, not secrecy: someone with this console can still read it;
- when there is no key to be had, the token is kept as it is, and the log
  says so.

The file is written whole to a .part, readable by its owner alone where the
file system has such a thing, then renamed over the old one.
*/

#ifndef STEAM_SEAL_H
#define STEAM_SEAL_H

#include <stddef.h>
#include <stdio.h>

/* the folder the session file goes in, made if need be (root: the data
root, the consoles' place for it) */
void steam_seal_folder(char const *root, char *folder, size_t size);

/* text sealed into out ("<scheme>:<hex>"): 1, or 0 where this device has no
key (out empty) */
int steam_seal(char const *text, char *out, size_t out_size);
/* a sealed text opened into out: 1, or 0 (another device's, damaged, or
the key gone) */
int steam_unseal(char const *sealed, char *out, size_t out_size);
/* the key forgotten where it is the device's to forget (Android's
Keystore entry), after a log out */
void steam_seal_forget(void);

/* a file of the owner's alone, made anew (0600 where there are modes),
for writing: NULL on a failure */
FILE *steam_seal_create(char const *path);

#endif
