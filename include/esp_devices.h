#ifndef SERIAL_DEVICES_H
#define SERIAL_DEVICES_H

#include <libserialport.h>
#include <stdint.h>

#define TARGET_VID_PID_LIST {{4292, 60000}}
#define PORT_TIMEOUT_MS 5000

typedef struct {
	uint16_t vid;
	uint16_t pid;
	char *   port;
} EspDevice;

typedef struct {
	EspDevice * esps;
	size_t  count;
} EspDeviceList;

const char * esp_error_string(enum sp_return result);

int esp_get_devices(EspDeviceList * edl);

int esp_snd_rcv_message(const char * portname, const char * msg,
						char * recv_buf, size_t recv_buf_size);

void esp_free_devices(EspDeviceList * edl);

#endif