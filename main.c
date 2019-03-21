/*
 * esp32tool
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

#include "debug.h"
#include "esp32.h"
#include "reader.h"
#include "protocol.h"

uint32_t read_reg(int port_fd, uint32_t address) {
	uint8_t read_reg_packet[14];
	packet_header_t read_reg_packet_header = {
		.direction = 0x0,
		.command = ESP_READ_REG,
		.data_len = 0x0,
		.value_or_checksum = 0x0,
	};
	size_t read_reg_packet_size = build_packet(read_reg_packet, 14, &read_reg_packet_header, (uint8_t *) &address, 4);

	printf("read pkt\n");
	hexdump(read_reg_packet, read_reg_packet_size);

	write(port_fd, read_reg_packet, read_reg_packet_size);

	// read
	while (1) {
		uint16_t packet_size = read_packet(port_fd);
		if (packet_size > 0) {
			hexdump(packet_buf, packet_size);
			if (packet_buf_header->command == ESP_READ_REG) {
				return packet_buf_header->value_or_checksum;
			}
		}
	}

	return 0;
}

uint32_t read_efuse(int port_fd, uint8_t i) {
	return read_reg(port_fd, ESP32_EFUSE_REG_BASE + (4 * i));
}

int main(int argc, char ** argv) {
	printf("esp32tool\n");

	const char * PORT_NAME = "/dev/cu.usbmodem0001";

	int port_fd = open(PORT_NAME, O_NONBLOCK | O_NOCTTY | O_RDWR);

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

	// read chip info
	uint32_t efuse1 = read_efuse(port_fd, 1);
	uint32_t efuse2 = read_efuse(port_fd, 2);
	uint32_t efuse3 = read_efuse(port_fd, 3);
	uint32_t efuse4 = read_efuse(port_fd, 4);
	uint32_t efuse6 = read_efuse(port_fd, 6);

	uint32_t chip_type = (efuse3 >> 9) & 0x7;
	uint32_t chip_revision = (efuse3 >> 15) & 0x1;

	printf("Chip type: ");

	switch (chip_type) {
		case 0:
			printf("ESP32D0WDQ6");
			break;

		case 1:
			printf("ESP32D0WDQ5");
			break;

		case 2:
			printf("ESP32D2WDQ5");
			break;

		case 5:
			printf("ESP32-PICO-D4");
			break;
	
		default:
			printf("unknown ESP32");
			break;
	}

	printf(" - revision %d\n", chip_revision);

	uint8_t mac_address[6];
	mac_address[0] = ((uint8_t *)&efuse2)[1];
	mac_address[1] = ((uint8_t *)&efuse2)[0];
	mac_address[2] = ((uint8_t *)&efuse1)[3];
	mac_address[3] = ((uint8_t *)&efuse1)[2];
	mac_address[4] = ((uint8_t *)&efuse1)[1];
	mac_address[5] = ((uint8_t *)&efuse1)[0];

	printf("MAC address: %02x:%02x:%02x:%02x:%02x:%02x\n",
		mac_address[0],
		mac_address[1],
		mac_address[2],
		mac_address[3],
		mac_address[4],
		mac_address[5]);

	close(port_fd);

	printf("closed\n");

	return 0;
}