#include "commands.h"

#define SEND_PACKET_BUF_SIZE 128 + ESP_MAX_RAM_BLOCK_SIZE
#define DATA_SCRATCH_BUF_SIZE 16 + ESP_MAX_RAM_BLOCK_SIZE

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

void ram_download_start(int port_fd, size_t data_len, size_t block_count, size_t block_size, uint32_t offset) {
	uint8_t mem_begin_data[16];

	memcpy(mem_begin_data, (uint8_t *) &data_len, 4);
	memcpy(mem_begin_data + 4, (uint8_t *) &block_count, 4);
	memcpy(mem_begin_data + 8, (uint8_t *) &block_size, 4);
	memcpy(mem_begin_data + 12, (uint8_t *) &offset, 4);

	packet_header_t mem_begin_packet_header = {
		.direction = 0x0,
		.command = ESP_MEM_BEGIN,
		.data_len = 0x0,
		.value_or_checksum = 0x0,
	};
	size_t mem_begin_packet_size = build_packet(send_packet_buf, SEND_PACKET_BUF_SIZE, &mem_begin_packet_header, mem_begin_data, 16);

	printf("ram_download_start\n");
	hexdump(send_packet_buf, mem_begin_packet_size);

	write_packet_data(port_fd, send_packet_buf, mem_begin_packet_size);

	wait_for_response(port_fd, ESP_MEM_BEGIN);
}

void ram_download_block(int port_fd, uint32_t seq, const uint8_t * data, size_t data_len) {
	if (data_len > ESP_MAX_RAM_BLOCK_SIZE) {
		return;
	}

	// set up data header and copy packet data to scratch buf
	memset(data_scratch_buf, '\0', 16);
	memcpy(data_scratch_buf, (uint8_t *) &data_len, 4);
	memcpy(data_scratch_buf + 4, (uint8_t *) &seq, 4);
	memcpy(data_scratch_buf + 16, data, data_len);

	packet_header_t mem_data_packet_header = {
		.direction = 0x0,
		.command = ESP_MEM_DATA,
		.data_len = 0x0,
		.value_or_checksum = checksum(data, data_len),
	};
	size_t mem_data_packet_size = build_packet(send_packet_buf, SEND_PACKET_BUF_SIZE, &mem_data_packet_header, data_scratch_buf, 16 + data_len);

	printf("ram_download_block\n");
	hexdump(send_packet_buf, mem_data_packet_size);

	write_packet_data(port_fd, send_packet_buf, mem_data_packet_size);

	wait_for_response(port_fd, ESP_MEM_DATA);
}

void ram_download_end(int port_fd, uint32_t entrypoint) {
	uint8_t mem_end_data[8];

	uint32_t no_jump_to_entrypoint = 0;
	if (entrypoint == 0) {
		no_jump_to_entrypoint = 1;
	}

	memcpy(mem_end_data, (uint8_t *) &no_jump_to_entrypoint, 4);
	memcpy(mem_end_data + 4, (uint8_t *) &entrypoint, 4);

	packet_header_t mem_end_packet_header = {
		.direction = 0x0,
		.command = ESP_MEM_END,
		.data_len = 0x0,
		.value_or_checksum = 0x0,
	};
	size_t mem_end_packet_size = build_packet(send_packet_buf, SEND_PACKET_BUF_SIZE, &mem_end_packet_header, mem_end_data, 8);

	printf("ram_download_end\n");
	hexdump(send_packet_buf, mem_end_packet_size);

	write_packet_data(port_fd, send_packet_buf, mem_end_packet_size);

	// we do NOT wait for a response at the end of this because that response could be corrupted if/when the chip resets
	// it's up to the caller to wait for a signal from the code on the chip, if necessary
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
	write_packet_data(port_fd, send_packet_buf, read_reg_packet_size);
	write_packet_data(port_fd, send_packet_buf, read_reg_packet_size);
	write_packet_data(port_fd, send_packet_buf, read_reg_packet_size);
	write_packet_data(port_fd, send_packet_buf, read_reg_packet_size);

	wait_for_response(port_fd, ESP_READ_REG);

	printf("superwait\n");
	while (1) {
		wait_for_response(port_fd, ESP_READ_REG);
	}

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