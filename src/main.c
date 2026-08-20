#include <libserialport.h>
#include <syslog.h>

#include <libubox/uloop.h>
#include <libubus.h>

#include "ubus_api.h"
#include "signals.h"
#include "esp_devices.h"

enum {
	UBUS_CONNECT_ERROR = 1,
	UBUS_INIT_FAILED
};

int main(void)
{
	Signal(SIGINT, handle_signal);
	Signal(SIGTERM, handle_signal);
	Signal(SIGHUP, handle_signal);
	Signal(SIGQUIT, handle_signal);

	openlog("ubved", LOG_PID | LOG_CONS, LOG_DAEMON);

	int mret, ret;
	struct ubus_context * uctx = NULL;

	uloop_init();

	uctx = ubus_connect(NULL);
	
	if(!uctx) {	
		syslog(LOG_ERR, "Failed to connect to ubus");
		mret = UBUS_CONNECT_ERROR;
		goto cleanup;
	}

	ubus_add_uloop(uctx);
		
	if((ret = ubus_api_init(uctx)) != 0) {
		syslog(LOG_ERR, "ubus init failed: %s", ubus_strerror(ret));
		mret = UBUS_INIT_FAILED;
		goto cleanup;
	}

	uloop_run();

cleanup:
	if(uctx) {
		ubus_free(uctx);
		uctx = NULL;	
	}
	if(mret == 0)
		syslog(LOG_INFO, "Shutting down gracefully");
	return mret;
}