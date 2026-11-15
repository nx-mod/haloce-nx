/* The map lists' IMPORT as the host does it (port/android/host/host_mcc_import.c) run natively over a data root:
 * IMPORT_ROOT, its import inbox filled by tools/test_mcc_import.py. The argument answers its questions in order: r
 * replace, k keep (r when none is left). Prints each question and the report. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "host_mcc_import.h"

void host_logf(int priority, const char *format, ...) { (void)priority; (void)format; }

int main(int argc, char **argv)
{
	char custom[1024], report[HOST_MCC_IMPORT_TEXT_SIZE], title[96], destination[64];
	const char *answers = argc > 1 ? argv[1] : "";
	const char *root = getenv("IMPORT_ROOT");
	int changes, answered = 0;
	struct timespec pause = { 0, 5 * 1000 * 1000 };

	snprintf(custom, sizeof(custom), "%s/custom_maps", root);
	if (!host_mcc_import_begin(root, custom))
		return 2;
	for (;;)
	{
		int state = host_mcc_import_current_state();

		if (state == HOST_MCC_IMPORT_DONE)
			break;
		if (state == HOST_MCC_IMPORT_ASKING)
		{
			int question = host_mcc_import_current_question(title, sizeof(title), destination, sizeof(destination));

			if (question != answered)
			{
				int replace = *answers ? *answers == 'r' : 1;

				printf("ASK %s|%s\n", title, destination);
				host_mcc_import_reply(question, replace);
				/* (a second press for the same question changes nothing) */
				host_mcc_import_reply(question, !replace);
				answered = question;
				if (*answers)
					answers++;
			}
		}
		nanosleep(&pause, NULL);
	}
	changes = host_mcc_import_end(report, sizeof(report));
	printf("REPORT %s\nCHANGED %d %d\n", report, (changes & HOST_MCC_IMPORT_MCC_CHANGED) != 0,
		(changes & HOST_MCC_IMPORT_MAPS_CHANGED) != 0);
	return 0;
}
