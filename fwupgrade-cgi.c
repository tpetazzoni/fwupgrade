#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#include "fwupgrade.h"
#include "fwupgrade-cgi.h"
#include "fwupgrade-uboot-env.h"

FILE *logfile;

#define THIS_HWID 0x2424

struct fwupgrade_action {
	const char *part_name;
	const char *mtd_part1;
	const char *mtd_part2;
};

struct fwupgrade_action actions[] = {
	{ "kernel", "mtd0", "mtd2" },
	{ "rootfs", "mtd1", "mtd3" },
};

int flash_fwpart(const char *mtdpart, const char *data, unsigned int len,
		 char *msg, int msglen)
{
	char cmd[1024];
	int ret;
	FILE *nandwrite_pipe;
	size_t sz;

	snprintf(cmd, sizeof(cmd), "flash_erase /dev/%s 0 0", mtdpart);
	ret = system(cmd);
	if (ret) {
		snprintf(msg, msglen, "Unable to erase partition %s\n", mtdpart);
		return -1;
	}

	snprintf(cmd, sizeof(cmd), "nandwrite -p /dev/%s - > /tmp/nandwrite.log 2>&1", mtdpart);
	nandwrite_pipe = popen(cmd, "w");
	if (! nandwrite_pipe) {
		snprintf(msg, msglen, "Unable to flash partition %s\n", mtdpart);
		return -1;
	}

	sz = fwrite(data, len, 1, nandwrite_pipe);
	if (sz != 1) {
		snprintf(msg, msglen, "Unable to flash partition %s\n", mtdpart);
		return -1;
	}

	ret = pclose(nandwrite_pipe);
	if (! WIFEXITED(ret) || WEXITSTATUS(ret) != 0) {
		snprintf(msg, msglen, "Unable to flash partition %s %d %m\n", mtdpart, WIFEXITED(ret));
		return -1;
	}

	return 0;
}

int handle_fwpart(const char *partname, const char *data, unsigned int len,
		  char *msg, int msglen)
{
	struct fwupgrade_action *act = NULL;
	const char *current_mtdpart, *next_mtdpart;
	char uboot_varname[64];
	int i, ret;

	for (i = 0; i < (sizeof(actions) / sizeof(actions[0])); i++) {
		if (! strcmp(actions[i].part_name, partname)) {
			act = & actions[i];
			break;
		}
	}

	if (! act) {
		snprintf(msg, msglen, "Unknown partition '%s' in firmware image", partname);
		return -1;
	}

	snprintf(uboot_varname, sizeof(uboot_varname), "%s_mtdpart", partname);
	current_mtdpart = fw_env_read(uboot_varname);
	if (! current_mtdpart) {
		snprintf(msg, msglen, "Cannot find current MTD partition for '%s'", partname);
		return -1;
	}

	if (! strcmp(current_mtdpart, act->mtd_part1)) {
		next_mtdpart = act->mtd_part2;
	}
	else if (! strcmp(current_mtdpart, act->mtd_part2)) {
		next_mtdpart = act->mtd_part1;
	}
	else {
		snprintf(msg, msglen, "Invalid current MTD partition '%s'", current_mtdpart);
		return -1;
	}

	ret = flash_fwpart(next_mtdpart, data, len, msg, msglen);
	if (ret)
		return ret;

	fw_env_write(uboot_varname, (char*) next_mtdpart);

	return 0;
}

int apply_upgrade(const char *data, unsigned int data_length,
		  char *msg, unsigned int msglen)
{
	int i, ret;

	struct fwheader *header = (struct fwheader *) data;

	if (le32toh(header->magic) != FWUPGRADE_MAGIC) {
		snprintf(msg, msglen, "Invalid firmware magic");
		return -1;
	}

	if (le32toh(header->hwid) != THIS_HWID) {
		snprintf(msg, msglen, "Invalid HWID");
		return -1;
	}

	/* First loop to verify the CRC */
	for (i = 0; i < FWPART_COUNT; i++) {
		unsigned int sz, offset;
		char computed_crc[FWPART_CRC_SZ];

		sz = le32toh(header->parts[i].length);
		if (! sz)
			continue;

		offset = le32toh(header->parts[i].offset);

		md5(data + offset, sz, computed_crc);
		if (memcmp(computed_crc, header->parts[i].crc, FWPART_CRC_SZ)) {
			snprintf(msg, msglen, "Invalid CRC");
			return -1;
		}
	}

	ret = fw_env_open();
	if (ret) {
		snprintf(msg, msglen, "Cannot read the U-Boot environment");
		return -1;
	}

	/* Second loop to actually apply the upgrade */
	for (i = 0; i < FWPART_COUNT; i++) {
		unsigned int sz, offset;
		int ret;

		sz = le32toh(header->parts[i].length);
		if (! sz)
			continue;

		offset = le32toh(header->parts[i].offset);

		fprintf(logfile, "Handling part %d\n", i);

		ret = handle_fwpart(header->parts[i].name, data + offset, sz,
				    msg, msglen);
		if (ret)
			return ret;
	}

	ret = fw_env_close();
	if (ret) {
		snprintf(msg, msglen, "Could not rewrite U-Boot environment");
		return -1;
	}

	return 0;
}

int main(int argc, char *argv[])
{
	char *data;
	char msg[256];
	unsigned int data_length;
	int ret;

	logfile = fopen("/tmp/fwupgrade.log", "a+");

	fprintf(stdout, "Content-type: text/html\n\n");

	data = cgi_receive_data(& data_length);

	ret = apply_upgrade(data, data_length, msg, sizeof(msg));
	if (ret) {
		fprintf(stdout, "ERROR: %s", msg);
		return -1;
	}

	fprintf(stdout, "Upgrade successful");

	return 0;
}
