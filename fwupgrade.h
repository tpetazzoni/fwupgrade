#ifndef FWUPGRADE_H
#define FWUPGRADE_H

#include <stdint.h>

#define FWPART_NAME_SZ 16
#define FWPART_CRC_SZ  16

/* Structure describing one part of the firmware. */
struct fwpart {
	/* 0-terminated string */
	char         name[FWPART_NAME_SZ];

	/* MD5SUM of the part data */
	char         crc[FWPART_CRC_SZ];

	/* Size of the part, in bytes */
	unsigned int length;

	/* Offset of the part, in bytes, from the beginning of the
	   file */
	unsigned int offset;

	/* Pad the structure so that it takes 128 bytes. This should
	   allows future extensions */
	char         unused[88];
};

#define FWPART_COUNT 8

#define FWUPGRADE_MAGIC 0x5E7F28CD

/* Header of the firmware. We pad it so that the structure takes 2048
   bytes, for future extensions */
struct fwheader {
	unsigned int  magic;
	unsigned int  hwid;
	unsigned int  flags;
	struct fwpart parts[FWPART_COUNT];
	char          unused[1012];
};

void md5 (const char *input, int len, char output[16]);

#endif /* FWUPGRADE_H */
