#ifndef _COMMANDS_H
#define _COMMANDS_H

#include <stdint.h>
#include <stdio.h>
#include <time.h>

#include "debug.h"
#include "esp32.h"
#include "protocol.h"
#include "reader.h"

ssize_t write_packet_data(int port_fd, uint8_t * data, size_t data_len);
void wait_for_response(int port_fd, uint8_t command);

void ram_download_start(int port_fd, size_t data_len, size_t block_count, size_t block_size, uint32_t offset);
void ram_download_block(int port_fd, uint32_t seq, const uint8_t * data, size_t data_len);
void ram_download_end(int port_fd, uint32_t entrypoint);
uint32_t read_reg(int port_fd, uint32_t address);
uint32_t read_efuse(int port_fd, uint8_t i);

uint8_t sync_chip(int port_fd);
void print_chip_info(int port_fd);
void download_stub(int port_fd);

void read_flash(int port_fd, uint32_t offset, uint32_t length, uint32_t block_size, uint32_t max_blocks_in_flight, void * metadata, void (*callback)(uint8_t *, uint16_t, void *));

#endif