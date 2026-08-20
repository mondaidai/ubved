#include "ubus_api.h"
#include "esp_devices.h"

#include <libubus.h>
#include <libubox/blobmsg_json.h>
#include <syslog.h>

static int get_devices(struct ubus_context * ctx, struct ubus_object * obj,
					  struct ubus_request_data * req, const char * method, struct blob_attr * msg);

static int set_pin_on_off(struct ubus_context * ctx, struct ubus_object * obj,
						  struct ubus_request_data * req, const char * method, struct blob_attr * msg);

static int get_device_data(struct ubus_context * ctx, struct ubus_object * obj,
						  struct ubus_request_data * req, const char * method, struct blob_attr * msg);

enum {
	DEVICES_ON_OFF_PORT,
	DEVICES_ON_OFF_PIN,
	__DEVICES_ON_OFF_MAX
};

const struct blobmsg_policy devices_on_off_policy[] = {
	[DEVICES_ON_OFF_PORT] = {.name = "port", .type = BLOBMSG_TYPE_STRING},
	[DEVICES_ON_OFF_PIN] = {.name = "pin", .type = BLOBMSG_TYPE_INT32}
};

enum {
	DEVICES_GET_PORT,
	DEVICES_GET_PIN,
	DEVICES_GET_MODEL,
	DEVICES_GET_SENSOR,
	__DEVICES_GET_MAX
};

const struct blobmsg_policy devices_get_policy[] = {
	[DEVICES_GET_PORT] = {.name = "port", .type = BLOBMSG_TYPE_STRING},
	[DEVICES_GET_PIN] = {.name = "pin", .type = BLOBMSG_TYPE_INT32},
	[DEVICES_GET_MODEL] = {.name = "model", .type = BLOBMSG_TYPE_STRING},
	[DEVICES_GET_SENSOR] = {.name = "sensor", .type = BLOBMSG_TYPE_STRING}
};

static struct ubus_method device_methods[] = {
	UBUS_METHOD_NOARG("devices", get_devices),
	UBUS_METHOD("on", set_pin_on_off, devices_on_off_policy),
	UBUS_METHOD("off", set_pin_on_off, devices_on_off_policy),
	UBUS_METHOD("get", get_device_data, devices_get_policy),
};

static struct ubus_object_type device_object_type =
	UBUS_OBJECT_TYPE("ubved", device_methods);

static struct ubus_object device_object = {
	.name = "ubved",
	.type = &device_object_type,
	.methods = device_methods,
	.n_methods = ARRAY_SIZE(device_methods),
};

static void device_to_blobmsg(struct blob_buf * b, const EspDevice * device)
{
	char buf[7];
	blobmsg_add_string(b, "port", device->port);
	snprintf(buf, sizeof(buf), "%#X", device->vid);
	blobmsg_add_string(b, "vendor_id", buf);
	snprintf(buf, sizeof(buf), "%#X", device->pid);
	blobmsg_add_string(b, "product_id", buf);
}

static int get_devices(struct ubus_context * ctx, struct ubus_object * obj,
					  struct ubus_request_data * req, const char * method, struct blob_attr * msg)
{
	EspDeviceList devices;
	int ret = esp_get_devices(&devices);
	if(ret != SP_OK) {
		syslog(LOG_ERR, "get_esp_devices failed: %s", esp_error_string(ret));
		return UBUS_STATUS_UNKNOWN_ERROR;
	}
	if(!devices.count) {
		return UBUS_STATUS_NO_DATA;
	}

	 struct blob_buf b = {};
	blob_buf_init(&b, 0);
	void * array = blobmsg_open_array(&b, "devices");
	for (size_t i = 0; i < devices.count; i++) {
		void * table = blobmsg_open_table(&b, NULL);
		device_to_blobmsg(&b, &devices.esps[i]);
		blobmsg_close_table(&b, table);
	}
	blobmsg_close_array(&b, array);
	ubus_send_reply(ctx, req, b.head);
	blob_buf_free(&b);
	esp_free_devices(&devices);
	return UBUS_STATUS_OK;
}

static int set_pin_on_off(struct ubus_context * ctx, struct ubus_object * obj,
						  struct ubus_request_data * req, const char * method, struct blob_attr * msg)
{
	struct blob_attr *tb[__DEVICES_ON_OFF_MAX] = {0};
	if(!msg)
		return UBUS_STATUS_INVALID_ARGUMENT;

	blobmsg_parse(devices_on_off_policy, __DEVICES_ON_OFF_MAX, tb, blob_data(msg), blob_len(msg));

	if(!tb[DEVICES_ON_OFF_PIN] || !tb[DEVICES_ON_OFF_PORT])
		return UBUS_STATUS_INVALID_ARGUMENT;

	int32_t pin = blobmsg_get_u32(tb[DEVICES_ON_OFF_PIN]);
	char * portname = blobmsg_get_string(tb[DEVICES_ON_OFF_PORT]);
	if(!portname)
		return UBUS_STATUS_INVALID_ARGUMENT;

	int buff_size = snprintf(NULL, 0, "{\"action\":\"%s\",\"pin\":%d}\n", method, pin) + 1;
	char * buff = malloc(buff_size);
	if(!buff) {
		syslog(LOG_ERR, "Failed to allocate memory for message buffer");
		return UBUS_STATUS_UNKNOWN_ERROR;
	}
	snprintf(buff, buff_size, "{\"action\":\"%s\",\"pin\":%d}\n", method, pin);

	char recv_buf[256] = {0};
	enum sp_return result = esp_snd_rcv_message(portname, buff, recv_buf, sizeof(recv_buf));
	free(buff);

	if(result != SP_OK || recv_buf[0] == '\0') {
		syslog(LOG_ERR, "Failed to set device pin: %s", esp_error_string(result));
		return UBUS_STATUS_UNKNOWN_ERROR;
	}

	char * end = strstr(recv_buf, "\r\n");
	if(end)
		*end = '\0';

	struct blob_buf b = {};

	blob_buf_init(&b, 0);

	if(!blobmsg_add_json_from_string(&b, recv_buf)) {
		syslog(LOG_ERR, "Invalid JSON received: %s", recv_buf);
		blob_buf_free(&b);
		return UBUS_STATUS_UNKNOWN_ERROR;
	}
	ubus_send_reply(ctx, req, b.head);
	blob_buf_free(&b);

	return UBUS_STATUS_OK;
}

static int get_device_data(struct ubus_context * ctx, struct ubus_object * obj,
						  struct ubus_request_data * req, const char * method, struct blob_attr * msg)
{
	struct blob_attr *tb[__DEVICES_GET_MAX] = {0};
	if(!msg)
		return UBUS_STATUS_INVALID_ARGUMENT;

	blobmsg_parse(devices_get_policy, __DEVICES_GET_MAX, tb, blob_data(msg), blob_len(msg));
	if(!tb[DEVICES_GET_PIN] || !tb[DEVICES_GET_PORT] || !tb[DEVICES_GET_MODEL] || !tb[DEVICES_GET_SENSOR])
		return UBUS_STATUS_INVALID_ARGUMENT;

	char * portname = blobmsg_get_string(tb[DEVICES_GET_PORT]);
	int32_t pin = blobmsg_get_u32(tb[DEVICES_GET_PIN]);
	char * model = blobmsg_get_string(tb[DEVICES_GET_MODEL]);
	char * sensor = blobmsg_get_string(tb[DEVICES_GET_SENSOR]);

	if(!portname || !model || !sensor )
		return UBUS_STATUS_INVALID_ARGUMENT;

	int buff_size = snprintf(NULL, 0,
		"{\"action\":\"%s\",\"sensor\":\"%s\",\"pin\":%d,\"model\":\"%s\"}\n",
		method, sensor, pin, model) + 1;
	char * buff = malloc(buff_size);
	if(!buff)
		return UBUS_STATUS_UNKNOWN_ERROR;

	snprintf(buff, buff_size,
		"{\"action\":\"%s\",\"sensor\":\"%s\",\"pin\":%d,\"model\":\"%s\"}\n",
		method, sensor, pin, model);

	char recv_buf[256] = {0};
	enum sp_return result = esp_snd_rcv_message(portname, buff, recv_buf, sizeof(recv_buf));
	free(buff);

	if(result != SP_OK || recv_buf[0] == '\0') {
		syslog(LOG_ERR, "Failed to get device data: %s", esp_error_string(result));
		return UBUS_STATUS_UNKNOWN_ERROR;
	}

	char * end = strstr(recv_buf, "\r\n");
	if(end)
		*end = '\0';

	struct blob_buf b = {};

	blob_buf_init(&b, 0);

	if(!blobmsg_add_json_from_string(&b, recv_buf)) {
		syslog(LOG_ERR, "Invalid JSON received: %s", recv_buf);
		blob_buf_free(&b);
		return UBUS_STATUS_UNKNOWN_ERROR;
	}
	ubus_send_reply(ctx, req, b.head);
	blob_buf_free(&b);

	return UBUS_STATUS_OK;
}

int ubus_api_init(struct ubus_context * uctx)
{
	int ret;
	ret = ubus_add_object(uctx, &device_object);
	if(ret) {
		syslog(LOG_ERR, "ubus_add_object failed: %s (%d)", ubus_strerror(ret), ret);
		return ret;
	}
	return 0;
}
