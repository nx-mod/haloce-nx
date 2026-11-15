/*
MCC_IMPORT.H

The map lists' IMPORT, shared by every platform (the host's, whose calls
mcc_import.c makes: port/android/host/host_mcc_import.c): what lands in
the import inbox, <data root>/import (Android's file picker copies the files
chosen there, HaloActivity.importMaps; the consoles' upload page puts them
there, map_import_page.c), taken in on a thread of its own:

- a .map: a Custom Edition map, into custom_maps;
- a .zip: Custom Edition maps (a set of them in a folder of its own), or MCC
  content: Workshop packages (a ModInfo.json each), MCC's resource sets (by
  their place in an MCC installation), or MCC maps alone (a package named
  after the .zip), into mcc_maps.

Every MCC package is worked out before any is installed; one already on disk
is asked about (MCC_IMPORT_ASKING: replace it, or keep the version on disk).
*/

#ifndef MCC_IMPORT_H
#define MCC_IMPORT_H

enum mcc_import_state
{
	MCC_IMPORT_IDLE,
	/* the inbox being read and staged, or the packages installed */
	MCC_IMPORT_WORKING,
	/* a package on disk already: mcc_import_question, then mcc_import_answer */
	MCC_IMPORT_ASKING,
	/* done: mcc_import_finish */
	MCC_IMPORT_DONE,
};

enum
{
	MCC_IMPORT_TEXT_SIZE = 2048,
};

/* the inbox's path, <data root>/import (made when it is not there) */
int mcc_import_inbox(char *path, unsigned long size);
/* whether the inbox has a whole file to take in (not a .part, which is still being written) */
int mcc_import_inbox_ready(void);
/* takes in the inbox's files on a thread, Custom Edition maps into custom_maps (the folder's path): 0 when an import runs already */
int mcc_import_start(char const *custom_maps);
enum mcc_import_state mcc_import_state(void);
/* while WORKING: what it is doing, for the player */
void mcc_import_progress(char *text, unsigned long size);
/* while ASKING: the package asked about (its title, and its folder in mcc_maps), and the question's number */
int mcc_import_question(char *title, unsigned long title_size, char *destination, unsigned long destination_size);
/* the answer to that question (by its number: an answer to one asked before is ignored): replace the package on
disk, or keep it */
void mcc_import_answer(int question, int replace);
/* when DONE: what was imported and what was not (for the player), whether
mcc_maps and custom_maps changed; then IDLE again */
void mcc_import_finish(char *report, unsigned long size, int *mcc_changed, int *maps_changed);

#endif
