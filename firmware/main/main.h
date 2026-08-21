#ifndef MAIN_H
#define MAIN_H

#include <stdint.h>

#define CMD_CHR_BUF_SIZE 32

uint8_t* get_cmd_chr_data(void);
void ble_send_telemetry(const char *data);
uint16_t ble_get_conn_handle(void);

#endif