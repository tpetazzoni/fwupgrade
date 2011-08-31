
CFLAGS=-Wall
HOSTCC?=gcc

all: fwupgrade-cgi fwupgrade-tool

fwupgrade-cgi: fwupgrade-cgi.c fwupgrade-cgi-data.c fwupgrade-uboot-env.c md5.c crc32.c
	$(CC) -o $@ $^ $(CFLAGS)

fwupgrade-tool: fwupgrade-tool.c md5.c
	$(HOSTCC) -o $@ $^ $(CFLAGS)

clean:
	$(RM) *.o fwupgrade-tool fwupgrade-cgi
