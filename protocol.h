#ifndef _PROTOCOL_H
#define _PROTOCOL_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

// constants
#define ESP_SYNC 0x08
#define ESP_WRITE_REG 0x09
#define ESP_READ_REG 0x0a
#define ESP_SPI_SET_PARAMS 0x0b

// structs
typedef struct __attribute__((__packed__)) {
	uint8_t direction;
	uint8_t command;
	uint16_t data_len;
	uint32_t value_or_checksum;
} packet_header_t;

// functions
size_t build_packet(uint8_t * buffer, size_t buffer_max_size, packet_header_t * header, uint8_t * data, size_t data_len);

#endif