

all: fwupgrade-cgi fwupgrade-tool

fwupgrade-cgi: fwupgrade-cgi.c
	$(CC) -o $@ $^

fwupgrade-tool: fwupgrade-tool.c md5.c
	$(CC) -o $@ $^