#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#include "fwupgrade-cgi.h"

#define VALID_CONTENT_TYPE "multipart/form-data; boundary="
#define VALID_ELEMENT_CONTENT_DISPOSITION "Content-Disposition:"
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

char *get_filename_from_content_disposition(char *buf)
{
	char *p, *psave;
	char *localbuf, *tmp;
	int buflen;

	/* Eliminate the Content-Disposition header name */
	buf += strlen(VALID_ELEMENT_CONTENT_DISPOSITION);

	/* Copy the header value so that it is 0 terminated, which
	 * allows it to be parsed with strtok_r() */
	buflen = strchr(buf, '\r') - buf;
	localbuf = malloc(buflen + 1);
	if (! localbuf)
		return NULL;
	memset(localbuf, 0, buflen + 1);
	strncpy(localbuf, buf, buflen);

	tmp = localbuf;

	/* This parses a string like 'form-data; name="file";
	 * filename="foobar.img"' and extracts 'foobar.img' */
	while((p = strtok_r(tmp, ";", &psave)) != NULL) {
		char *delim;

		/* Eliminate spaces before the field name */
		while (*p == ' ')
			p++;

		/* Split the name from the value */
		delim = strchr(p, '=');
		if (! delim)
			goto next;
		else {
			int len; char *value; char *name;
			name = p;
			*delim = '\0';
			value = delim + 1;
			len = strlen(value);

			/* Remove quotes */
			if (value[0] == '"' && value[len-1] == '"') {
				value[len-1] = '\0';
				value++;
			}

			if (! strcmp(name, "filename")) {
				free(localbuf);
				return strdup(value);
			}
		}

	next:
		tmp = NULL;
	}

	free(localbuf);
	return NULL;
}

char *cgi_receive_data(unsigned int *length_out)
{
	char *method;
	char *content_type;
	char *content_length;
	long length, length_read;
	char *buffer = NULL;
	char *boundary = NULL, *boundary_start, *data, *cur;
	unsigned int boundary_len, data_len, remaining;
	int boundary_found;
	char *filename;

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

	remaining = length;
	cur       = buffer;

	/* The data should start with the boundary delimiter */
	if (strncmp(boundary, cur, boundary_len)) {
		printf("ERROR: cannot find boundary delimiter in data, aborting.\n");
		goto error;
	}

	cur = nextline(cur, & remaining);

	/* Check that we have a Content-Disposition line */
	if (strncmp(cur, VALID_ELEMENT_CONTENT_DISPOSITION,
		    strlen(VALID_ELEMENT_CONTENT_DISPOSITION))) {
		printf("ERROR: cannot find Content-Disposition in element\n");
		goto error;
	}

	filename = get_filename_from_content_disposition(cur);

	cur = nextline(cur, & remaining);

	/* Check that we have a Content-Type line */
	if (strncmp(cur, VALID_ELEMENT_CONTENT_TYPE,
		    strlen(VALID_ELEMENT_CONTENT_TYPE))) {
		printf("ERROR: cannot find Content-Type in element\n");
		goto error;
	}

	/* Skip all lines until we find an empty line */
	while((cur = nextline(cur, & remaining)) != NULL) {
		if (cur[0] == '\r' && cur[1] == '\n') {
			cur = nextline(cur, & remaining);
			break;
		}
	}

	if (cur == NULL) {
		printf("ERROR: cannot find data in firmware image\n");
		goto error;
	}

	/* The real data starts here */
	data = cur;

	data_len = 0;
	boundary_found = 0;
	while(remaining >= boundary_len) {
		/* Have we reached the boundary, which marks the end
		   of the data ? */
		if (! strncmp(cur, boundary, boundary_len)) {
			if (! strncmp(data + data_len - 2, "\r\n", 2))
				data_len -= 2;
			boundary_found = 1;
			break;
		}

		data_len ++;
		cur      ++;
		remaining --;
	}

	if (! boundary_found) {
		printf("ERROR: cannot find boundary\n");
		goto error;
	}

	printf("Received image file '%s' of %d bytes\n",
	       filename, data_len);

	/* Move the useful data at the beginning of the buffer, so
	   that the beginning of the data is 4 bytes aligned */
	memmove(buffer, data, data_len);
	*length_out = data_len;
	free(boundary);

	return buffer;

error:
	free(buffer);
	free(boundary);
	return NULL;
}
