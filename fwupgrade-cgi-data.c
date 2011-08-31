#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#include "fwupgrade-cgi.h"

#define VALID_CONTENT_TYPE "multipart/form-data; boundary="
#define VALID_ELEMENT_CONTENT_DISPOSITION "Content-Disposition: form-data"
#define VALID_ELEMENT_CONTENT_TYPE "Content-Type: application/octet-stream"

static char *nextline(char *s, unsigned int *remaining)
{
	/* Go to the end of line */
	while (*s != '\n' && *s != '\r' && *s != 0) {
		(*remaining)--;
		s++;
	}

	if (! *s)
		return NULL;

	if (*s == '\r') {
		(*remaining)--;
		s++;
	}
	else
		return NULL;

	if (*s == '\n') {
		(*remaining)--;
		s++;
	}
	else
		return NULL;

	return s;
}

char *cgi_receive_data(unsigned int *length_out)
{
	char *method;
	char *content_type;
	char *content_length;
	long length, length_read;
	char *buffer = NULL;
	char *boundary = NULL, *boundary_start, *data;
	unsigned int boundary_len, data_len, remaining;

	method = getenv("REQUEST_METHOD");
	if (! method) {
		printf("ERROR: incorrect REQUEST_METHOD, aborting.\n");
		goto error;
	}

	if (strcasecmp(method, "post")) {
		printf("ERROR: incorrect HTTP method, aborting.\n");
		goto error;
	}

	content_type = getenv("CONTENT_TYPE");
	if (! content_type) {
		printf("ERROR: no content type, aborting.\n");
		goto error;
	}

	content_length = getenv("CONTENT_LENGTH");
	if (! content_length) {
		printf("ERROR: no content length, aborting.\n");
		goto error;
	}

	/* Verify that we have a supported content type */
	if (strncasecmp(content_type, VALID_CONTENT_TYPE,
			strlen(VALID_CONTENT_TYPE))) {
		printf("ERROR: unsupported content type %s, aborting.\n",
		       content_type);
		goto error;
	}

	boundary_start = strchr(content_type, '=');
	if (! boundary_start) {
		printf("ERROR: cannot find boundary delimiter, aborting.\n");
		goto error;
	}

	/* Skip the '=' character */
	boundary_start += 1;

	boundary_len = 2 + strlen(boundary_start);
	boundary = malloc(boundary_len + 1);
	if (! boundary) {
		printf("ERROR: memory allocation problem, aborting.\n");
		goto error;
	}

	snprintf(boundary, boundary_len + 1, "--%s", boundary_start);

	length = strtol(content_length, NULL, 10);
	if (length == LONG_MIN || length == LONG_MAX) {
		printf("ERROR: incorrect length\n");
		goto error;
	}

	buffer = malloc(length);
	if (! buffer) {
		printf("ERROR: memory allocation problem, aborting.\n");
		goto error;
	}

	length_read = fread(buffer, 1, length, stdin);
	if (length_read != length) {
		printf("ERROR: could not read the complete %ld bytes, aborting.\n", length);
		goto error;
	}

	printf("Received a firmware image of %ld bytes\n", length);
	remaining = length;

	/* The data should start with the boundary delimiter */
	if (strncmp(boundary, buffer, boundary_len)) {
		printf("ERROR: cannot find boundary delimiter in data, aborting.\n");
		goto error;
	}

	buffer = nextline(buffer, & remaining);

	/* Check that we have a Content-Disposition line */
	if (strncmp(buffer, VALID_ELEMENT_CONTENT_DISPOSITION,
		    strlen(VALID_ELEMENT_CONTENT_DISPOSITION))) {
		printf("ERROR: cannot find Content-Disposition in element\n");
		goto error;
	}

	buffer = nextline(buffer, & remaining);

	/* Check that we have a Content-Type line */
	if (strncmp(buffer, VALID_ELEMENT_CONTENT_TYPE,
		    strlen(VALID_ELEMENT_CONTENT_TYPE))) {
		printf("ERROR: cannot find Content-Type in element\n");
		goto error;
	}

	/* Skip all lines until we find an empty line */
	while((buffer = nextline(buffer, & remaining)) != NULL) {
		if (buffer[0] == '\r' && buffer[1] == '\n') {
			buffer = nextline(buffer, & remaining);
			break;
		}
	}

	if (buffer == NULL) {
		printf("ERROR: cannot find data in firmware image\n");
		goto error;
	}

	/* The real data starts here */
	data = buffer;

	while(remaining) {
		/* Have we reached the boundary, which marks the end
		   of the data ? */
		if (! strncmp(buffer, boundary, boundary_len)) {
			if (! strncmp(data + data_len - 2, "\r\n", 2))
				data_len -= 2;
			break;
		}

		data_len ++;
		buffer   ++;
		remaining --;
	}

	*length_out = data_len;
	free(boundary);

	return data;

error:
	free(buffer);
	free(boundary);
	return NULL;
}
