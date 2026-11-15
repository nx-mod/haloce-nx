/*
HOST_MCC_IMPORT.C

The map lists' IMPORT on every platform (port/linux/src/mcc_import.h, whose
calls in the guest are these: host_mcc_import_*): the files in the import
inbox, <data root>/import, taken in on a thread of the host's own. The host
does it, not the guest: its files are read and written natively (an MCC
package's .zip can pass 4 GB, which the guest's 32-bit offsets and its
system-call bridge are not for), and its tables and buffers are the host's
memory, not the guest image's.

Shared by the Android app's host, the Switch's (tools/switch_build.py) and
the PS5's (tools/ps5_build.py): it uses the C library, the platform layer's
host files (posix_files.c, posix_mcc_share.c), its JSON reader
(server_json.c) and the port's zlib.

A .zip is read through its central directory (zip64 too: an MCC package can
pass 4 GB), and what it is told from its files' names and its maps' headers
before anything is copied:

- Custom Edition maps (version 609): copied into custom_maps as a .map.part
  each, checked, then named; more than one map (the resource maps aside) is a
  set, in a folder named like the .zip. A map of that name there is kept.
- MCC content (a ModInfo.json, an MCC installation's resource set, or version
  13 maps): each package copied into a staging of its own under mcc_staging
  (beside mcc_maps, so that publishing it is a rename), and checked as
  the Android importer it replaces did: its maps' headers and names, its campaign's
  metadata (its missions, as the game's mcc_manifest_load reads them), the
  Workshop ID it carries (written to workshop_id.txt).

When every file is staged, a package whose folder is in mcc_maps already is
asked about (HOST_MCC_IMPORT_ASKING), and then the packages are installed: the old
folder moved aside, the new one moved in, the old one removed; a failed move
puts the old one back, and an import cut short is recovered by the next. The
inbox's files are removed once they have been taken in, imported or not.
*/

#include "host.h"
#include "host_mcc_import.h"
/* (GCC's, the Switch's: its paths are cut at their buffers' size, as any of
the host's are, and those that could reach it are refused where they are
made: what it warns of is no fault here) */
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic ignored "-Wformat-truncation"
#endif
#include "posix.h"
#include "server_json.h"
#include "zlib_prefixed.h"

#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <sys/types.h>


enum
{
	IMPORT_MAXIMUM_FILES = 4096,
	IMPORT_MAXIMUM_PACKAGES = 64,
	IMPORT_MAXIMUM_STAGINGS = IMPORT_MAXIMUM_PACKAGES + 32,
	IMPORT_PATH_SIZE = 1024,
	IMPORT_NAME_SIZE = 513,
	/* Custom Edition's limits (HaloActivity.java's, as the lists take them) */
	CE_MAP_NAME_LENGTH = 51,
	CE_SET_NAME_LENGTH = 63,
	CE_DESCRIPTION_SIZE = 65536,
	CE_VERSION = 609,
	XBOX_VERSION = 5,
	MCC_VERSION = 13,
	MAP_HEADER_SIZE = 2048,
	JSON_MAXIMUM = 262144,
	SIDE_FILE_MAXIMUM = 8 << 20,
};

#define CE_MAP_MAXIMUM ((uint64_t)1 << 30)
#define MCC_MAP_MAXIMUM ((uint64_t)0x7fffffff)
#define IMPORT_TOTAL_MAXIMUM ((uint64_t)16 << 30)

/* ---------- the report */

struct import_list
{
	char text[1024];
};

static void list_add(struct import_list *list, char const *separator, char const *format, ...)
	__attribute__((format(__printf__, 3, 4)));
static void list_add(struct import_list *list, char const *separator, char const *format, ...)
{
	size_t length = strlen(list->text);
	va_list arguments;

	if (length + 8 >= sizeof(list->text))
		return;
	if (length)
	{
		snprintf(list->text + length, sizeof(list->text) - length, "%s", separator);
		length = strlen(list->text);
	}
	va_start(arguments, format);
	vsnprintf(list->text + length, sizeof(list->text) - length, format, arguments);
	va_end(arguments);
}

/* ---------- the state */

struct prepared
{
	char destination[64];
	char title[96];
	char content[IMPORT_PATH_SIZE];
	char staging[IMPORT_PATH_SIZE];
	int maps;
	int campaign;
	int replace;
};

static struct
{
	int state;
	pthread_t thread;
	pthread_mutex_t lock;
	char root[IMPORT_PATH_SIZE];
	char custom_maps[IMPORT_PATH_SIZE];
	/* (under lock) */
	char progress[256];
	char question_title[96];
	char question_destination[64];
	/* the question's number, and its answer: -1 none yet, 0 keep, 1 replace */
	int question;
	int answer;
	struct import_list copied, skipped, failed, installed, kept;
	int mcc_changed, maps_changed;
	char report[HOST_MCC_IMPORT_TEXT_SIZE];
	/* (on the heap, made at the first import: some 240 KB, which the guest
	images have no room for) */
	struct prepared *packages;
	int package_count;
	/* the stagings made, removed at the end */
	char (*stagings)[IMPORT_PATH_SIZE];
	int staging_count;
	int parts;
} import = { .state = HOST_MCC_IMPORT_IDLE, .lock = PTHREAD_MUTEX_INITIALIZER };

static void progress(char const *format, ...) __attribute__((format(__printf__, 1, 2)));
static void progress(char const *format, ...)
{
	va_list arguments;

	pthread_mutex_lock(&import.lock);
	va_start(arguments, format);
	vsnprintf(import.progress, sizeof(import.progress), format, arguments);
	va_end(arguments);
	pthread_mutex_unlock(&import.lock);
}

/* ---------- files */

static int is_directory(char const *path)
{
	struct posix_file_information information;

	return posix_stat(path, &information) == 0 && (information.flags & _posix_file_is_directory);
}

static int exists(char const *path)
{
	struct posix_file_information information;

	return posix_stat(path, &information) == 0;
}

/* each folder of path made, up to its last part (which is left alone when last is 0) */
static int make_directories(char const *path, int last)
{
	char partial[IMPORT_PATH_SIZE];
	size_t index, length = strlen(path);

	if (length >= sizeof(partial))
		return 0;
	for (index = 1; index <= length; index++)
	{
		if (path[index] != '/' && (path[index] || !last))
			continue;
		memcpy(partial, path, index);
		partial[index] = 0;
		if (!is_directory(partial) && posix_make_directory(partial) != 0 && !is_directory(partial))
			return 0;
	}
	return 1;
}

/* a folder and what is in it removed (staging and old packages only: under mcc_staging) */
static void remove_tree(char const *path)
{
	void *directory;
	char name[256], child[IMPORT_PATH_SIZE];

	if (!is_directory(path))
	{
		posix_mcc_share_remove(path, 0);
		return;
	}
	directory = posix_directory_open(path);
	while (directory && posix_directory_next(directory, name, sizeof(name)))
	{
		if (!strcmp(name, ".") || !strcmp(name, ".."))
			continue;
		if (snprintf(child, sizeof(child), "%s/%s", path, name) < (int)sizeof(child))
			remove_tree(child);
	}
	if (directory)
		posix_directory_close(directory);
	posix_mcc_share_remove(path, 1);
}

/* parent/name, as the folder spells it if it has it in any case */
static int child_path(char *out, size_t size, char const *parent, char const *name)
{
	char found[256];

	if (posix_find_entry_case_insensitive(parent, name, found, sizeof(found)))
		name = found;
	return snprintf(out, size, "%s/%s", parent, name) < (int)size;
}

/* mcc_maps/destination (destination: a package's folder, or _shared/<set>) */
static int package_target(char *out, size_t size, char const *destination)
{
	char maps[IMPORT_PATH_SIZE], part[64];
	char const *slash = strchr(destination, '/');

	if (!child_path(maps, sizeof(maps), import.root, "mcc_maps"))
		return 0;
	if (!slash)
		return child_path(out, size, maps, destination);
	if ((size_t)(slash - destination) >= sizeof(part))
		return 0;
	memcpy(part, destination, slash - destination);
	part[slash - destination] = 0;
	{
		char shared[IMPORT_PATH_SIZE];

		return child_path(shared, sizeof(shared), maps, part) && child_path(out, size, shared, slash + 1);
	}
}

static int write_file(char const *path, void const *data, size_t size)
{
	FILE *file = fopen(path, "wb");
	int ok;

	if (!file)
		return 0;
	ok = fwrite(data, 1, size, file) == size;
	ok = fclose(file) == 0 && ok;
	return ok;
}

static long read_file(char const *path, void *data, size_t size)
{
	FILE *file = fopen(path, "rb");
	size_t got;

	if (!file)
		return -1;
	got = fread(data, 1, size, file);
	fclose(file);
	return (long)got;
}

static uint32_t little32(unsigned char const *bytes)
{
	return bytes[0] | bytes[1] << 8 | bytes[2] << 16 | (uint32_t)bytes[3] << 24;
}

static uint16_t little16(unsigned char const *bytes)
{
	return (uint16_t)(bytes[0] | bytes[1] << 8);
}

static uint64_t little64(unsigned char const *bytes)
{
	return little32(bytes) | (uint64_t)little32(bytes + 4) << 32;
}

/* ---------- names */

static void lower(char *text)
{
	for (; *text; text++)
		if (*text >= 'A' && *text <= 'Z')
			*text = (char)(*text - 'A' + 'a');
}

static char const *leaf(char const *path)
{
	char const *slash = strrchr(path, '/');

	return slash ? slash + 1 : path;
}

static int ends_with(char const *text, char const *end)
{
	size_t length = strlen(text), end_length = strlen(end);

	return length >= end_length && !strcmp(text + length - end_length, end);
}

/* a path in a .zip made safe (the Android importer's rules): '\' made '/'; 0 for one that is absolute, too deep,
has "." or ".." or a part a file system refuses */
static int safe_path(char const *name, char *out, size_t size)
{
	size_t length = strlen(name), index, part = 0, parts = 1;

	if (!length || length > 512 || length >= size)
		return 0;
	for (index = 0; index <= length; index++)
	{
		char c = name[index] == '\\' ? '/' : name[index];

		out[index] = c;
		if (c && c != '/')
		{
			if ((unsigned char)c < 32 || c == 127 || strchr(":*?\"<>|", c))
				return 0;
			continue;
		}
		if (index == part || (index - part == 1 && out[part] == '.') ||
			(index - part == 2 && out[part] == '.' && out[part + 1] == '.') ||
			out[index - 1] == '.' || out[index - 1] == ' ')
			return 0;
		if (c == '/' && ++parts > 8)
			return 0;
		part = index + 1;
	}
	return 1;
}

static int resource_name(char const *name)
{
	return !strcmp(name, "bitmaps.map") || !strcmp(name, "sounds.map") || !strcmp(name, "loc.map");
}

/* the MCC resource set a (lower case) path is from: an MCC installation's halo1/maps/bitmaps.map, the singleplayer
set; halo1/maps/custom_edition/bitmaps.map and sounds.map, the multiplayer set; else NULL */
static char const *shared_set(char const *key)
{
	static char const *const multiplayer[] = { "halo1/maps/custom_edition/bitmaps.map", "halo1/maps/custom_edition/sounds.map" };
	size_t index;

	for (index = 0; index < 2; index++)
	{
		size_t length = strlen(key), wanted = strlen(multiplayer[index]);

		if (length >= wanted && !strcmp(key + length - wanted, multiplayer[index]) &&
			(length == wanted || key[length - wanted - 1] == '/'))
			return "multiplayer";
	}
	if (!strcmp(key, "halo1/maps/bitmaps.map") || ends_with(key, "/halo1/maps/bitmaps.map"))
		return "singleplayer";
	return NULL;
}

/* a folder name for an MCC package: letters, digits, spaces, underscores and hyphens, at most 40, first a letter or a
digit; 0 when nothing of text is left */
static int package_name(char const *text, char *out, size_t size)
{
	size_t length = 0, start = 0;
	char const *dot = strrchr(text, '.');
	size_t end = dot && (!strcmp(dot, ".zip") || !strcmp(dot, ".ZIP")) ? (size_t)(dot - text) : strlen(text);
	size_t index;

	if (size < 41)
		return 0;
	for (index = 0; index < end && length < 40; index++)
	{
		char c = text[index];

		out[length++] = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == ' ' || c == '-' ? c : '_';
	}
	out[length] = 0;
	while (out[start] == ' ' || out[start] == '_' || out[start] == '-')
		start++;
	memmove(out, out + start, strlen(out + start) + 1);
	length = strlen(out);
	while (length && out[length - 1] == ' ')
		out[--length] = 0;
	return length > 0;
}

/* a Custom Edition map's name, of its file's: no folders, the extension gone, what the lists cannot take (anything
but printable ASCII, and what a file system refuses) made '_', no dot or space at its ends (HaloActivity.mapName) */
static int ce_name(char const *path, char const *extension, char *out, size_t size)
{
	char const *name = path + strlen(path);
	size_t length, index, start = 0, extension_length = strlen(extension);

	while (name > path && name[-1] != '/' && name[-1] != '\\')
		name--;
	length = strlen(name);
	if (length >= extension_length)
	{
		for (index = 0; index < extension_length; index++)
		{
			char c = name[length - extension_length + index];

			if ((c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c) != extension[index])
				break;
		}
		if (index == extension_length)
			length -= extension_length;
	}
	if (length >= size)
		length = size - 1;
	for (index = 0; index < length; index++)
	{
		char c = name[index];

		out[index] = (unsigned char)c < 0x20 || (unsigned char)c > 0x7E || strchr("\\/:*?\"<>|", c) ? '_' : c;
	}
	out[length] = 0;
	while (out[start] == '.' || out[start] == ' ')
		start++;
	memmove(out, out + start, strlen(out + start) + 1);
	length = strlen(out);
	while (length && (out[length - 1] == '.' || out[length - 1] == ' '))
		out[--length] = 0;
	return length > 0;
}

/* ---------- zip */

struct zip_entry
{
	char name[IMPORT_NAME_SIZE];
	/* its safe path, lower case ("" when unsafe) */
	char key[IMPORT_NAME_SIZE];
	uint64_t compressed, size, offset;
	unsigned method;
	int version;
};

struct zip
{
	FILE *file;
	struct zip_entry *entries;
	int count;
};

static void zip_close(struct zip *zip)
{
	if (zip->file)
		fclose(zip->file);
	free(zip->entries);
	memset(zip, 0, sizeof(*zip));
}

static int zip_seek(struct zip *zip, uint64_t offset)
{
	return fseeko(zip->file, (off_t)offset, SEEK_SET) == 0;
}

/* its central directory read; why says what is wrong when it cannot be */
static int zip_open(struct zip *zip, char const *path, char const **why)
{
	unsigned char *tail = NULL, header[56], *record = NULL;
	uint64_t size, tail_size, entries, directory_size, directory_offset, at;
	long found = -1, index;
	int ok = 0;

	memset(zip, 0, sizeof(*zip));
	*why = "it is not a .zip file";
	zip->file = fopen(path, "rb");
	if (!zip->file || fseeko(zip->file, 0, SEEK_END) != 0)
		goto done;
	size = (uint64_t)ftello(zip->file);
	tail_size = size < 65557 ? size : 65557;
	if (tail_size < 22 || !(tail = malloc((size_t)tail_size)) || !zip_seek(zip, size - tail_size) ||
		fread(tail, 1, (size_t)tail_size, zip->file) != tail_size)
		goto done;
	for (index = (long)tail_size - 22; index >= 0; index--)
	{
		if (little32(tail + index) == 0x06054b50)
		{
			found = index;
			break;
		}
	}
	if (found < 0)
		goto done;
	entries = little16(tail + found + 10);
	directory_size = little32(tail + found + 12);
	directory_offset = little32(tail + found + 16);
	if (entries == 0xFFFF || directory_size == 0xFFFFFFFF || directory_offset == 0xFFFFFFFF)
	{
		/* (zip64: its locator just before, and the record it points at) */
		uint64_t locator = size - tail_size + (uint64_t)found;

		if (locator < 20 || !zip_seek(zip, locator - 20) || fread(header, 1, 20, zip->file) != 20 ||
			little32(header) != 0x07064b50 || !zip_seek(zip, little64(header + 8)) ||
			fread(header, 1, 56, zip->file) != 56 || little32(header) != 0x06064b50)
			goto done;
		entries = little64(header + 32);
		directory_size = little64(header + 40);
		directory_offset = little64(header + 48);
	}
	*why = "it has too many files";
	if (entries > IMPORT_MAXIMUM_FILES)
		goto done;
	*why = "its directory is damaged";
	if (directory_offset + directory_size > size || directory_size > 64u << 20 ||
		!(record = malloc((size_t)directory_size + 1)) || !zip_seek(zip, directory_offset) ||
		fread(record, 1, (size_t)directory_size, zip->file) != directory_size ||
		!(zip->entries = calloc(entries ? (size_t)entries : 1, sizeof(*zip->entries))))
		goto done;
	for (at = 0, index = 0; index < (long)entries; index++)
	{
		struct zip_entry *entry = &zip->entries[zip->count];
		unsigned name_length, extra_length, comment_length, flags;
		unsigned char const *extra;

		if (at + 46 > directory_size || little32(record + at) != 0x02014b50)
			goto done;
		flags = little16(record + at + 8);
		entry->method = little16(record + at + 10);
		entry->compressed = little32(record + at + 20);
		entry->size = little32(record + at + 24);
		name_length = little16(record + at + 28);
		extra_length = little16(record + at + 30);
		comment_length = little16(record + at + 32);
		entry->offset = little32(record + at + 42);
		if (at + 46 + name_length + extra_length + comment_length > directory_size || name_length >= sizeof(entry->name))
			goto done;
		memcpy(entry->name, record + at + 46, name_length);
		entry->name[name_length] = 0;
		/* (zip64's sizes and offset, in that order, for those its fields say are in it) */
		for (extra = record + at + 46 + name_length; extra + 4 <= record + at + 46 + name_length + extra_length;
			extra += 4 + little16(extra + 2))
		{
			unsigned char const *value = extra + 4, *end = extra + 4 + little16(extra + 2);

			if (little16(extra) != 0x0001)
				continue;
			if (entry->size == 0xFFFFFFFF && value + 8 <= end)
				entry->size = little64(value), value += 8;
			if (entry->compressed == 0xFFFFFFFF && value + 8 <= end)
				entry->compressed = little64(value), value += 8;
			if (entry->offset == 0xFFFFFFFF && value + 8 <= end)
				entry->offset = little64(value);
		}
		at += 46 + name_length + extra_length + comment_length;
		if (!name_length || entry->name[name_length - 1] == '/' || entry->name[name_length - 1] == '\\')
			continue;
		if ((flags & 1) || (entry->method != 0 && entry->method != 8))
		{
			*why = "it is encrypted, or compressed in a way that cannot be read (use a plain .zip)";
			goto done;
		}
		if (safe_path(entry->name, entry->key, sizeof(entry->key)))
			lower(entry->key);
		else
			entry->key[0] = 0;
		entry->version = -1;
		zip->count++;
	}
	ok = 1;
done:
	free(tail);
	free(record);
	if (!ok)
		zip_close(zip);
	return ok;
}

/* an entry's bytes, into out (when given) and memory (when given, the first memory_size of them); its size, or -1
when it cannot be read or is larger than limit */
static int sink(unsigned char const *data, size_t produced, uint64_t *total, uint64_t limit, uint64_t expected,
	FILE *out, unsigned char *memory, uint64_t memory_size)
{
	uint64_t from = *total;

	*total += produced;
	if (*total > limit || *total > expected)
		return 0;
	if (memory && from < memory_size)
	{
		uint64_t room = memory_size - from;

		memcpy(memory + from, data, (size_t)(produced < room ? produced : room));
	}
	return !out || !produced || fwrite(data, 1, produced, out) == produced;
}

static int64_t zip_read(struct zip *zip, struct zip_entry const *entry, FILE *out, unsigned char *memory,
	uint64_t memory_size, uint64_t limit)
{
	unsigned char header[30], *in = NULL, *buffer = NULL;
	uint64_t left = entry->compressed, total = 0;
	z_stream stream;
	int result = Z_OK, ok = 0;

	if (entry->size > limit)
		return -1;
	if (!zip_seek(zip, entry->offset) || fread(header, 1, 30, zip->file) != 30 || little32(header) != 0x04034b50 ||
		!zip_seek(zip, entry->offset + 30 + little16(header + 26) + little16(header + 28)))
		return -1;
	if (!(in = malloc(65536)))
		return -1;
	if (entry->method == 0)
	{
		while (left)
		{
			size_t got = fread(in, 1, left < 65536 ? (size_t)left : 65536, zip->file);

			if (!got || !sink(in, got, &total, limit, entry->size, out, memory, memory_size))
				break;
			left -= got;
		}
		ok = !left;
	}
	else if ((buffer = malloc(65536)) != NULL)
	{
		memset(&stream, 0, sizeof(stream));
		if (inflateInit2(&stream, -MAX_WBITS) == Z_OK)
		{
			while (result != Z_STREAM_END)
			{
				if (!stream.avail_in)
				{
					size_t got = left ? fread(in, 1, left < 65536 ? (size_t)left : 65536, zip->file) : 0;

					if (!got)
						break;
					left -= got;
					stream.next_in = in;
					stream.avail_in = (uInt)got;
				}
				stream.next_out = buffer;
				stream.avail_out = 65536;
				result = inflate(&stream, Z_NO_FLUSH);
				if ((result != Z_OK && result != Z_STREAM_END) ||
					!sink(buffer, 65536 - stream.avail_out, &total, limit, entry->size, out, memory, memory_size))
					break;
			}
			ok = result == Z_STREAM_END;
			inflateEnd(&stream);
		}
	}
	free(in);
	free(buffer);
	return ok && total == entry->size ? (int64_t)total : -1;
}

/* the first bytes of an entry (up to size), for its header: how many */
static int zip_head(struct zip *zip, struct zip_entry const *entry, unsigned char *head, size_t size)
{
	unsigned char header[30], in[4096];
	z_stream stream;
	uint64_t left = entry->compressed;
	int got = 0, result = Z_OK;

	if (!zip_seek(zip, entry->offset) || fread(header, 1, 30, zip->file) != 30 || little32(header) != 0x04034b50 ||
		!zip_seek(zip, entry->offset + 30 + little16(header + 26) + little16(header + 28)))
		return 0;
	if (entry->method == 0)
	{
		size_t want = size < left ? size : (size_t)left;

		return (int)fread(head, 1, want, zip->file);
	}
	memset(&stream, 0, sizeof(stream));
	if (inflateInit2(&stream, -MAX_WBITS) != Z_OK)
		return 0;
	stream.next_out = head;
	stream.avail_out = (uInt)size;
	while (stream.avail_out && result == Z_OK && left)
	{
		size_t read = fread(in, 1, left < sizeof(in) ? (size_t)left : sizeof(in), zip->file);

		if (!read)
			break;
		left -= read;
		stream.next_in = in;
		stream.avail_in = (uInt)read;
		while (stream.avail_in && stream.avail_out && (result = inflate(&stream, Z_NO_FLUSH)) == Z_OK)
			;
	}
	got = (int)(size - stream.avail_out);
	inflateEnd(&stream);
	return got;
}

/* an entry's map version (a cache file's header's second field), -1 for none */
static int zip_version(struct zip *zip, struct zip_entry *entry)
{
	unsigned char head[8];

	if (entry->version == -1)
		entry->version = zip_head(zip, entry, head, sizeof(head)) == 8 && little32(head) == 0x68656164 ?
			(int)little32(head + 4) : -2;
	return entry->version < 0 ? -1 : entry->version;
}

/* an entry copied to path; 0, with why, when it cannot be */
static int zip_extract(struct zip *zip, struct zip_entry const *entry, char const *path, uint64_t limit,
	uint64_t *total, char const **why)
{
	FILE *out;
	int64_t size;

	*why = "it cannot be written";
	if (!make_directories(path, 0) || !(out = fopen(path, "wb")))
		return 0;
	size = zip_read(zip, entry, out, NULL, 0, limit);
	if (fclose(out) != 0 && size >= 0)
		size = -1, *why = "it cannot be written (is the storage full?)";
	else if (size < 0)
		*why = entry->size > limit ? "it is too large" : "it cannot be read from the .zip";
	if (size < 0 || (*total += (uint64_t)size) > IMPORT_TOTAL_MAXIMUM)
	{
		remove(path);
		if (size >= 0)
			*why = "the .zip is larger than 16 GB";
		return 0;
	}
	return 1;
}

/* ---------- Custom Edition maps */

/* why a map file is not one the lists take (as server_map_header reads it: a Custom Edition cache file; for a
resource map's name, that resource map), else NULL */
static char const *ce_problem(unsigned char const *header, long read, char const *name)
{
	static char message[96];
	uint32_t version;
	int resource = !strcmp(name, "bitmaps") ? 1 : !strcmp(name, "sounds") ? 2 : !strcmp(name, "loc") ? 3 : 0;

	if (resource)
		return read >= 4 && little32(header) == (uint32_t)resource ? NULL : "it is not Custom Edition's resource map";
	if (read < MAP_HEADER_SIZE || little32(header) != 0x68656164 || little32(header + 0x7FC) != 0x666f6f74)
		return "it is not a map file";
	version = little32(header + 4);
	if (version == XBOX_VERSION)
		return "it is an Xbox map, not a Custom Edition one";
	if (version == MCC_VERSION)
		return "it is an MCC map: import its package's .zip";
	if (version != CE_VERSION)
	{
		snprintf(message, sizeof(message), "it is not a Custom Edition map (its version is %u)", (unsigned)version);
		return message;
	}
	return NULL;
}

/* a checked map file (part) named folder[/set]/<name>.map; one of that name there is kept */
static void ce_keep(char const *part, char const *name, char const *set, char const *description, size_t description_size)
{
	char directory[IMPORT_PATH_SIZE], kept[IMPORT_PATH_SIZE], shown[128], text[IMPORT_PATH_SIZE];
	int resource = !strcmp(name, "bitmaps") || !strcmp(name, "sounds") || !strcmp(name, "loc");

	if (set && !resource)
		snprintf(directory, sizeof(directory), "%s/%s", import.custom_maps, set), snprintf(shown, sizeof(shown), "%s/%s", set, name);
	else
		snprintf(directory, sizeof(directory), "%s", import.custom_maps), snprintf(shown, sizeof(shown), "%s", name);
	snprintf(kept, sizeof(kept), "%s/%s.map", directory, name);
	if (exists(kept))
	{
		list_add(&import.skipped, ", ", "%s", shown);
		remove(part);
		return;
	}
	if (!make_directories(directory, 1) || rename(part, kept) != 0)
	{
		list_add(&import.failed, "; ", "%s: it cannot be kept", shown);
		remove(part);
		return;
	}
	list_add(&import.copied, ", ", "%s", shown);
	import.maps_changed = 1;
	snprintf(text, sizeof(text), "%s/%s.txt", directory, name);
	if (description && !exists(text))
		write_file(text, description, description_size);
}

/* the .map, part, checked as one of chosen (its file's name); 0, its reason reported, when it is not one to keep */
static int ce_check(char const *part, char const *chosen, char *name, size_t name_size)
{
	unsigned char header[MAP_HEADER_SIZE];
	char const *problem;
	long read;

	if (!ce_name(chosen, ".map", name, name_size))
	{
		list_add(&import.failed, "; ", "%s: it has no name", chosen);
		return 0;
	}
	if (strlen(name) > CE_MAP_NAME_LENGTH)
	{
		list_add(&import.failed, "; ", "%s: its name is longer than %d characters", name, CE_MAP_NAME_LENGTH);
		return 0;
	}
	if (!strcasecmp(name, "bitmaps") || !strcasecmp(name, "sounds") || !strcasecmp(name, "loc"))
		lower(name);
	read = read_file(part, header, sizeof(header));
	if ((problem = ce_problem(header, read, name)) != NULL)
	{
		list_add(&import.failed, "; ", "%s: %s", name, problem);
		return 0;
	}
	return 1;
}

/* a .map from the inbox */
static void ce_import_map(char const *path, char const *chosen)
{
	char name[256], part[IMPORT_PATH_SIZE];
	struct posix_file_information information;

	if (posix_stat(path, &information) == 0 &&
		((uint64_t)information.size_high << 32 | information.size_low) > CE_MAP_MAXIMUM)
	{
		list_add(&import.failed, "; ", "%s: it is larger than 1024 MB", chosen);
		return;
	}
	if (!ce_check(path, chosen, name, sizeof(name)))
		return;
	snprintf(part, sizeof(part), "%s/.import-%d.map.part", import.custom_maps, import.parts++);
	if (!make_directories(import.custom_maps, 1) || rename(path, part) != 0)
	{
		list_add(&import.failed, "; ", "%s: it cannot be moved into custom_maps", name);
		return;
	}
	ce_keep(part, name, NULL, NULL, 0);
}

/* a .zip of Custom Edition maps: its maps, and the .txt of a map's name, its description */
static void ce_import_zip(struct zip *zip, char const *chosen)
{
	struct kept { char name[64]; char part[IMPORT_PATH_SIZE]; } *maps;
	char set_name[CE_SET_NAME_LENGTH + 1];
	int count = 0, found = 0, set = 0, index;
	uint64_t total = 0;

	if (!(maps = calloc((size_t)zip->count + 1, sizeof(*maps))))
	{
		list_add(&import.failed, "; ", "%s: not enough memory", chosen);
		return;
	}
	for (index = 0; index < zip->count; index++)
	{
		struct zip_entry const *entry = &zip->entries[index];
		char lower_name[IMPORT_NAME_SIZE], name[256];
		char const *why;

		snprintf(lower_name, sizeof(lower_name), "%s", entry->name);
		lower(lower_name);
		if (!ends_with(lower_name, ".map"))
			continue;
		found++;
		progress("Copying %s", leaf(entry->name));
		snprintf(maps[count].part, sizeof(maps[count].part), "%s/.import-%d.map.part", import.custom_maps, import.parts++);
		if (!zip_extract(zip, entry, maps[count].part, CE_MAP_MAXIMUM, &total, &why))
		{
			list_add(&import.failed, "; ", "%s: %s", leaf(entry->name), why);
			continue;
		}
		if (!ce_check(maps[count].part, entry->name, name, sizeof(name)))
		{
			remove(maps[count].part);
			continue;
		}
		snprintf(maps[count].name, sizeof(maps[count].name), "%s", name);
		if (strcmp(name, "bitmaps") && strcmp(name, "sounds") && strcmp(name, "loc"))
			set++;
		count++;
	}
	if (!found)
		list_add(&import.failed, "; ", "%s: it has no .map file", chosen);
	if (!ce_name(chosen, ".zip", set_name, sizeof(set_name)))
		set_name[0] = 0;
	{
		size_t length = strlen(set_name);

		while (length && (set_name[length - 1] == '.' || set_name[length - 1] == ' '))
			set_name[--length] = 0;
	}
	for (index = 0; index < count; index++)
	{
		unsigned char *description = NULL;
		int64_t description_size = -1;
		int other;

		/* (its description: a .txt of its name, anywhere in the .zip) */
		for (other = 0; other < zip->count && description_size < 0; other++)
		{
			char name[256];

			if (ends_with(zip->entries[other].key, ".txt") && ce_name(zip->entries[other].name, ".txt", name, sizeof(name)) &&
				!strcasecmp(name, maps[index].name) && zip->entries[other].size <= CE_DESCRIPTION_SIZE &&
				(description = malloc(CE_DESCRIPTION_SIZE)) != NULL)
			{
				description_size = zip_read(zip, &zip->entries[other], NULL, description, CE_DESCRIPTION_SIZE, CE_DESCRIPTION_SIZE);
				if (description_size < 0)
				{
					free(description);
					description = NULL;
				}
			}
		}
		ce_keep(maps[index].part, maps[index].name, set > 1 && set_name[0] ? set_name : NULL,
			(char const *)description, description_size > 0 ? (size_t)description_size : 0);
		free(description);
	}
	free(maps);
}

/* ---------- MCC packages */

/* a staging of its own, under mcc_staging: its path */
static int staging_new(char *out, size_t size)
{
	char folder[IMPORT_PATH_SIZE];
	static unsigned long serial;
	int attempt;

	if (import.staging_count >= IMPORT_MAXIMUM_STAGINGS ||
		!child_path(folder, sizeof(folder), import.root, "mcc_staging") || !make_directories(folder, 1))
		return 0;
	for (attempt = 0; attempt < 8; attempt++)
	{
		if (snprintf(out, size, "%s/import-%08lx%04lx", folder, (unsigned long)time(NULL) & 0xFFFFFFFFUL,
			++serial & 0xFFFF) >= (int)size)
			return 0;
		if (!exists(out) && posix_make_directory(out) == 0)
		{
			char owner[IMPORT_PATH_SIZE];

			snprintf(owner, sizeof(owner), "%s/owner", out);
			write_file(owner, "mcc-import-v1", 13);
			snprintf(import.stagings[import.staging_count++], IMPORT_PATH_SIZE, "%s", out);
			return 1;
		}
	}
	return 0;
}

static struct prepared *package_new(char const *destination, char const *title, char const **why)
{
	struct prepared *package;
	int index;

	*why = "it has too many packages";
	if (import.package_count >= IMPORT_MAXIMUM_PACKAGES)
		return NULL;
	for (index = 0; index < import.package_count; index++)
	{
		if (!strcasecmp(import.packages[index].destination, destination))
		{
			*why = "the same package is in more than one of the files chosen";
			return NULL;
		}
	}
	package = &import.packages[import.package_count];
	memset(package, 0, sizeof(*package));
	snprintf(package->destination, sizeof(package->destination), "%s", destination);
	snprintf(package->title, sizeof(package->title), "%s", title && *title ? title : destination);
	*why = "a staging folder cannot be made";
	if (!staging_new(package->staging, sizeof(package->staging)) ||
		snprintf(package->content, sizeof(package->content), "%s/content", package->staging) >= (int)sizeof(package->content) ||
		posix_make_directory(package->content) != 0)
		return NULL;
	return package;
}

/* an MCC map's header checked (as the Android importer did): its type (0 campaign, 1 multiplayer), -1 when invalid */
static int mcc_map_type(char const *path)
{
	unsigned char header[MAP_HEADER_SIZE];
	struct posix_file_information information;
	uint64_t actual, length, offset, size;
	int index, name_end = 0, build_end = 0, type;

	if (posix_stat(path, &information) != 0 || read_file(path, header, sizeof(header)) != MAP_HEADER_SIZE)
		return -1;
	actual = (uint64_t)information.size_high << 32 | information.size_low;
	length = little32(header + 8);
	if (!length)
		length = actual;
	offset = little32(header + 16);
	size = little32(header + 20);
	type = little16(header + 0x60);
	for (index = 0; index < 32; index++)
	{
		name_end |= header[0x20 + index] == 0;
		build_end |= header[0x40 + index] == 0;
	}
	if (little32(header) != 0x68656164 || little32(header + 4) != MCC_VERSION || little32(header + 0x7FC) != 0x666f6f74 ||
		!name_end || !build_end || actual > MCC_MAP_MAXIMUM || length < MAP_HEADER_SIZE || length > actual || type > 1 ||
		offset < MAP_HEADER_SIZE || size < 40 || size > 0x4000000 || offset > length || size > length - offset)
		return -1;
	return type;
}

/* whether a level's name can be launched from mcc_maps/<package>/ (mcc_maps\<package>\<level> fits the game's 63) */
static int mcc_level_name(char const *package, char const *level)
{
	char const *c;

	if (!*level || !((*level >= 'a' && *level <= 'z') || (*level >= '0' && *level <= '9')))
		return 0;
	for (c = level; *c; c++)
		if (!((*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9') || *c == ' ' || *c == '_' || *c == '-'))
			return 0;
	return 9 + strlen(package) + 1 + strlen(level) <= 63;
}

static int retained(char const *key)
{
	return ends_with(key, ".map") || ends_with(key, ".json") || ends_with(key, ".png") || ends_with(key, ".txt");
}

static uint64_t side_limit(char const *key)
{
	return ends_with(key, ".map") ? MCC_MAP_MAXIMUM : ends_with(key, ".json") ? JSON_MAXIMUM : SIDE_FILE_MAXIMUM;
}

/* the text of a JSON token (a number's digits, a string's unescaped) */
static int json_text(struct server_json_document const *document, int token, char *out, int size)
{
	int length;

	if (token < 0)
		return 0;
	if (server_json_get_string(document, token, out, size))
		return 1;
	if (document->tokens[token].type != _server_json_primitive)
		return 0;
	length = document->tokens[token].end - document->tokens[token].start;
	if (length <= 0 || length >= size)
		return 0;
	memcpy(out, document->text + document->tokens[token].start, (size_t)length);
	out[length] = 0;
	return 1;
}

/* a staged package's JSON file (relative, in lower case as staged): parsed into document, its text held in *text
(freed with it); 0 when it is not there, too large or not an object */
static int json_read(char const *content, char const *relative, struct server_json_document *document, char **text)
{
	char path[IMPORT_PATH_SIZE];
	long length;
	int offset;

	*text = NULL;
	if (snprintf(path, sizeof(path), "%s/%s", content, relative) >= (int)sizeof(path) ||
		!(*text = malloc(JSON_MAXIMUM + 1)))
		return 0;
	length = read_file(path, *text, JSON_MAXIMUM + 1);
	offset = length >= 3 && (unsigned char)(*text)[0] == 0xEF && (unsigned char)(*text)[1] == 0xBB &&
		(unsigned char)(*text)[2] == 0xBF ? 3 : 0;
	if (length < 0 || length > JSON_MAXIMUM || !server_json_parse(document, *text + offset, (int)length - offset, 16))
	{
		free(*text);
		*text = NULL;
		return 0;
	}
	if (!document->count || document->tokens[0].type != _server_json_object)
	{
		server_json_document_free(document);
		free(*text);
		*text = NULL;
		return 0;
	}
	return 1;
}

/* whether a mission's kind is the one supported: absent, or "Default" in any case */
static int default_kind(struct server_json_document const *document)
{
	int token = server_json_member(document, 0, "CampaignMapKind");
	char kind[32];

	return token < 0 || (server_json_get_string(document, token, kind, sizeof(kind)) && !strcasecmp(kind, "default"));
}

/* a staged package's campaign, as the game's mcc_manifest_load reads it: CampaignInfo.json's 1 to 64 missions, each a
JSON file of the package's whose ScenarioFile names one of its campaign maps (levels, their types; each once), of the
supported kind; 0, with reason, when it is not */
static int campaign_check(char const *content, char (*levels)[64], int const *types, int level_count, char *reason,
	size_t reason_size)
{
	struct server_json_document document;
	char *text, (*missions)[128], (*names)[64];
	int list, count, index, ok = 0;

	snprintf(reason, reason_size, "its campaign metadata is missing or invalid");
	missions = calloc(64, sizeof(*missions));
	names = calloc(64, sizeof(*names));
	if (!missions || !names || !json_read(content, "campaigninfo.json", &document, &text))
		goto done;
	list = server_json_member(&document, 0, "CampaignMaps");
	count = list >= 0 && document.tokens[list].type == _server_json_array ? document.tokens[list].size : 0;
	for (index = 0; index < count && count <= 64; index++)
	{
		char safe[128];

		if (!server_json_get_string(&document, server_json_element(&document, list, index), missions[index],
			sizeof(missions[index])) || !safe_path(missions[index], safe, sizeof(safe)))
			break;
		snprintf(missions[index], sizeof(missions[index]), "%s", safe);
		lower(missions[index]);
	}
	server_json_document_free(&document);
	free(text);
	if (count < 1 || count > 64 || index != count)
	{
		snprintf(reason, reason_size, "its campaign must list 1 to 64 missions");
		goto done;
	}
	for (index = 0; index < count; index++)
	{
		char scenario[256], safe[256];
		char const *name;
		int level, other, valid;

		if (!json_read(content, missions[index], &document, &text))
		{
			snprintf(reason, reason_size, "its campaign's %s is missing or invalid", missions[index]);
			goto done;
		}
		valid = default_kind(&document) &&
			server_json_get_string(&document, server_json_member(&document, 0, "ScenarioFile"), scenario, sizeof(scenario)) &&
			safe_path(scenario, safe, sizeof(safe));
		server_json_document_free(&document);
		free(text);
		if (!valid)
		{
			snprintf(reason, reason_size, "Unsupported campaign mission kind or scenario path.");
			goto done;
		}
		name = leaf(safe);
		snprintf(names[index], sizeof(names[index]), "%s", name);
		lower(names[index]);
		if (ends_with(names[index], ".map"))
			names[index][strlen(names[index]) - 4] = 0;
		for (other = 0; other < index && strcmp(names[other], names[index]); other++)
			;
		for (level = 0; level < level_count && strcmp(levels[level], names[index]); level++)
			;
		if (other != index || level == level_count || types[level] != 0)
		{
			snprintf(reason, reason_size, "its campaign's mission %s is missing, listed twice or not a campaign map",
				names[index]);
			goto done;
		}
	}
	ok = 1;
done:
	free(missions);
	free(names);
	return ok;
}

/* a Workshop package: the ModInfo.json at prefix and the files under it */
static int mcc_workshop_package(struct zip *zip, int manifest, char const *chosen, uint64_t *total, char const **why)
{
	char prefix[IMPORT_NAME_SIZE], id[32] = "", title[96] = "", engine[16] = "", name[64];
	unsigned char *text;
	int64_t length;
	struct server_json_document document;
	struct prepared *package;
	int index, maps = 0, campaign = 0, digits;
	char (*levels)[64];
	int *types, level_count = 0;

	snprintf(prefix, sizeof(prefix), "%.*s", (int)(strlen(zip->entries[manifest].key) - strlen("modinfo.json")),
		zip->entries[manifest].key);
	*why = "its ModInfo.json cannot be read";
	if (!(text = malloc(JSON_MAXIMUM + 1)))
		return 0;
	length = zip_read(zip, &zip->entries[manifest], NULL, text, JSON_MAXIMUM, JSON_MAXIMUM);
	{
		int offset = length >= 3 && text[0] == 0xEF && text[1] == 0xBB && text[2] == 0xBF ? 3 : 0;

		if (length < 0 || !server_json_parse(&document, (char const *)text + offset, (int)length - offset, 16))
		{
			free(text);
			return 0;
		}
	}
	json_text(&document, server_json_member(&document, 0, "Engine"), engine, sizeof(engine));
	json_text(&document, server_json_member(&document, server_json_member(&document, 0, "Title"), "Neutral"), title, sizeof(title));
	json_text(&document, server_json_member(&document, server_json_member(&document,
		server_json_member(&document, 0, "ModIdentifier"), "HostedModIds"), "SteamWorkshopId"), id, sizeof(id));
	server_json_get_boolean(&document, server_json_member(&document, server_json_member(&document, 0, "GameModContents"),
		"HasCampaign"), &campaign);
	server_json_document_free(&document);
	free(text);
	*why = "only Halo 1 MCC packages can be imported";
	if (strcmp(engine, "Halo1"))
		return 0;
	for (digits = 0; id[digits] >= '0' && id[digits] <= '9'; digits++)
		;
	if (!digits || id[digits] || digits > 20)
		id[0] = 0;
	*why = "its package has neither a Workshop ID nor a usable title";
	if (!(id[0] ? snprintf(name, sizeof(name), "%s", id) > 0 : package_name(title, name, sizeof(name))))
		return 0;
	if (!(package = package_new(name, title, why)))
		return 0;
	*why = "not enough memory";
	levels = calloc((size_t)zip->count + 1, sizeof(*levels));
	types = calloc((size_t)zip->count + 1, sizeof(*types));
	if (!levels || !types)
		goto failed;
	for (index = 0; index < zip->count; index++)
	{
		struct zip_entry const *entry = &zip->entries[index];
		char const *relative = entry->key + strlen(prefix);
		char output[IMPORT_NAME_SIZE], path[IMPORT_PATH_SIZE];

		if (!entry->key[0] || strncmp(entry->key, prefix, strlen(prefix)) || !retained(entry->key))
			continue;
		snprintf(output, sizeof(output), "%s", relative);
		if (ends_with(relative, ".map"))
		{
			if (!strncmp(relative, "halo1/maps/", 11))
				snprintf(output, sizeof(output), "%s", relative + 11);
			else if (!strncmp(relative, "maps/", 5))
				snprintf(output, sizeof(output), "%s", relative + 5);
			*why = "it has a map in an unsupported folder";
			if (strchr(output, '/'))
				goto failed;
		}
		progress("Copying %s", leaf(entry->name));
		if (snprintf(path, sizeof(path), "%s/%s", package->content, output) >= (int)sizeof(path))
			goto failed;
		if (exists(path))
		{
			*why = "two of its files would have the same name";
			goto failed;
		}
		if (!zip_extract(zip, entry, path, side_limit(entry->key), total, why))
			goto failed;
		if (ends_with(output, ".map") && !resource_name(output))
		{
			snprintf(levels[level_count], sizeof(levels[level_count]), "%.*s", (int)(strlen(output) - 4), output);
			*why = "a map cannot be launched by its name";
			if (!mcc_level_name(name, levels[level_count]))
				goto failed;
			*why = "a map's header is not a Halo 1 MCC map's";
			if ((types[level_count] = mcc_map_type(path)) < 0)
				goto failed;
			level_count++;
			maps++;
		}
	}
	*why = "its package has no MCC maps, or more than 1024";
	if (!maps || maps > 1024)
		goto failed;
	if (campaign)
	{
		static char reason[160];

		*why = reason;
		if (!campaign_check(package->content, levels, types, level_count, reason, sizeof(reason)))
			goto failed;
		package->campaign = 1;
	}
	if (id[0])
	{
		char path[IMPORT_PATH_SIZE], sidecar[160];
		size_t index_title, kept = 0;

		for (index_title = 0; title[index_title] && kept < 40; index_title++)
			if ((unsigned char)title[index_title] >= 0x20 && (unsigned char)title[index_title] <= 0x7E)
				sidecar[strlen(id) + 1 + kept++] = title[index_title];
		memcpy(sidecar, id, strlen(id));
		sidecar[strlen(id)] = '\n';
		sidecar[strlen(id) + 1 + kept] = '\n';
		snprintf(path, sizeof(path), "%s/workshop_id.txt", package->content);
		write_file(path, sidecar, strlen(id) + 2 + kept);
	}
	package->maps = maps;
	free(levels);
	free(types);
	import.package_count++;
	(void)chosen;
	return 1;
failed:
	free(levels);
	free(types);
	return 0;
}

/* an MCC installation's resource set, by its place there (kind: singleplayer or multiplayer): -1 when it has none */
static int mcc_shared(struct zip *zip, char const *kind, uint64_t *total, char const **why)
{
	char destination[32], title[64];
	struct prepared *package;
	int index, bitmaps = -1, sounds = -1;

	for (index = 0; index < zip->count; index++)
	{
		char const *set = zip->entries[index].key[0] ? shared_set(zip->entries[index].key) : NULL;
		int *slot;

		if (!set || strcmp(set, kind))
			continue;
		slot = !strcmp(leaf(zip->entries[index].key), "bitmaps.map") ? &bitmaps : &sounds;
		*why = "it has more than one of a resource set's files";
		if (*slot >= 0)
			return 0;
		*slot = index;
	}
	if (bitmaps < 0 && sounds < 0)
		return -1;
	*why = !strcmp(kind, "singleplayer") ? "its singleplayer resources have no bitmaps.map" :
		"its multiplayer resources need both bitmaps.map and sounds.map";
	if (bitmaps < 0 || (!strcmp(kind, "multiplayer") && sounds < 0))
		return 0;
	snprintf(destination, sizeof(destination), "_shared/%s", kind);
	snprintf(title, sizeof(title), "MCC's %s resources", kind);
	if (!(package = package_new(destination, title, why)))
		return 0;
	for (index = 0; index < 2; index++)
	{
		int entry = index ? sounds : bitmaps;
		char path[IMPORT_PATH_SIZE];

		if (entry < 0)
			continue;
		progress("Copying %s", leaf(zip->entries[entry].name));
		snprintf(path, sizeof(path), "%s/%s", package->content, index ? "sounds.map" : "bitmaps.map");
		if (!zip_extract(zip, &zip->entries[entry], path, MCC_MAP_MAXIMUM, total, why))
			return 0;
		*why = "a resource file is too short";
		if (zip->entries[entry].size < 16)
			return 0;
	}
	import.package_count++;
	return 1;
}

/* version 13 maps and no ModInfo.json: a package of their own, named after the .zip (chosen) */
static int mcc_maps_alone(struct zip *zip, char const *chosen, uint64_t *total, char const **why)
{
	char name[64];
	struct prepared *package = NULL;
	int index, levels = 0;

	for (index = 0; index < zip->count; index++)
	{
		struct zip_entry *entry = &zip->entries[index];
		int version;

		if (!entry->key[0] || !ends_with(entry->key, ".map") || shared_set(entry->key) || resource_name(leaf(entry->key)))
			continue;
		version = zip_version(zip, entry);
		if (version != MCC_VERSION)
		{
			static char reason[IMPORT_NAME_SIZE + 64];

			snprintf(reason, sizeof(reason), "%s is %s", leaf(entry->name), version == CE_VERSION ?
				"a Custom Edition map, in a .zip of MCC maps" : "not an MCC map");
			*why = reason;
			return 0;
		}
		levels++;
	}
	if (!levels)
		return -1;
	*why = "its name has nothing a package folder can be named";
	if (!package_name(leaf(chosen), name, sizeof(name)) || !(package = package_new(name, name, why)))
		return 0;
	for (index = 0; index < zip->count; index++)
	{
		struct zip_entry const *entry = &zip->entries[index];
		char output[IMPORT_NAME_SIZE], path[IMPORT_PATH_SIZE];

		/* (the maps, their resources beside them, and their descriptions and pictures, at the package's top) */
		if (!entry->key[0] || shared_set(entry->key) || !(ends_with(entry->key, ".map") || ends_with(entry->key, ".txt") ||
			ends_with(entry->key, ".png")))
			continue;
		snprintf(output, sizeof(output), "%s", leaf(entry->key));
		if (snprintf(path, sizeof(path), "%s/%s", package->content, output) >= (int)sizeof(path))
			return 0;
		*why = "two of its files have the same name";
		if (exists(path))
			return 0;
		progress("Copying %s", leaf(entry->name));
		if (!zip_extract(zip, entry, path, side_limit(entry->key), total, why))
			return 0;
		if (ends_with(output, ".map") && !resource_name(output))
		{
			output[strlen(output) - 4] = 0;
			*why = "a map cannot be launched by its name";
			if (!mcc_level_name(name, output))
				return 0;
			*why = "a map's header is not a Halo 1 MCC map's";
			if (mcc_map_type(path) < 0)
				return 0;
		}
	}
	package->maps = levels;
	import.package_count++;
	return 1;
}

/* whether a .zip holds MCC content: a ModInfo.json, an MCC installation's resource set, or a version 13 map */
static int mcc_content(struct zip *zip)
{
	int index;

	for (index = 0; index < zip->count; index++)
	{
		struct zip_entry *entry = &zip->entries[index];

		if (!entry->key[0])
			continue;
		if (!strcmp(leaf(entry->key), "modinfo.json") || shared_set(entry->key))
			return 1;
		if (ends_with(entry->key, ".map") && !resource_name(leaf(entry->key)) && zip_version(zip, entry) == MCC_VERSION)
			return 1;
	}
	return 0;
}

/* a .zip of MCC content: every package in it worked out and staged */
static void mcc_import_zip(struct zip *zip, char const *chosen)
{
	uint64_t total = 0;
	char const *why = NULL;
	int index, manifests = 0, first = import.package_count, result = 1;

	for (index = 0; index < zip->count; index++)
	{
		if (!zip->entries[index].key[0])
		{
			list_add(&import.failed, "; ", "%s: it has an unsafe path (%s)", chosen, zip->entries[index].name);
			return;
		}
	}
	for (index = 0; index < zip->count && result; index++)
	{
		char const *key = zip->entries[index].key;

		if (strcmp(leaf(key), "modinfo.json"))
			continue;
		manifests++;
		{
			int other;
			size_t prefix = strlen(key) - strlen("modinfo.json");

			for (other = 0; other < zip->count; other++)
			{
				if (other != index && !strcmp(leaf(zip->entries[other].key), "modinfo.json") &&
					!strncmp(zip->entries[other].key, key, prefix))
				{
					why = "it has a package inside another";
					result = 0;
				}
			}
		}
		if (result)
			result = mcc_workshop_package(zip, index, chosen, &total, &why);
	}
	if (!manifests && result)
	{
		int singleplayer = mcc_shared(zip, "singleplayer", &total, &why);
		int multiplayer = singleplayer ? mcc_shared(zip, "multiplayer", &total, &why) : 0;
		int alone = singleplayer && multiplayer ? mcc_maps_alone(zip, chosen, &total, &why) : 0;

		result = singleplayer && multiplayer && alone;
		if (result && singleplayer < 0 && multiplayer < 0 && alone < 0)
		{
			why = "it has no MCC maps";
			result = 0;
		}
	}
	if (!result)
	{
		/* (none of it installed: its packages forgotten, their stagings removed at the end) */
		import.package_count = first;
		list_add(&import.failed, "; ", "%s: %s", chosen, why ? why : "it cannot be imported");
	}
}

/* the stagings an import cut short left: an old package a replacement had moved aside put back, the rest removed */
static void recover(void)
{
	char folder[IMPORT_PATH_SIZE], name[256];
	void *directory;

	if (!child_path(folder, sizeof(folder), import.root, "mcc_staging") || !is_directory(folder))
		return;
	directory = posix_directory_open(folder);
	while (directory && posix_directory_next(directory, name, sizeof(name)))
	{
		char staging[IMPORT_PATH_SIZE], destination[96], previous[IMPORT_PATH_SIZE], path[IMPORT_PATH_SIZE],
			target[IMPORT_PATH_SIZE], owner[16];
		long length;

		if (strncmp(name, "import-", 7) || snprintf(staging, sizeof(staging), "%s/%s", folder, name) >= (int)sizeof(staging))
			continue;
		snprintf(path, sizeof(path), "%s/owner", staging);
		if (read_file(path, owner, sizeof(owner)) != 13 || memcmp(owner, "mcc-import-v1", 13))
			continue;
		snprintf(path, sizeof(path), "%s/destination", staging);
		snprintf(previous, sizeof(previous), "%s/previous", staging);
		length = read_file(path, destination, sizeof(destination) - 1);
		if (exists(previous) && length > 0)
		{
			destination[length] = 0;
			if (package_target(target, sizeof(target), destination) && !exists(target) && make_directories(target, 0))
				posix_mcc_share_publish(previous, target);
		}
		if (!exists(previous))
			remove_tree(staging);
		else
			host_logf(HOST_LOG_WARN, "mcc import: %s needs attention (an old package could not be put back)", staging);
	}
	if (directory)
		posix_directory_close(directory);
}

/* a package moved into mcc_maps (an old one moved aside first, when replace, and put back if that fails) */
static int install(struct prepared *package, char const **why)
{
	char target[IMPORT_PATH_SIZE], previous[IMPORT_PATH_SIZE], record[IMPORT_PATH_SIZE];
	int existing;

	*why = "its folder in mcc_maps cannot be found";
	if (!package_target(target, sizeof(target), package->destination) || !make_directories(target, 0))
		return 0;
	existing = exists(target);
	snprintf(previous, sizeof(previous), "%s/previous", package->staging);
	snprintf(record, sizeof(record), "%s/destination", package->staging);
	*why = "it cannot be written";
	if (!write_file(record, package->destination, strlen(package->destination)))
		return 0;
	if (existing && !posix_mcc_share_publish(target, previous))
	{
		*why = "the package on disk cannot be moved aside";
		return 0;
	}
	if (!posix_mcc_share_publish(package->content, target))
	{
		*why = "it cannot be moved into mcc_maps";
		if (existing && !posix_mcc_share_publish(previous, target))
			*why = "it cannot be moved into mcc_maps, and the old package could not be put back (the next import tries)";
		return 0;
	}
	if (existing)
		remove_tree(previous);
	return 1;
}

/* ---------- the thread */

static int inbox_path(char *path, unsigned long size)
{
	return child_path(path, size, import.root, "import");
}

static int inbox_file_wanted(char const *name)
{
	return name[0] != '.' && !ends_with(name, ".part");
}

static int name_order(void const *a, void const *b)
{
	return strcasecmp((char const *)a, (char const *)b);
}

static void *import_main(void *unused)
{
	char inbox[IMPORT_PATH_SIZE], name[256];
	char (*names)[256] = NULL;
	int count = 0, index;
	void *directory;

	(void)unused;
	recover();
	if (inbox_path(inbox, sizeof(inbox)) && (names = calloc(256, sizeof(*names))) != NULL &&
		(directory = posix_directory_open(inbox)) != NULL)
	{
		while (count < 256 && posix_directory_next(directory, name, sizeof(name)))
			if (inbox_file_wanted(name))
				snprintf(names[count++], sizeof(names[0]), "%s", name);
		posix_directory_close(directory);
		/* (in the order of their names, as the questions come) */
		qsort(names, (size_t)count, sizeof(*names), name_order);
	}
	/* every file read and staged first */
	for (index = 0; index < count; index++)
	{
		char path[IMPORT_PATH_SIZE], lower_name[256];

		snprintf(path, sizeof(path), "%s/%s", inbox, names[index]);
		snprintf(lower_name, sizeof(lower_name), "%s", names[index]);
		lower(lower_name);
		progress("Reading %s", names[index]);
		if (ends_with(lower_name, ".zip"))
		{
			struct zip zip;
			char const *why;

			if (!zip_open(&zip, path, &why))
				list_add(&import.failed, "; ", "%s: %s", names[index], why);
			else
			{
				if (mcc_content(&zip))
					mcc_import_zip(&zip, names[index]);
				else
					ce_import_zip(&zip, names[index]);
				zip_close(&zip);
			}
		}
		else
			ce_import_map(path, names[index]);
		remove(path);
	}
	free(names);
	/* then each package on disk already asked about */
	for (index = 0; index < import.package_count; index++)
	{
		struct prepared *package = &import.packages[index];
		char target[IMPORT_PATH_SIZE];

		package->replace = -1;
		if (!package_target(target, sizeof(target), package->destination) || !exists(target))
			continue;
		pthread_mutex_lock(&import.lock);
		snprintf(import.question_title, sizeof(import.question_title), "%s", package->title);
		snprintf(import.question_destination, sizeof(import.question_destination), "%s", package->destination);
		import.question++;
		import.answer = -1;
		pthread_mutex_unlock(&import.lock);
		__atomic_store_n(&import.state, HOST_MCC_IMPORT_ASKING, __ATOMIC_RELEASE);
		while (__atomic_load_n(&import.answer, __ATOMIC_ACQUIRE) < 0)
		{
			struct timespec pause = { 0, 50 * 1000 * 1000 };

			nanosleep(&pause, NULL);
		}
		package->replace = import.answer;
		__atomic_store_n(&import.state, HOST_MCC_IMPORT_WORKING, __ATOMIC_RELEASE);
	}
	/* then installed */
	for (index = 0; index < import.package_count; index++)
	{
		struct prepared *package = &import.packages[index];
		char const *why;

		if (package->replace == 0)
		{
			list_add(&import.kept, ", ", "%s", package->title);
			continue;
		}
		progress("Installing %s", package->title);
		if (install(package, &why))
		{
			list_add(&import.installed, ", ", "%s (mcc_maps/%s)", package->title, package->destination);
			import.mcc_changed = 1;
		}
		else
			list_add(&import.failed, "; ", "%s: %s", package->title, why);
	}
	for (index = 0; index < import.staging_count; index++)
	{
		char previous[IMPORT_PATH_SIZE];

		snprintf(previous, sizeof(previous), "%s/previous", import.stagings[index]);
		if (!exists(previous))
			remove_tree(import.stagings[index]);
	}
	{
		char *report = import.report;
		size_t size = sizeof(import.report);
		int campaign = 0;

		for (index = 0; index < import.package_count; index++)
			campaign |= import.packages[index].campaign && import.packages[index].replace != 0;
		snprintf(report, size, "%s%s%s%s%s%s%s%s%s%s%s%s%s%s",
			import.installed.text[0] ? "Installed " : "", import.installed.text, import.installed.text[0] ? ". " : "",
			import.copied.text[0] ? "Imported " : "", import.copied.text, import.copied.text[0] ? ". " : "",
			import.skipped.text, import.skipped.text[0] ? (strchr(import.skipped.text, ',') ? " were already there. " : " was already there. ") : "",
			import.kept.text[0] ? "Kept the version on disk of " : "", import.kept.text, import.kept.text[0] ? ". " : "",
			import.failed.text[0] ? "Not imported: " : "", import.failed.text, import.failed.text[0] ? "." : "");
		if (!report[0])
			snprintf(report, size, "Nothing imported.");
		if (campaign && import.mcc_changed)
			snprintf(report + strlen(report), size - strlen(report),
				" For its missions in order, turn on CAMPAIGN MISSIONS in the MCC Packages settings.");
		host_logf(HOST_LOG_INFO, "mcc import: %s", report);
	}
	__atomic_store_n(&import.state, HOST_MCC_IMPORT_DONE, __ATOMIC_RELEASE);
	return NULL;
}

/* ---------- public code */

int host_mcc_import_begin(char const *root, char const *custom_maps)
{
	if (__atomic_load_n(&import.state, __ATOMIC_ACQUIRE) != HOST_MCC_IMPORT_IDLE)
		return 0;
	if (!import.packages && !(import.packages = calloc(IMPORT_MAXIMUM_PACKAGES, sizeof(*import.packages))))
		return 0;
	if (!import.stagings && !(import.stagings = calloc(IMPORT_MAXIMUM_STAGINGS, sizeof(*import.stagings))))
		return 0;
	memset(&import.copied, 0, sizeof(import.copied));
	memset(&import.skipped, 0, sizeof(import.skipped));
	memset(&import.failed, 0, sizeof(import.failed));
	memset(&import.installed, 0, sizeof(import.installed));
	memset(&import.kept, 0, sizeof(import.kept));
	import.mcc_changed = import.maps_changed = 0;
	import.package_count = import.staging_count = 0;
	import.report[0] = import.progress[0] = 0;
	snprintf(import.root, sizeof(import.root), "%s", root);
	snprintf(import.custom_maps, sizeof(import.custom_maps), "%s", custom_maps);
	{
		size_t length = strlen(import.custom_maps);

		while (length > 1 && (import.custom_maps[length - 1] == '/' || import.custom_maps[length - 1] == '\\'))
			import.custom_maps[--length] = 0;
	}
	import.state = HOST_MCC_IMPORT_WORKING;
	if (pthread_create(&import.thread, NULL, import_main, NULL) != 0)
	{
		import.state = HOST_MCC_IMPORT_IDLE;
		return 0;
	}
	pthread_detach(import.thread);
	return 1;
}

int host_mcc_import_current_state(void)
{
	return __atomic_load_n(&import.state, __ATOMIC_ACQUIRE);
}

void host_mcc_import_current_progress(char *text, unsigned long size)
{
	pthread_mutex_lock(&import.lock);
	snprintf(text, size, "%s", import.progress);
	pthread_mutex_unlock(&import.lock);
}

int host_mcc_import_current_question(char *title, unsigned long title_size, char *destination,
	unsigned long destination_size)
{
	int question;

	pthread_mutex_lock(&import.lock);
	snprintf(title, title_size, "%s", import.question_title);
	snprintf(destination, destination_size, "%s", import.question_destination);
	question = import.question;
	pthread_mutex_unlock(&import.lock);
	return question;
}

void host_mcc_import_reply(int question, int replace)
{
	pthread_mutex_lock(&import.lock);
	if (host_mcc_import_current_state() == HOST_MCC_IMPORT_ASKING && question == import.question && import.answer < 0)
		__atomic_store_n(&import.answer, replace ? 1 : 0, __ATOMIC_RELEASE);
	pthread_mutex_unlock(&import.lock);
}

int host_mcc_import_end(char *report, unsigned long size)
{
	int changes;

	if (host_mcc_import_current_state() != HOST_MCC_IMPORT_DONE)
	{
		if (size)
			report[0] = 0;
		return 0;
	}
	snprintf(report, size, "%s", import.report);
	changes = (import.mcc_changed ? HOST_MCC_IMPORT_MCC_CHANGED : 0) | (import.maps_changed ? HOST_MCC_IMPORT_MAPS_CHANGED : 0);
	__atomic_store_n(&import.state, HOST_MCC_IMPORT_IDLE, __ATOMIC_RELEASE);
	return changes;
}

/* ---------- the guest's calls (its addresses, 32-bit, are the host's) */

#define GUEST_POINTER(address) ((char *)(uintptr_t)(address))

int host_mcc_import_start(unsigned int root, unsigned int custom_maps)
{
	return host_mcc_import_begin(GUEST_POINTER(root), GUEST_POINTER(custom_maps));
}

int host_mcc_import_state(void)
{
	return host_mcc_import_current_state();
}

void host_mcc_import_progress(unsigned int text, unsigned int size)
{
	host_mcc_import_current_progress(GUEST_POINTER(text), size);
}

int host_mcc_import_question(unsigned int title, unsigned int title_size, unsigned int destination,
	unsigned int destination_size)
{
	return host_mcc_import_current_question(GUEST_POINTER(title), title_size, GUEST_POINTER(destination),
		destination_size);
}

void host_mcc_import_answer(int question, int replace)
{
	host_mcc_import_reply(question, replace);
}

int host_mcc_import_finish(unsigned int report, unsigned int size)
{
	return host_mcc_import_end(GUEST_POINTER(report), size);
}
