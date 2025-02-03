#include "reader.h"

uint8_t read_buf[READ_BUF_SIZE];
uint8_t scratch_buf[SCRATCH_BUF_SIZE];

uint16_t scratch_buf_index = 0;

uint8_t packet_buf[PACKET_BUF_SIZE];
packet_header_t * packet_buf_header = (packet_header_t *)(packet_buf + 1);

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