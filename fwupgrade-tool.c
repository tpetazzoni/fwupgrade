#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/mman.h>

#include "fwupgrade.h"

int dodumpfile(const char *filename)
{
	void *addr;
	struct stat s;
	int ret, fd, i;
	struct fwheader *header;

	ret = stat(filename, &s);
	if (ret) {
		fprintf(stderr, "Error while stat()ing file: %m\n");
		return -1;
	}

	fd = open(filename, O_RDONLY);;
	if (fd < 0) {
		fprintf(stderr, "Error while opening file: %m\n");
		return -1;
	}

	addr = mmap(NULL, s.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
	if (addr == MAP_FAILED) {
		fprintf(stderr, "Error while mapping file: %m\n");
		close(fd);
		return -1;
	}

	header = addr;

	if (le32toh(header->magic) != FWUPGRADE_MAGIC) {
		fprintf(stderr, "Unrecognized firmware file, invalid magic\n");
		return -1;
	}

	printf("HWID    : 0x%x\n", le32toh(header->hwid));
	printf("Flags   : 0x%x\n", le32toh(header->flags));
	for (i = 0; i < FWPART_COUNT; i++) {
		unsigned int sz, offset;
		char computed_crc[FWPART_CRC_SZ];

		sz = le32toh(header->parts[i].length);
		offset = le32toh(header->parts[i].offset);
		if (! sz)
			continue;

		md5(addr + offset, sz, computed_crc);
		if (memcmp(computed_crc, header->parts[i].crc, FWPART_CRC_SZ)) {
			fprintf(stderr, "CRC for part %d do not match\n", i);
			return -1;
		}

		printf("part[%d] : name=%s, size=%d, offset=%d\n",
		       i, header->parts[i].name, sz, offset);
	}

	return 0;
}

void help(void)
{
	printf("fwupgrade-tool, create and dump firmware images\n");
	printf(" image creation: fwupgrade-tool -o output-file -p part1name:part1file -p part2name:part2file -i HWID\n");
	printf(" image dump    : fwupgrade-tool -d image-file\n");
}

int main(int argc, char *argv[])
{
	int opt;
	unsigned int hwid = 0;

	/* Contains the name:filename list of strings, as passed by
	   the user using the -p option */
	char *parts[FWPART_COUNT];

	/* Contains the memory addresses at which each part has been
	   mapped */
	void *parts_addrs[FWPART_COUNT];
	int part_count = 0;
	int i, ret;

	char *output = NULL;
	char *dumpfile = NULL;
	struct fwheader header;
	unsigned int current_offset = sizeof(struct fwheader);
	int verbose = 0;

	memset(parts, 0, sizeof(parts));
	memset(parts_addrs, 0, sizeof(parts_addrs));

	/* Analyze the options. We fill the "hwid" variable and the
	   "parts" array. */
	while ((opt = getopt(argc, argv, "hi:p:o:d:v")) != -1) {
		switch(opt) {
		case 'h':
			help();
			exit(0);
		case 'i':
			hwid = strtol(optarg, NULL, 16);
			break;

		case 'p':
			if (part_count >= FWPART_COUNT) {
				fprintf(stderr, "Too many parts\n");
				exit(1);
			}
			parts[part_count] = strdup(optarg);
			part_count++;
			break;
		case 'o':
			output = strdup(optarg);
			break;
		case 'd':
			dumpfile = strdup(optarg);
			break;
		case 'v':
			verbose = 1;
			break;
		default:
			fprintf(stderr, "Unknown option\n");
			exit(1);
		}
	}

	if (dumpfile) {
		return dodumpfile(dumpfile);
	}

	if (part_count == 0) {
		fprintf(stderr, "No parts given, aborting\n");
		help();
		exit(1);
	}

	if (hwid == 0) {
		fprintf(stderr, "No HWID given, aborting\n");
		help();
		exit(1);
	}

	if (! output) {
		fprintf(stderr, "No output file given, aborting\n");
		help();
		exit(1);
	}

	memset(& header, 0, sizeof(struct fwheader));

	header.magic = htole32(FWUPGRADE_MAGIC);
	header.hwid  = htole32(hwid);
	header.flags = htole32(0);

	/* For each part, we get the part size, map the part into
	   memory, calculate its MD5 checksum and we fill the header
	   with those informations. */

	for (i = 0; i < part_count; i++) {
		struct stat s;
		int fd;
		char *filename, *tmp;
		int name_len;

		/* First, we extract the name:filename informations */
		tmp = strchr(parts[i], ':');
		if (! tmp) {
			fprintf(stderr, "Incorrect part syntax: '%s'\n",
				parts[i]);
			exit (1);
		}

		name_len = tmp - parts[i];

		if (name_len + 1 > FWPART_NAME_SZ) {
			fprintf(stderr, "Name too long in part: '%s'\n",
				parts[i]);
			exit(1);
		}

		/* Fill the name information of the part header */
		strncpy(header.parts[i].name, parts[i], name_len);
		header.parts[i].name[name_len] = '\0';

		/* Skip the ':' to get the filename */
		filename = tmp + 1;

		/* Get the file size */
		ret = stat(filename, & s);
		if (ret) {
			fprintf(stderr, "Cannot find part '%s'\n", parts[i]);
			exit(1);
		}

		/* Store the size in the header */
		header.parts[i].length = htole32(s.st_size);
		header.parts[i].offset = htole32(current_offset);
		current_offset += s.st_size;

		/* Open and map the file */
		fd = open(filename, O_RDONLY);
		if (fd < 0) {
			fprintf(stderr, "Cannot open part '%s'\n", parts[i]);
			exit(1);
		}

		parts_addrs[i] = mmap(NULL, s.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
		if (parts_addrs[i] == MAP_FAILED) {
			fprintf(stderr, "Cannot map part '%s'\n", parts[i]);
			exit(1);
		}

		/* Compute its MD5 */
		md5(parts_addrs[i], header.parts[i].length,
		    header.parts[i].crc);

		if (verbose)
			printf("part[%d], name=%s, filename=%s, size=%d, offset=%d, md5=%hhx%hhx%hhx%hhx%hhx%hhx%hhx%hhx%hhx%hhx%hhx%hhx%hhx%hhx%hhx%hhx\n",
			       i, header.parts[i].name, filename, header.parts[i].length, header.parts[i].offset,
			       header.parts[i].crc[0], header.parts[i].crc[1],
			       header.parts[i].crc[2], header.parts[i].crc[3],
			       header.parts[i].crc[4], header.parts[i].crc[5],
			       header.parts[i].crc[6], header.parts[i].crc[7],
			       header.parts[i].crc[8], header.parts[i].crc[9],
			       header.parts[i].crc[10], header.parts[i].crc[11],
			       header.parts[i].crc[12], header.parts[i].crc[13],
			       header.parts[i].crc[14], header.parts[i].crc[15]);
	}

	/* Write data to the output file: first the header, then each
	   part */
	FILE *outfile = fopen(output, "w+");
	fwrite(& header, 1, sizeof(header), outfile);
	for (i = 0; i < part_count; i++) {
		fwrite(parts_addrs[i], 1, header.parts[i].length, outfile);
	}
	fclose(outfile);

	return 0;
}
