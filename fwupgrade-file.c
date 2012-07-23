#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>

#include "fwupgrade-file.h"

char *fwupgrade_load_file_data(const char *filename, unsigned int *length_out)
{
	int fd;
	struct stat st;

	if (! filename)
		return NULL;

	fd = open(filename, O_RDONLY);
	if (fd < 0)
		return NULL;

	if (fstat(fd, &st))
		return NULL;

	return mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
}
