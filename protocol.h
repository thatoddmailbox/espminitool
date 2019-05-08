#ifndef _PROTOCOL_H
#define _PROTOCOL_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

// constants
#define ESP_FLASH_BEGIN 0x02
#define ESP_FLASH_END 0x03
#define ESP_FLASH_DATA 0x04
#define ESP_MEM_BEGIN 0x05
#define ESP_MEM_END 0x06
#define ESP_MEM_DATA 0x07
#define ESP_SYNC 0x08
#define ESP_WRITE_REG 0x09
#define ESP_READ_REG 0x0a
#define ESP_SPI_SET_PARAMS 0x0b
#define ESP_SPI_ATTACH 0x0d
#define ESP_READ_FLASH 0xd2

#define ESP_FLASH_SECTOR_SIZE 0x1000
#define ESP_MAX_FLASH_BLOCK_SIZE 0x400
#define ESP_MAX_RAM_BLOCK_SIZE 0x1800

// structs
typedef struct __attribute__((__packed__)) {
	uint8_t direction;
	uint8_t command;
	uint16_t data_len;
	uint32_t value_or_checksum;
} packet_header_t;

// functions
uint8_t checksum(const uint8_t * data, size_t data_len);
size_t build_packet(uint8_t * buffer, size_t buffer_max_size, packet_header_t * header, uint8_t * data, size_t data_len);

#endif