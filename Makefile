
CFLAGS=-Wall
HOSTCC?=gcc

all: fwupgrade fwupgrade-tool

fwupgrade: fwupgrade.c fwupgrade-cgi.c fwupgrade-file.c fwupgrade-uboot-env.c md5.c crc32.c
	$(CC) -o $@ $^ $(CFLAGS)

fwupgrade-tool: fwupgrade-tool.c md5.c
	$(HOSTCC) -o $@ $^ $(CFLAGS)

clean:
	$(RM) *.o fwupgrade-tool fwupgrade
