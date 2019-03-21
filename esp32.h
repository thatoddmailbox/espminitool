#ifndef _ESP32_H
#define _ESP32_H

#include <stddef.h>
#include <stdint.h>

#define ESP32_EFUSE_REG_BASE 0x6001a000

#define ESP32_STUB_ENTRY 0x4009F564

typedef struct {
	uint32_t data_start;
	size_t data_length;
	const uint8_t data[];
} esp_stub_segment_t;

extern const esp_stub_segment_t ESP32_STUB_DATA;
extern const esp_stub_segment_t ESP32_STUB_TEXT;

#endif