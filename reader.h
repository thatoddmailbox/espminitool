#ifndef _READER_H
#define _READER_H

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "protocol.h"

#define READ_BUF_SIZE 128
#define PACKET_BUF_SIZE 256
#define SCRATCH_BUF_SIZE 512

uint8_t packet_buf[PACKET_BUF_SIZE];
extern packet_header_t * packet_buf_header;

uint16_t read_packet(int port_fd);

#endif