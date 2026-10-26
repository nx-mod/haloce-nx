/*
HOST_HTTPS.C

The server browser's requests to the master servers (host_https_get,
port/android/guest/runtime/guest_host.h; port/linux/src/master_list.c), made
in the host with the desktop's HTTPS client (port/linux/src/posix_update.c's
update_https_request, Mbed TLS, the system's certificate authorities): the
guest has no TLS of its own, and its memory is the host's to read and write.
*/

#include "host.h"
#include "../../linux/src/update.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int host_https_get(const char *url, char *headers, char *body, char *error, struct host_https_sizes *sizes)
{
	struct update_response response;

	sizes->status = 0;
	sizes->body_size = 0;
	if (!update_https_request("GET", url, NULL, NULL, 0, (unsigned long)sizes->body_capacity, &response, error,
		sizes->error_size))
	{
		return 0;
	}
	sizes->status = response.status;
	snprintf(headers, (size_t)sizes->headers_size, "%s", response.headers);
	if (response.body_size > (unsigned long)sizes->body_capacity)
		response.body_size = (unsigned long)sizes->body_capacity;
	memcpy(body, response.body, response.body_size);
	sizes->body_size = (int)response.body_size;
	free(response.body);
	return 1;
}
