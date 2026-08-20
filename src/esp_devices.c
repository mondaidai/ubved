#include "esp_devices.h"
#include <libserialport.h>
#include <stdbool.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>

static bool is_esp_device(uint16_t vid, uint16_t pid)
{
	static uint16_t target_vid_pid_list[][2] = TARGET_VID_PID_LIST;
	size_t count = sizeof(target_vid_pid_list) / sizeof(target_vid_pid_list[0]);

	for (int i = 0; i < (int)count; i++) {
		if(target_vid_pid_list[i][0] == vid &&
		    target_vid_pid_list[i][1] == pid)
			return true;
	}
	return false;
}

const char * esp_error_string(enum sp_return result)
{
	switch (result) {
	case SP_OK:
		return "Operation completed successfully.";
	case SP_ERR_ARG:
		return "Invalid arguments were passed to the function.";
	case SP_ERR_FAIL:
		return "A system error occurred while executing the operation.";
	case SP_ERR_MEM:
		return "A memory allocation failed while executing the operation.";
	case SP_ERR_SUPP:
		return "The requested operation is not supported by this system or device.";
	default:
		return "Unknown error";
	}
}

int esp_get_devices(EspDeviceList * edl)
{
	if(edl == NULL)
		return SP_ERR_ARG;

	edl->esps = NULL;
	edl->count = 0;

	struct sp_port ** port_list = NULL;
	enum sp_return result = sp_list_ports(&port_list);
	if(result != SP_OK)
		return result;

	size_t esp_devices_found = 0;
	int vid, pid;
	for (int i = 0; port_list[i] != NULL; i++) {
		result = sp_get_port_usb_vid_pid(port_list[i], &vid, &pid);
		if(result != SP_OK)
			continue;
		if(is_esp_device((uint16_t)vid, (uint16_t)pid))
			esp_devices_found++;
	}
	if(!esp_devices_found) {
		result = SP_OK;
		goto cleanup;
	}

	EspDevice * esps = malloc(esp_devices_found * sizeof(EspDevice));
	if(!esps) {
		result = SP_ERR_MEM;
		goto cleanup;
	}

	edl->esps = esps;
	edl->count = 0;
	for (int i = 0; port_list[i] != NULL; i++) {
		result = sp_get_port_usb_vid_pid(port_list[i], &vid, &pid);
		if(result != SP_OK)
			continue;
		if(is_esp_device((uint16_t)vid, (uint16_t)pid)) {
			const char * port_name = sp_get_port_name(port_list[i]);
			if(!port_name)
				continue;
			esps[edl->count].port = strdup(port_name);
			if(!esps[edl->count].port) {
				result = SP_ERR_MEM;
				goto cleanup;
			}
			esps[edl->count].pid = (uint16_t)pid;
			esps[edl->count].vid = (uint16_t)vid;
			edl->count++;
		}
	}

	result = SP_OK;

cleanup:
	if(result != SP_OK)
		esp_free_devices(edl);
	if(port_list)
		sp_free_port_list(port_list);
	return result;
}

void esp_free_devices(EspDeviceList * edl)
{
	if(edl == NULL)
		return;
	for (size_t i = 0; i < edl->count; i++) {
		free(edl->esps[i].port);
	}
	free(edl->esps);
	memset(edl, 0, sizeof(*edl));
}

int esp_snd_rcv_message(const char * portname, const char * msg,
							char * recv_buf, size_t recv_buf_size)
{
	enum sp_return result = SP_OK;
	struct sp_port * port = NULL;

	if(portname == NULL || msg == NULL || recv_buf == NULL || recv_buf_size == 0)
		return SP_ERR_ARG;

	recv_buf[0] = '\0';

	result = sp_get_port_by_name(portname, &port);
	if(result != SP_OK) {
		return result;
	}

	result = sp_open(port, SP_MODE_READ_WRITE);
	if(result != SP_OK) {
		sp_free_port(port);
		return result;
	}

	if((result = sp_set_baudrate(port, 9600)) != SP_OK)
		goto cleanup;
	if((result = sp_set_bits(port, 8)) != SP_OK)
		goto cleanup;
	if((result = sp_set_parity(port, SP_PARITY_NONE)) != SP_OK)
		goto cleanup;
	if((result = sp_set_stopbits(port, 1)) != SP_OK)
		goto cleanup;
	if((result = sp_set_flowcontrol(port, SP_FLOWCONTROL_NONE)) != SP_OK)
		goto cleanup;

	sp_flush(port, SP_BUF_INPUT);
	
	size_t total = 0;
	size_t len = strlen(msg);

	while(total < len) {
		int written = sp_blocking_write(
			port,
			msg + total,
			len - total,
			PORT_TIMEOUT_MS);

	if(written < 0) {
		result = written;
		goto cleanup;
	}

	if(written == 0) {
		result = SP_ERR_FAIL;
		goto cleanup;
	}

		total += written;
	}
	size_t sent = 0;
	while(sent < recv_buf_size - 1) {
		int n = sp_blocking_read(
			port,
			recv_buf + sent,
			recv_buf_size - 1 - sent,
			PORT_TIMEOUT_MS);

		if(n < 0) {
			result = n;
			goto cleanup;
		}

		if(n == 0) {
			result = SP_ERR_FAIL; /* timeout */
			goto cleanup;
		}

		sent += n;
		recv_buf[sent] = '\0';

		if(strstr(recv_buf, "\r\n"))
			break;
	}

	result = SP_OK;

cleanup:
	if(result != SP_OK)
		recv_buf[0] = '\0';
	if(port) {
		sp_close(port);
		sp_free_port(port);
	}
	return result;
}
