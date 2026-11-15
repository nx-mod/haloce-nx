/*
MCC_IMPORT.C

The map lists' IMPORT in the guest (mcc_import.h): the import inbox's path
and whether it has a whole file, and the calls to the host, which takes the
files in (port/android/host/host_mcc_import.c: its .zip files read natively,
on a thread of its own, in its own memory). The guest's addresses are the
host's, passed as 32-bit numbers.
*/

#include "mcc_import.h"
#include "posix.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

const char *platform_data_root(void);

/* the host's (port/android/guest/runtime/guest_host.h) */
int host_mcc_import_start(unsigned int root, unsigned int custom_maps);
int host_mcc_import_state(void);
void host_mcc_import_progress(unsigned int text, unsigned int size);
int host_mcc_import_question(unsigned int title, unsigned int title_size, unsigned int destination,
	unsigned int destination_size);
void host_mcc_import_answer(int question, int replace);
int host_mcc_import_finish(unsigned int report, unsigned int size);

#define HOST_ADDRESS(pointer) ((unsigned int)(uintptr_t)(pointer))

static int is_directory(char const *path)
{
	struct posix_file_information information;

	return posix_stat(path, &information) == 0 && (information.flags & _posix_file_is_directory);
}

/* the data root's import folder, as the folder spells it if it has it in any case */
static int inbox_path(char *path, unsigned long size)
{
	char found[256];
	char const *name = "import";

	if (posix_find_entry_case_insensitive(platform_data_root(), name, found, sizeof(found)))
		name = found;
	return snprintf(path, size, "%s/%s", platform_data_root(), name) < (int)size;
}

int mcc_import_inbox(char *path, unsigned long size)
{
	if (!inbox_path(path, size))
		return 0;
	return is_directory(path) || posix_make_directory(path) == 0 || is_directory(path);
}

int mcc_import_inbox_ready(void)
{
	char inbox[1024], name[256];
	void *directory;
	int ready = 0;

	if (!inbox_path(inbox, sizeof(inbox)) || !(directory = posix_directory_open(inbox)))
		return 0;
	while (!ready && posix_directory_next(directory, name, sizeof(name)))
	{
		size_t length = strlen(name);

		/* (a .part is still being copied or uploaded) */
		ready = name[0] != '.' && !(length > 5 && !strcmp(name + length - 5, ".part"));
	}
	posix_directory_close(directory);
	return ready;
}

int mcc_import_start(char const *custom_maps)
{
	static char root[1024], folder[1024];

	/* (held until the host's thread has copied them) */
	snprintf(root, sizeof(root), "%s", platform_data_root());
	snprintf(folder, sizeof(folder), "%s", custom_maps);
	return host_mcc_import_start(HOST_ADDRESS(root), HOST_ADDRESS(folder));
}

enum mcc_import_state mcc_import_state(void)
{
	return (enum mcc_import_state)host_mcc_import_state();
}

void mcc_import_progress(char *text, unsigned long size)
{
	host_mcc_import_progress(HOST_ADDRESS(text), (unsigned int)size);
}

int mcc_import_question(char *title, unsigned long title_size, char *destination, unsigned long destination_size)
{
	return host_mcc_import_question(HOST_ADDRESS(title), (unsigned int)title_size, HOST_ADDRESS(destination),
		(unsigned int)destination_size);
}

void mcc_import_answer(int question, int replace)
{
	host_mcc_import_answer(question, replace);
}

void mcc_import_finish(char *report, unsigned long size, int *mcc_changed, int *maps_changed)
{
	int changes = host_mcc_import_finish(HOST_ADDRESS(report), (unsigned int)size);

	/* (the host's HOST_MCC_IMPORT_MCC_CHANGED and _MAPS_CHANGED) */
	*mcc_changed = (changes & 1) != 0;
	*maps_changed = (changes & 2) != 0;
}
