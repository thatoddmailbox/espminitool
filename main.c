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
#include <time.h>
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

	// create packet
	uint8_t sync_packet[46];
	packet_header_t sync_packet_header = {
		.direction = 0x0,
		.command = ESP_SYNC,
		.data_len = 0x0,
		.value_or_checksum = 0x0,
	};
	uint8_t sync_packet_data[36] = "\x07\x07\x12\x20\x55\x55\x55\x55\x55\x55\x55\x55\x55\x55\x55\x55\x55\x55\x55\x55\x55\x55\x55\x55\x55\x55\x55\x55\x55\x55\x55\x55\x55\x55\x55\x55";
	uint8_t sync_packet_data_len = 36;
	size_t sync_packet_size = build_packet(sync_packet, 46, &sync_packet_header, sync_packet_data, sync_packet_data_len);

	hexdump(sync_packet, sync_packet_size);

	printf("write 1\n");

	// write sync packet two times to warm up autobauder
	write(port_fd, sync_packet, sync_packet_size);
	write(port_fd, sync_packet, sync_packet_size);

	// read stuff
	uint8_t sync_count = 0;
	clock_t last_sync_packet = 0;
	while (1) {
		uint16_t packet_size = read_packet(port_fd);
		if (packet_size > 0) {
			if (packet_buf_header->command == ESP_SYNC) {
				last_sync_packet = clock();
				sync_count++;
			}
		}

		if (sync_count >= 8) {
			// we've got at least 8 sync packets
			// maybe we're done?
			if ((clock() - last_sync_packet) > 0.25 * CLOCKS_PER_SEC) {
				break;
			}
		}
	}

	print_chip_info(port_fd);

	close(port_fd);

	printf("closed\n");

	return 0;
}