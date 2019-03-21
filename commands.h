#ifndef _COMMANDS_H
#define _COMMANDS_H

#include <stdint.h>
#include <stdio.h>

#include "debug.h"
#include "esp32.h"
#include "protocol.h"
#include "reader.h"

uint32_t read_reg(int port_fd, uint32_t address);
uint32_t read_efuse(int port_fd, uint8_t i);
void print_chip_info(int port_fd);

#endif