#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#define VALID_CONTENT_TYPE "multipart/form-data; boundary="
#define VALID_ELEMENT_CONTENT_DISPOSITION "Content-Disposition: form-data"
#define VALID_ELEMENT_CONTENT_TYPE "Content-Type: application/octet-stream"

FILE *logfile;

char *nextline(char *s, unsigned int *remaining)
{
	/* Go to the end of line */
	while (*s != '\n' && *s != '\r' && *s != 0) {
		*remaining--;
		s++;
	}

	if (! *s)
		return NULL;

	if (*s == '\r') {
		*remaining--;
		s++;
	}
	else
		return NULL;

	if (*s == '\n') {
		*remaining--;
		s++;
	}
	else
		return NULL;

	return s;
}

int main(int argc, char *argv[])
{
	char *method;
	char *content_type;
	char *content_length;
	long length, length_read;
	char *buffer;
	char *boundary, *boundary_start, *data;
	unsigned int boundary_len, data_len, remaining;

	logfile = fopen("/tmp/fwupgrade.log", "a+");

	method = getenv("REQUEST_METHOD");
	if (! method) {
		fprintf(logfile, "no method\n");
		exit(1);
	}

	if (strcasecmp(method, "post")) {
		fprintf(logfile, "incorrect method %s\n", method);
		exit(1);
	}

	content_type = getenv("CONTENT_TYPE");
	if (! content_type) {
		fprintf(logfile, "no content type\n");
		exit(1);
	}

	content_length = getenv("CONTENT_LENGTH");
	if (! content_length) {
		fprintf(logfile, "no content length\n");
		exit(1);
	}

	/* Verify that we have a supported content type */
	if (strncasecmp(content_type, VALID_CONTENT_TYPE,
			strlen(VALID_CONTENT_TYPE))) {
		fprintf(logfile, "unsupported content type %s\n",
			content_type);
		exit(1);
	}

	boundary_start = strchr(content_type, '=');
	if (! boundary_start) {
		fprintf(logfile, "cannot find boundary delimiter\n");
		exit(1);
	}

	/* Skip the '=' character */
	boundary_start += 1;

	boundary_len = 2 + strlen(boundary_start);
	boundary = malloc(boundary_len + 1);
	if (! boundary) {
		fprintf(logfile, "memory allocation problem\n");
		exit(1);
	}

	snprintf(boundary, boundary_len + 1, "--%s", boundary_start);

	length = strtol(content_length, NULL, 10);
	if (length == LONG_MIN || length == LONG_MAX) {
		fprintf(logfile, "incorrect length\n");
		exit (1);
	}

	buffer = malloc(length);
	if (! buffer) {
		fprintf(logfile, "cannot allocate memory\n");
		exit (1);
	}

	length_read = fread(buffer, 1, length, stdin);
	if (length_read != length) {
		fprintf(logfile, "could not read the complete %ld bytes\n", length);
		exit (1);
	}

	fprintf(logfile, "I have read %ld bytes\n", length);
	remaining = length;

	/* The data should start with the boundary delimiter */
	if (strncmp(boundary, buffer, boundary_len)) {
		fprintf(logfile, "cannot find boundary delimiter in data\n");
		exit(1);
	}

	buffer = nextline(buffer, & remaining);

	/* Check that we have a Content-Disposition line */
	if (strncmp(buffer, VALID_ELEMENT_CONTENT_DISPOSITION,
		    strlen(VALID_ELEMENT_CONTENT_DISPOSITION))) {
		fprintf(logfile, "cannot find Content-Disposition in element\n");
		exit(1);
	}

	buffer = nextline(buffer, & remaining);

	/* Check that we have a Content-Type line */
	if (strncmp(buffer, VALID_ELEMENT_CONTENT_TYPE,
		    strlen(VALID_ELEMENT_CONTENT_TYPE))) {
		fprintf(logfile, "cannot find Content-Type in element\n");
		exit(1);
	}

	/* Skip all lines until we find an empty line */
	while((buffer = nextline(buffer, & remaining)) != NULL) {
		if (buffer[0] == '\r' && buffer[1] == '\n') {
			buffer = nextline(buffer, & remaining);
			break;
		}
	}

	if (buffer == NULL) {
		fprintf(logfile, "cannot find data\n");
		exit(1);
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

	fprintf(logfile, "data length is data_len: %d\n", data_len);

	fprintf(stdout, "Content-type: text/html\n\n");
	fprintf(stdout, "Koin koin");
}
