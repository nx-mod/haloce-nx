/*
HOST_MCC_IMPORT.H

The map lists' IMPORT, done by the host (host_mcc_import.c): its calls with
the host's own pointers (the native test's, tools/test_mcc_import.py), and
the guest's, whose addresses are 32-bit (declared again in
port/android/guest/runtime/guest_host.h; the guest's wrappers are
port/linux/src/mcc_import.c's).
*/

#ifndef HOST_MCC_IMPORT_H
#define HOST_MCC_IMPORT_H

/* the states (mcc_import.h's enum mcc_import_state, the same numbers) */
enum
{
	HOST_MCC_IMPORT_IDLE,
	HOST_MCC_IMPORT_WORKING,
	HOST_MCC_IMPORT_ASKING,
	HOST_MCC_IMPORT_DONE,
};

enum
{
	HOST_MCC_IMPORT_TEXT_SIZE = 2048,
	/* host_mcc_import_end's: what changed */
	HOST_MCC_IMPORT_MCC_CHANGED = 1,
	HOST_MCC_IMPORT_MAPS_CHANGED = 2,
};

/* the inbox's files (root's import folder) taken in on a thread, Custom Edition maps into custom_maps: 0 when an
import runs already */
int host_mcc_import_begin(char const *root, char const *custom_maps);
int host_mcc_import_current_state(void);
void host_mcc_import_current_progress(char *text, unsigned long size);
/* while ASKING: the package asked about, and the question's number */
int host_mcc_import_current_question(char *title, unsigned long title_size, char *destination,
	unsigned long destination_size);
/* the answer to that question (one to a question asked before is ignored) */
void host_mcc_import_reply(int question, int replace);
/* when DONE: the report, what changed (HOST_MCC_IMPORT_*_CHANGED); then IDLE again */
int host_mcc_import_end(char *report, unsigned long size);

/* the guest's */
int host_mcc_import_start(unsigned int root, unsigned int custom_maps);
int host_mcc_import_state(void);
void host_mcc_import_progress(unsigned int text, unsigned int size);
int host_mcc_import_question(unsigned int title, unsigned int title_size, unsigned int destination,
	unsigned int destination_size);
void host_mcc_import_answer(int question, int replace);
int host_mcc_import_finish(unsigned int report, unsigned int size);

#endif
