#include "commands.h"

#define SEND_PACKET_BUF_SIZE 128 + ESP_MAX_FLASH_BLOCK_SIZE
#define DATA_SCRATCH_BUF_SIZE 16 + ESP_MAX_FLASH_BLOCK_SIZE

uint8_t data_scratch_buf[DATA_SCRATCH_BUF_SIZE];
uint8_t send_packet_buf[SEND_PACKET_BUF_SIZE];

ssize_t find_escape_character(uint8_t * data, size_t data_len) {
	for (size_t i = 0; i < data_len; i++) {
		if (data[i] == (uint8_t) '\xC0' || data[i] == (uint8_t) '\xDB') {
			return i;
		}
	}
	return -1;
}

ssize_t write_packet_data(int port_fd, uint8_t * data, size_t data_len) {
	write(port_fd, "\xC0", 1);

	// printf("write_packet_data (len == %zu)\n", data_len);

	size_t total_data_sent = 0;
	while (total_data_sent < data_len) {
		size_t bytes_left = data_len - total_data_sent;

		ssize_t next_escape_character = find_escape_character(data + total_data_sent, bytes_left);

		if (data[total_data_sent] == (uint8_t) '\xC0') {
			write(port_fd, "\xDB\xDC", 2);
			total_data_sent++;
			bytes_left--;
			continue;
		} else if (data[total_data_sent] == (uint8_t) '\xDB') {
			write(port_fd, "\xDB\xDD", 2);
			total_data_sent++;
			bytes_left--;
			continue;
		}

		if (next_escape_character >= 0) {
			bytes_left = next_escape_character;
		}
		ssize_t data_sent = write(port_fd, data + total_data_sent, bytes_left);
		if (data_sent < 0) {
			if (errno == EAGAIN || errno == EWOULDBLOCK) {
				// that's ok, just keep trying
				continue;
			} else {
				return -1;
			}
		}

		// printf("chunk (tds=%zu, left=%zu, total=%zu)\n", total_data_sent, bytes_left, data_len);
		// hexdump(data + total_data_sent, bytes_left);

		total_data_sent += data_sent;
	}

	write(port_fd, "\xC0", 1);

	return total_data_sent;
}

void wait_for_response(int port_fd, uint8_t command) {
	while (1) {
		uint16_t packet_size = read_packet(port_fd);
		if (packet_size > 0) {
			hexdump(packet_buf, packet_size);
			if (packet_buf_header->command == command) {
				return;
			}
		}
	}
}

uint8_t sync_chip(int port_fd) {
	packet_header_t sync_packet_header = {
		.direction = 0x0,
		.command = ESP_SYNC,
		.data_len = 0x0,
		.value_or_checksum = 0x0,
	};
	uint8_t sync_packet_data[36] = "\x07\x07\x12\x20\x55\x55\x55\x55\x55\x55\x55\x55\x55\x55\x55\x55\x55\x55\x55\x55\x55\x55\x55\x55\x55\x55\x55\x55\x55\x55\x55\x55\x55\x55\x55\x55";
	uint8_t sync_packet_data_len = 36;
	size_t sync_packet_size = build_packet(send_packet_buf, SEND_PACKET_BUF_SIZE, &sync_packet_header, sync_packet_data, sync_packet_data_len);

	hexdump(send_packet_buf, sync_packet_size);

	// write sync packet two times to warm up autobauder
	write_packet_data(port_fd, send_packet_buf, sync_packet_size);
	write_packet_data(port_fd, send_packet_buf, sync_packet_size);

	// read stuff
	uint8_t sync_count = 0;
	clock_t last_sync_packet = 0;
	uint8_t talking_to_stub = 0;
	while (1) {
		uint16_t packet_size = read_packet(port_fd);
		if (packet_size > 0) {
			if (packet_buf_header->command == ESP_SYNC) {
				if (packet_buf_header->data_len == 2) {
					// on the esp32, the stub sends packets with 2 data bytes, while the rom loader does 4 bytes
					talking_to_stub = 1;
				}
				last_sync_packet = clock();
				sync_count++;
			}
		}

		uint8_t packets_per_sync = (talking_to_stub ? 1 : 4);

		if (sync_count >= packets_per_sync) {
			// we've got at least one sync's worth of packets
			// maybe we're done?
			if ((clock() - last_sync_packet) > 0.25 * CLOCKS_PER_SEC) {
				break;
			}
		}
	}

	return talking_to_stub;
}

void download_start(int port_fd, uint8_t command, size_t data_len, size_t block_count, size_t block_size, uint32_t offset) {
	uint8_t mem_begin_data[16];

	memcpy(mem_begin_data, (uint8_t *) &data_len, 4);
	memcpy(mem_begin_data + 4, (uint8_t *) &block_count, 4);
	memcpy(mem_begin_data + 8, (uint8_t *) &block_size, 4);
	memcpy(mem_begin_data + 12, (uint8_t *) &offset, 4);

	packet_header_t mem_begin_packet_header = {
		.direction = 0x0,
		.command = command,
		.data_len = 0x0,
		.value_or_checksum = 0x0,
	};
	size_t mem_begin_packet_size = build_packet(send_packet_buf, SEND_PACKET_BUF_SIZE, &mem_begin_packet_header, mem_begin_data, 16);

	printf("download_start\n");
	hexdump(send_packet_buf, mem_begin_packet_size);

	write_packet_data(port_fd, send_packet_buf, mem_begin_packet_size);

	wait_for_response(port_fd, command);
}

void download_block(int port_fd, uint8_t command, uint32_t seq, const uint8_t * data, size_t data_len) {
	// set up data header and copy packet data to scratch buf
	memset(data_scratch_buf, '\0', 16);

	uint32_t block_size = data_len;
	if (command == ESP_FLASH_DATA) {
		// has to pad
		block_size = ESP_MAX_FLASH_BLOCK_SIZE;
		memset(data_scratch_buf + 16, '\xFF', block_size);
	}

	memcpy(data_scratch_buf, (uint8_t *) &block_size, 4);
	memcpy(data_scratch_buf + 4, (uint8_t *) &seq, 4);
	memcpy(data_scratch_buf + 16, data, data_len);

	packet_header_t mem_data_packet_header = {
		.direction = 0x0,
		.command = command,
		.data_len = 0x0,
		.value_or_checksum = checksum(data, data_len),
	};
	size_t mem_data_packet_size = build_packet(send_packet_buf, SEND_PACKET_BUF_SIZE, &mem_data_packet_header, data_scratch_buf, 16 + block_size);

	printf("download_block\n");
	// hexdump(send_packet_buf, mem_data_packet_size);

	write_packet_data(port_fd, send_packet_buf, mem_data_packet_size);

	wait_for_response(port_fd, command);
}

void download_end(int port_fd, uint8_t command, uint32_t entrypoint) {
	uint8_t mem_end_data[8];

	uint32_t no_jump_to_entrypoint = 0;
	if (entrypoint == 0) {
		no_jump_to_entrypoint = 1;
	}

	memcpy(mem_end_data, (uint8_t *) &no_jump_to_entrypoint, 4);
	memcpy(mem_end_data + 4, (uint8_t *) &entrypoint, 4);

	packet_header_t mem_end_packet_header = {
		.direction = 0x0,
		.command = command,
		.data_len = 0x0,
		.value_or_checksum = 0x0,
	};
	size_t mem_end_packet_size = build_packet(send_packet_buf, SEND_PACKET_BUF_SIZE, &mem_end_packet_header, mem_end_data, 8);

	printf("download_end\n");
	hexdump(send_packet_buf, mem_end_packet_size);

	write_packet_data(port_fd, send_packet_buf, mem_end_packet_size);

	// we do NOT wait for a response at the end of this because that response could be corrupted if/when the chip resets
	// it's up to the caller to wait for a signal from the code on the chip, if necessary
}

void ram_download_start(int port_fd, size_t data_len, size_t block_count, size_t block_size, uint32_t offset) {
	download_start(port_fd, ESP_MEM_BEGIN, data_len, block_count, block_size, offset);
}

void ram_download_block(int port_fd, uint32_t seq, const uint8_t * data, size_t data_len) {
	download_block(port_fd, ESP_MEM_DATA, seq, data, data_len);
}

void ram_download_end(int port_fd, uint32_t entrypoint) {
	download_end(port_fd, ESP_MEM_END, entrypoint);
}

void flash_download_start(int port_fd, size_t data_len, size_t block_count, size_t block_size, uint32_t offset) {
	download_start(port_fd, ESP_FLASH_BEGIN, data_len, block_count, block_size, offset);
}

void flash_download_block(int port_fd, uint32_t seq, const uint8_t * data, size_t data_len) {
	download_block(port_fd, ESP_FLASH_DATA, seq, data, data_len);
}

void flash_download_end(int port_fd, uint32_t entrypoint) {
	download_end(port_fd, ESP_FLASH_END, entrypoint);
}

uint32_t read_reg(int port_fd, uint32_t address) {
	packet_header_t read_reg_packet_header = {
		.direction = 0x0,
		.command = ESP_READ_REG,
		.data_len = 0x0,
		.value_or_checksum = 0x0,
	};
	size_t read_reg_packet_size = build_packet(send_packet_buf, SEND_PACKET_BUF_SIZE, &read_reg_packet_header, (uint8_t *) &address, 4);

	printf("read reg pkt\n");
	hexdump(send_packet_buf, read_reg_packet_size);

	write_packet_data(port_fd, send_packet_buf, read_reg_packet_size);

	wait_for_response(port_fd, ESP_READ_REG);

	return packet_buf_header->value_or_checksum;
}

uint32_t read_efuse(int port_fd, uint8_t i) {
	return read_reg(port_fd, ESP32_EFUSE_REG_BASE + (4 * i));
}

void print_chip_info(int port_fd) {
	// read chip info
	uint32_t efuse1 = read_efuse(port_fd, 1);
	uint32_t efuse2 = read_efuse(port_fd, 2);
	uint32_t efuse3 = read_efuse(port_fd, 3);
	// uint32_t efuse4 = read_efuse(port_fd, 4);
	// uint32_t efuse6 = read_efuse(port_fd, 6);

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
}

void download_stub(int port_fd) {
	ram_download_start(port_fd, ESP32_STUB_TEXT.data_length, 1, ESP32_STUB_TEXT.data_length, ESP32_STUB_TEXT.data_start);
	ram_download_block(port_fd, 0, ESP32_STUB_TEXT.data, ESP32_STUB_TEXT.data_length);

	ram_download_start(port_fd, ESP32_STUB_DATA.data_length, 1, ESP32_STUB_DATA.data_length, ESP32_STUB_DATA.data_start);
	ram_download_block(port_fd, 0, ESP32_STUB_DATA.data, ESP32_STUB_DATA.data_length);

	ram_download_end(port_fd, ESP32_STUB_ENTRY);

	while (1) {
		uint16_t packet_size = read_packet(port_fd);
		if (packet_size > 0) {
			hexdump(packet_buf, packet_size);
			if (packet_size == 6) {
				// could it be?
				if (strncmp((const char *) packet_buf + 1, "OHAI", 4) == 0) {
					// stub loader is running
					break;
				}
			}
		}
	}

	printf("stub downloaded and running\n");
}

void read_flash(int port_fd, uint32_t offset, uint32_t length, uint32_t block_size, uint32_t max_blocks_in_flight, void * metadata, void (*callback)(uint8_t *, uint16_t, void *)) {
	uint8_t read_flash_data[16];

	memcpy(read_flash_data, (uint8_t *) &offset, 4);
	memcpy(read_flash_data + 4, (uint8_t *) &length, 4);
	memcpy(read_flash_data + 8, (uint8_t *) &block_size, 4);
	memcpy(read_flash_data + 12, (uint8_t *) &max_blocks_in_flight, 4);

	packet_header_t read_flash_packet_header = {
		.direction = 0x0,
		.command = ESP_READ_FLASH,
		.data_len = 0x0,
		.value_or_checksum = 0x0,
	};
	size_t read_flash_packet_size = build_packet(send_packet_buf, SEND_PACKET_BUF_SIZE, &read_flash_packet_header, read_flash_data, 16);

	hexdump(send_packet_buf, read_flash_packet_size);

	write_packet_data(port_fd, send_packet_buf, read_flash_packet_size);

	uint8_t got_start_packet = 0;
	uint32_t bytes_received = 0;

	while (1) {
		uint16_t packet_size = read_packet(port_fd);
		if (packet_size > 0) {
			if (bytes_received < length) {
				// there's still more
				if (!got_start_packet) {
					got_start_packet = 1;
					continue;
				}

				uint32_t data_size = packet_size - 2;

				printf("GOT BLOCK OF SIZE %d\n", data_size);

				// weird addition and subtraction are to account for the 0xC0 bytes
				callback(packet_buf + 1, packet_size - 2, metadata);

				bytes_received += data_size;

				// send an acknowledgement
				uint8_t acknowledgement[4];
				memcpy(acknowledgement, (uint8_t *) &bytes_received, 4);
				write_packet_data(port_fd, acknowledgement, 4);

				hexdump(acknowledgement, 4);
			} else {
				// we're done
				printf("got checksum?\n");
				hexdump(packet_buf, packet_size);
				break;
			}
		}
	}
}

void write_flash(int port_fd, uint32_t offset, uint8_t * data, uint32_t length, uint32_t block_size) {
	uint32_t block_count = length / block_size;
	if (length % block_size != 0) {
		block_count += 1;
	}

	flash_download_start(port_fd, length, block_count, block_size, offset);
	for (size_t i = 0; i < block_count; i++) {
		uint32_t current_block_size = block_size;
		if (i == block_count - 1) {
			current_block_size = length % block_size;
			if (current_block_size == 0) {
				current_block_size = block_size;
			}
		}
		flash_download_block(port_fd, i, data + (i * block_size), current_block_size);
	}
}

void md5_flash(int port_fd, uint32_t offset, uint32_t length) {
	uint8_t md5_flash_data[16];

	memset(md5_flash_data, '\0', 16);
	memcpy(md5_flash_data, (uint8_t *) &offset, 4);
	memcpy(md5_flash_data + 4, (uint8_t *) &length, 4);

	packet_header_t md5_flash_packet_header = {
		.direction = 0x0,
		.command = ESP_SPI_FLASH_MD5,
		.data_len = 0x0,
		.value_or_checksum = 0x0,
	};
	size_t md5_flash_packet_size = build_packet(send_packet_buf, SEND_PACKET_BUF_SIZE, &md5_flash_packet_header, md5_flash_data, 16);

	hexdump(send_packet_buf, md5_flash_packet_size);

	write_packet_data(port_fd, send_packet_buf, md5_flash_packet_size);

	wait_for_response(port_fd, ESP_SPI_FLASH_MD5);
	printf("yayyayayay\n");
}