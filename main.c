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

typedef struct {
	char * file_name;
	uint32_t offset;
} file_offset_pair_t;

void flash_callback(uint8_t * data, uint16_t size, void * metadata) {
	FILE * fp = (FILE *) metadata;
	fwrite(data, sizeof(uint8_t), size, fp);
}

uint8_t * read_file_bytes(char * name, size_t * size_out) {
	FILE * file_pointer = fopen(name, "r");

	// get file size
	long file_size = 0;
	fseek(file_pointer, 0, SEEK_END);
	file_size = ftell(file_pointer);
	rewind(file_pointer);

	if (size_out) {
		*size_out = file_size;
	}

	// create buffer
	uint8_t * buffer = calloc(file_size, sizeof(uint8_t));

	// read data
	fread(buffer, sizeof(uint8_t), file_size, file_pointer);

	fclose(file_pointer);

	return buffer;
}

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

	// FILE * data_file = fopen("data.bin", "w");
	// read_flash(port_fd, 0x10000, 1 * 1024 * 1024, ESP_FLASH_SECTOR_SIZE, 64, (void *) data_file, flash_callback);
	// fclose(data_file);

	file_offset_pair_t files[] = {
		{ .file_name = "bootloader.bin", .offset = 0x1000 },
		{ .file_name = "hello-world.bin", .offset = 0x10000 },
		{ .file_name = "partitions_singleapp.bin", .offset = 0x8000 }
	};

	size_t file_count = sizeof(files) / sizeof(file_offset_pair_t);

	for (size_t i = 0; i < file_count; i++) {
		file_offset_pair_t file = files[i];
		printf("%s - 0x%x\n", file.file_name, file.offset);

		size_t file_size;
		uint8_t * file_contents = read_file_bytes(file.file_name, &file_size);

		write_flash(port_fd, file.offset, file_contents, file_size, ESP_MAX_FLASH_BLOCK_SIZE);

		free(file_contents);
	}

	md5_flash(port_fd, 0x8000, 0xC00);

	flash_download_end(port_fd, 1);

	close(port_fd);

	printf("closed\n");

	return 0;
}