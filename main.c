/*
 * espminitool
 * todo:
 *  - fix scratch buffer overflow
 *  - fix reliance on a little-endian system
 */

#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include "commands.h"
#include "debug.h"
#include "esp32.h"
#include "reader.h"
#include "protocol.h"

int main(int argc, char ** argv) {
	printf("espminitool\n");

	if (argc < 2) {
		printf("usage: %s <serial port>\n", argv[0]);
		return 1;
	}

	char * port_name = argv[1];

	int port_fd = open(port_name, O_NONBLOCK | O_NOCTTY | O_RDWR);

	printf("opened\n");

	// termios stuff
	// see https://en.wikibooks.org/wiki/Serial_Programming/termios
	struct termios port_config;
	int err = tcgetattr(port_fd, &port_config);
	if (err < 0) {
		printf("tcgetattr err %d\n", err);
	}

	port_config.c_iflag &= ~(IGNBRK | BRKINT | ICRNL | INLCR | PARMRK | INPCK | ISTRIP | IXON);
	port_config.c_oflag = 0;
	port_config.c_lflag &= ~(ECHO | ECHONL | ICANON | IEXTEN | ISIG);
	port_config.c_cflag &= ~(CSIZE | PARENB);
	port_config.c_cflag |= CS8;
	port_config.c_cc[VMIN]  = 1;
	port_config.c_cc[VTIME] = 0;

	// set baud
	err = cfsetspeed(&port_config, 115200);
	if (err < 0) {
		printf("cfsetspeed err %d\n", err);
	}

	// actually set config
	tcsetattr(port_fd, TCSAFLUSH, &port_config);

	uint8_t talking_to_stub = sync_chip(port_fd);

	if (!talking_to_stub) {
		printf("Downloading stub...\n");
		download_stub(port_fd);
	} else {
		printf("Stub is running on chip, skipping download...\n");
	}

	print_chip_info(port_fd);

	close(port_fd);

	printf("closed\n");

	return 0;
}