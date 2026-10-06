/*
XISO.H

The maps folder out of an Xbox disc image (xiso.c).
*/

#ifndef __HALO_LINUX_XISO_H
#define __HALO_LINUX_XISO_H

/* called as the copy goes: the file being copied, and the bytes copied of
all the files' */
typedef void (*xiso_progress_proc)(void *context, const char *file, unsigned long long done,
	unsigned long long total);

/* copies the image's maps folder to <destination>/maps; returns nonzero on
success, or 0 with the reason (for the player) in error. Called from any
thread */
int xiso_extract_maps(const char *image_path, const char *destination, xiso_progress_proc progress, void *context,
	char *error, int error_size);

/* copies the image's root folder <folder> (e.g. "bink", the movies) into
<destination>/<folder>, alongside what is already there (defined only for
the Switch host, HALO_EXTRACTOR_STANDALONE) */
int xiso_extract_folder(const char *image_path, const char *folder, const char *destination,
	xiso_progress_proc progress, void *context, char *error, int error_size);
/* copies the file <name> from the image's root to <destination_path> (as
xiso_extract_folder, the Switch host only) */
int xiso_extract_file(const char *image_path, const char *name, const char *destination_path, char *error,
	int error_size);

#endif
