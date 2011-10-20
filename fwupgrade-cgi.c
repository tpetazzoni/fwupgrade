#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <unistd.h>
#include <sys/reboot.h>
#include <linux/reboot.h>

#include "fwupgrade.h"
#include "fwupgrade-cgi.h"
#include "fwupgrade-uboot-env.h"

#define THIS_HWID 0x2424

struct fwupgrade_action {
	const char *part_name;
	const char *mtd_part1;
	const char *mtd_part2;
};

struct fwupgrade_action actions[FWPART_COUNT];

int flash_fwpart(const char *mtdpart, const char *data, unsigned int len)
{
	char cmd[1024];
	int ret;
	FILE *nandwrite_pipe;
	size_t sz;

	printf("Erasing partition %s\n", mtdpart);

	snprintf(cmd, sizeof(cmd), "flash_erase -q /dev/%s 0 0", mtdpart);
	ret = system(cmd);
	if (ret) {
		printf("ERROR: Unable to erase partition %s, aborting.\n", mtdpart);
		return -1;
	}

	printf("Flashing partition %s\n", mtdpart);

	snprintf(cmd, sizeof(cmd), "nandwrite -q -p /dev/%s -", mtdpart);
	nandwrite_pipe = popen(cmd, "w");
	if (! nandwrite_pipe) {
		printf("ERROR: Unable to flash partition %s, aborting\n", mtdpart);
		return -1;
	}

	sz = fwrite(data, len, 1, nandwrite_pipe);
	if (sz != 1) {
		printf("ERROR: Unable to flash partition %s, aborting\n", mtdpart);
		return -1;
	}

	ret = pclose(nandwrite_pipe);
	if (! WIFEXITED(ret) || WEXITSTATUS(ret) != 0) {
		printf("ERROR: Unable to flash partition %s, aborting\n", mtdpart);
		return -1;
	}

	return 0;
}

int handle_fwpart(const char *partname, const char *data, unsigned int len)
{
	struct fwupgrade_action *act = NULL;
	const char *current_mtdpart, *next_mtdpart;
	char uboot_varname[64];
	int i, ret;

	for (i = 0; i < FWPART_COUNT; i++) {
		if (actions[i].part_name == NULL)
			break;

		if (! strcmp(actions[i].part_name, partname)) {
			act = & actions[i];
			break;
		}
	}

	if (! act) {
		printf("ERROR: Unknown partition '%s' in firmware image, aborting.\n", partname);
		return -1;
	}

	snprintf(uboot_varname, sizeof(uboot_varname), "%s_mtdpart", partname);
	current_mtdpart = fw_env_read(uboot_varname);
	if (! current_mtdpart) {
		printf("ERROR: Cannot find current MTD partition for '%s', aborting.\n", partname);
		return -1;
	}

	if (! strcmp(current_mtdpart, act->mtd_part1)) {
		next_mtdpart = act->mtd_part2;
	}
	else if (! strcmp(current_mtdpart, act->mtd_part2)) {
		next_mtdpart = act->mtd_part1;
	}
	else {
		printf("ERROR: Invalid current MTD partition '%s' for %s, aborting.\n",
		       current_mtdpart, act->part_name);
		return -1;
	}

	ret = flash_fwpart(next_mtdpart, data, len);
	if (ret)
		return ret;

	fw_env_write(uboot_varname, (char*) next_mtdpart);

	return 0;
}

int apply_upgrade(const char *data, unsigned int data_length)
{
	int i, ret;
	struct fwheader *header = (struct fwheader *) data;

	if (le32toh(header->magic) != FWUPGRADE_MAGIC) {
		printf("ERROR: Invalid firmware magic, aborting.\n");
		return -1;
	}

	if (le32toh(header->hwid) != THIS_HWID) {
		printf("ERROR: Invalid HWID, aborting.\n");
		return -1;
	}

	/* First loop to verify the CRC */
	for (i = 0; i < FWPART_COUNT; i++) {
		unsigned int sz, offset;
		char computed_crc[FWPART_CRC_SZ];

		sz = le32toh(header->parts[i].length);
		if (! sz)
			continue;

		printf("Checking part %s\n", header->parts[i].name);

		offset = le32toh(header->parts[i].offset);

		md5(data + offset, sz, computed_crc);
		if (memcmp(computed_crc, header->parts[i].crc, FWPART_CRC_SZ)) {
			printf("ERROR: Invalid CRC in firmware image part %s\n",
			       header->parts[i].name);
			return -1;
		}
	}

	ret = fw_env_open();
	if (ret) {
		printf("ERROR: Cannot read the U-Boot environment, aborting.\n");
		return -1;
	}

	/* Second loop to actually apply the upgrade */
	for (i = 0; i < FWPART_COUNT; i++) {
		unsigned int sz, offset;
		int ret;

		sz = le32toh(header->parts[i].length);
		if (! sz)
			continue;

		printf("Applying part %s\n", header->parts[i].name);

		offset = le32toh(header->parts[i].offset);

		ret = handle_fwpart(header->parts[i].name, data + offset, sz);
		if (ret)
			return ret;
	}

	ret = fw_env_close();
	if (ret) {
		printf("ERROR: Could not rewrite U-Boot environment, aborting\n");
		return -1;
	}

	return 0;
}

int parse_configuration(void)
{
	char line[255];
	int action = 0;

	FILE *cfg = fopen("/etc/fwupgrade.conf", "r");
	if (! cfg)
		return -1;

	memset(actions, 0, sizeof(actions));

	while (fgets(line, sizeof(line), cfg)) {
		char *tmp, *cur;
		enum { FIELD_PART_NAME,
		       FIELD_MTD_PART1,
		       FIELD_MTD_PART2 } field = FIELD_PART_NAME;

		if (action >= FWPART_COUNT) {
			fclose(cfg);
			return -1;
		}

		/* Remove ending newline if any */
		if (line[strlen(line)-1] == '\n')
			line[strlen(line)-1] = '\0';

		/* Split the three ':' separated fields */
		tmp = line;
		while((cur = strtok(tmp, ":")) != NULL) {
			if (field == FIELD_PART_NAME)
				actions[action].part_name = strdup(cur);
			else if (field == FIELD_MTD_PART1)
				actions[action].mtd_part1 = strdup(cur);
			else if (field == FIELD_MTD_PART2)
				actions[action].mtd_part2 = strdup(cur);
			field++;
			tmp = NULL;
		}

		action++;
	}

	fclose(cfg);

	return 0;
}

int main(int argc, char *argv[])
{
	char *data;
	unsigned int data_length;
	int ret;

	/* Switch to line-oriented buffering for stdout so that
	 * messages are sent to the HTTP client right away */
	setvbuf(stdout, NULL, _IOLBF, BUFSIZ);

	printf("Content-type: text/plain\n\n");

	ret = parse_configuration();
	if (ret < 0) {
		printf("Problem parsing configuration\n");
		return -1;
	}

	data = cgi_receive_data(& data_length);
	if (! data) {
		printf("Failed to receive data\n");
		return -1;
	}

	ret = apply_upgrade(data, data_length);
	if (ret) {
		printf("The system upgrade failed\n");
		close(STDOUT_FILENO);
		return -1;
	} else {
		printf("The system upgrade completed successfully\n");
		fflush(stdout);
		close(STDOUT_FILENO);
		sleep(1);
		reboot(LINUX_REBOOT_CMD_RESTART);
	}

	return 0;
}
