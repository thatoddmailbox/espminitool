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

#define ESP_SYNC 0x08
#define ESP_WRITE_REG 0x09
#define ESP_READ_REG 0x0a
#define ESP_SPI_SET_PARAMS 0x0b

#define ESP32_EFUSE_REG_BASE 0x6001a000

#define READ_BUF_SIZE 128
#define PACKET_BUF_SIZE 256
#define SCRATCH_BUF_SIZE 512

uint8_t packet_buf[PACKET_BUF_SIZE];
uint8_t read_buf[READ_BUF_SIZE];
uint8_t scratch_buf[SCRATCH_BUF_SIZE];

uint16_t scratch_buf_index = 0;

typedef struct __attribute__((__packed__)) {
	uint8_t direction;
	uint8_t command;
	uint16_t data_len;
	uint32_t value_or_checksum;
} packet_header_t;

packet_header_t * packet_buf_header = (packet_header_t *)(packet_buf + 1);

void hexdump(uint8_t * data, size_t data_len) {
	const uint8_t bytes_per_line = 16;

	size_t lines = (data_len + (bytes_per_line - 1)) / bytes_per_line;

	for (size_t line_i = 0; line_i < lines; line_i++) {
		uint8_t bytes_on_this_line = bytes_per_line;

		if (line_i == lines - 1) {
			bytes_on_this_line = data_len % bytes_per_line;
		}

		uint8_t spacer_bytes = (bytes_per_line - bytes_on_this_line);

		printf("| ");

		for (size_t byte_i = 0; byte_i < bytes_on_this_line; byte_i++) {
			printf("%02x ", data[(line_i*bytes_per_line) + byte_i]);
		}

		for (size_t spacer_i = 0; spacer_i < spacer_bytes; spacer_i++) {
			printf("   ");
		}

		printf("| ");

		for (size_t byte_i = 0; byte_i < bytes_on_this_line; byte_i++) {
			uint8_t byte = data[(line_i*bytes_per_line) + byte_i];
			if (byte > 32 && byte < 128) {
				putchar(byte);
			} else {
				putchar('.');
			}
		}

		for (size_t spacer_i = 0; spacer_i < spacer_bytes; spacer_i++) {
			printf(" ");
		}

		printf(" |\n");
	}	
}

uint16_t read_packet(int port_fd) {
	// check current scratch data
	int found_packet = 0;

	size_t packet_start_index = 0;

	// hexdump(scratch_buf, scratch_buf_index);

	for (size_t scan_index = 0; scan_index < scratch_buf_index; scan_index++) {
		if (scratch_buf[scan_index] == 0xC0) {
			if (found_packet == 0) {
				// found start of packet
				found_packet = 1;
				packet_start_index = scan_index;
			} else if (found_packet == 1) {
				// found end of packet
				size_t packet_end_index = scan_index;
				size_t packet_length = (packet_end_index - packet_start_index) + 1;

				// 0  1  2  3  4  5  6  7  8  9  10 11 12 13 14 16 16 17
				// c0 01 02 03 04 05 06 07 c0 c0 90 80 70 60 50 40 c0 XX
				// packet_start_index: 0
				// packet_end_index: 8
				
				// copy contents of scratch buf to packet buf
				memcpy(packet_buf, scratch_buf, packet_length);

				// copy remainder of scratch data
				memcpy(scratch_buf, scratch_buf + packet_start_index, packet_length);
				scratch_buf_index -= packet_length;

				// debugging
				printf("got packet (dir=%d, cmd=%d, len=%d, val=%d)\n",
					packet_buf_header->direction,
					packet_buf_header->command,
					packet_buf_header->data_len,
					packet_buf_header->value_or_checksum);

				return packet_length;
			}
		}
	}

	// read new data
	uint8_t read_buf[READ_BUF_SIZE];
	ssize_t n = read(port_fd, read_buf, READ_BUF_SIZE);
	if (n == -1) {
		if (errno == EWOULDBLOCK) {
			// no data yet
			return 0;
		} else {
			printf("errno %d\n", errno);
			exit(1);
			return 0;
		}
	}
	
	// printf("read %zd bytes\n", n);

	// copy read buf data into scratch buf
	memcpy((scratch_buf + scratch_buf_index), read_buf, n);
	scratch_buf_index += n;

	return 0;
}

size_t build_packet(uint8_t * buffer, size_t buffer_max_size, packet_header_t * header, uint8_t * data, size_t data_len) {
	if (buffer_max_size < 10 + data_len) {
		return 0;
	}

	header->data_len = data_len;

	size_t packet_size = 9;

	// start + header
	buffer[0] = 0xC0;
	memcpy(buffer + 1, header, 8);

	// data
	if (data != NULL) {
		memcpy(buffer + packet_size, data, data_len);
		packet_size += data_len;
	}

	// end
	buffer[packet_size] = 0xC0;
	packet_size++;

	return packet_size;
}

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