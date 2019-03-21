#include "debug.h"

void hexdump(uint8_t * data, size_t data_len) {
	const uint8_t bytes_per_line = 16;

	size_t lines = (data_len + (bytes_per_line - 1)) / bytes_per_line;

	for (size_t line_i = 0; line_i < lines; line_i++) {
		uint8_t bytes_on_this_line = bytes_per_line;

		if (line_i == lines - 1) {
			bytes_on_this_line = data_len % bytes_per_line;
			if (bytes_on_this_line == 0) {
				bytes_on_this_line = bytes_per_line;
			}
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