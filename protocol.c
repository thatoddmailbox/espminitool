#include "protocol.h"

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