/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2013 Bert Vermeulen <bert@biot.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <string.h>
#include "protocol.h"

#define USB_CONN "1041.8101"
#define VENDOR "Kecheng"
#define USB_INTERFACE 0

static const int32_t hwcaps[] = {
	SR_CONF_SOUNDLEVELMETER,
	SR_CONF_LIMIT_SAMPLES,
	SR_CONF_CONTINUOUS,
	SR_CONF_DATALOG,
	SR_CONF_SPL_WEIGHT_FREQ,
	SR_CONF_SPL_WEIGHT_TIME,
	SR_CONF_DATA_SOURCE,
};

static const uint64_t sample_intervals[][2] = {
	{ 1, 8 },
	{ 1, 2 },
	{ 1, 1 },
	{ 2, 1 },
	{ 5, 1 },
	{ 10, 1 },
	{ 60, 1 },
};

static const char *weight_freq[] = {
	"A",
	"C",
};

static const char *weight_time[] = {
	"F",
	"S",
};

static const char *data_sources[] = {
	"Live",
	"Memory",
};

SR_PRIV struct sr_dev_driver kecheng_kc_330b_driver_info;
static struct sr_dev_driver *di = &kecheng_kc_330b_driver_info;


static int init(struct sr_context *sr_ctx)
{
	return std_init(sr_ctx, di, LOG_PREFIX);
}

static int scan_kecheng(struct sr_usb_dev_inst *usb, char **model)
{
	struct drv_context *drvc;
	int len, ret;
	unsigned char cmd, buf[32];

	drvc = di->priv;
	if (sr_usb_open(drvc->sr_ctx->libusb_ctx, usb) != SR_OK)
		return SR_ERR;

	cmd = CMD_IDENTIFY;
	ret = libusb_bulk_transfer(usb->devhdl, EP_OUT, &cmd, 1, &len, 5);
	if (ret != 0) {
		libusb_close(usb->devhdl);
		sr_dbg("Failed to send Identify command: %s", libusb_error_name(ret));
		return SR_ERR;
	}

	ret = libusb_bulk_transfer(usb->devhdl, EP_IN, buf, 32, &len, 10);
	if (ret != 0) {
		libusb_close(usb->devhdl);
		sr_dbg("Failed to receive response: %s", libusb_error_name(ret));
		return SR_ERR;
	}

	libusb_close(usb->devhdl);
	usb->devhdl = NULL;

	if (len < 2 || buf[0] != (CMD_IDENTIFY | 0x80) || buf[1] > 30) {
		sr_dbg("Invalid response to Identify command");
		return SR_ERR;
	}

	buf[buf[1] + 2] = '\x0';
	*model = g_strndup((const gchar *)buf + 2, 30);
	//g_strstrip(*model);

	return SR_OK;
}

static GSList *scan(GSList *options)
{
	struct drv_context *drvc;
	struct dev_context *devc;
	struct sr_dev_inst *sdi;
	struct sr_probe *probe;
	GSList *usb_devices, *devices, *l;
	char *model;

	(void)options;

	drvc = di->priv;
	drvc->instances = NULL;

	devices = NULL;
	if ((usb_devices = sr_usb_find(drvc->sr_ctx->libusb_ctx, USB_CONN))) {
		/* We have a list of sr_usb_dev_inst matching the connection
		 * string. Wrap them in sr_dev_inst and we're done. */
		for (l = usb_devices; l; l = l->next) {
			if (scan_kecheng(l->data, &model) != SR_OK)
				continue;
			if (!(sdi = sr_dev_inst_new(0, SR_ST_INACTIVE, VENDOR,
					model, NULL)))
				return NULL;
			g_free(model);
			sdi->driver = di;
			sdi->inst_type = SR_INST_USB;
			sdi->conn = l->data;
			if (!(probe = sr_probe_new(0, SR_PROBE_ANALOG, TRUE, "SPL")))
				return NULL;
			sdi->probes = g_slist_append(sdi->probes, probe);

			if (!(devc = g_try_malloc(sizeof(struct dev_context)))) {
				sr_dbg("Device context malloc failed.");
				return NULL;
			}
			sdi->priv = devc;
			devc->limit_samples = 0;
			/* The protocol provides no way to read the current
			 * settings, so we'll enforce these. */
			devc->sample_interval = DEFAULT_SAMPLE_INTERVAL;
			devc->alarm_low = DEFAULT_ALARM_LOW;
			devc->alarm_high = DEFAULT_ALARM_HIGH;
			devc->mqflags = DEFAULT_WEIGHT_TIME | DEFAULT_WEIGHT_FREQ;
			devc->data_source = DEFAULT_DATA_SOURCE;

			/* TODO: Set date/time? */

			drvc->instances = g_slist_append(drvc->instances, sdi);
			devices = g_slist_append(devices, sdi);
		}
		g_slist_free(usb_devices);
	} else
		g_slist_free_full(usb_devices, g_free);

	return devices;
}

static GSList *dev_list(void)
{
	struct drv_context *drvc;

	drvc = di->priv;

	return drvc->instances;
}

static int dev_clear(void)
{
	return std_dev_clear(di, NULL);
}

static int dev_open(struct sr_dev_inst *sdi)
{
	struct drv_context *drvc;
	struct sr_usb_dev_inst *usb;
	int ret;

	if (!(drvc = di->priv)) {
		sr_err("Driver was not initialized.");
		return SR_ERR;
	}

	usb = sdi->conn;

	if (sr_usb_open(drvc->sr_ctx->libusb_ctx, usb) != SR_OK)
		return SR_ERR;

	if ((ret = libusb_claim_interface(usb->devhdl, USB_INTERFACE))) {
		sr_err("Failed to claim interface: %s.", libusb_error_name(ret));
		return SR_ERR;
	}
	sdi->status = SR_ST_ACTIVE;

	/* Force configuration. */
	ret = kecheng_kc_330b_configure(sdi);

	return ret;
}

static int dev_close(struct sr_dev_inst *sdi)
{
	struct sr_usb_dev_inst *usb;

	if (!di->priv) {
		sr_err("Driver was not initialized.");
		return SR_ERR;
	}

	usb = sdi->conn;

	if (!usb->devhdl)
		/*  Nothing to do. */
		return SR_OK;

	libusb_release_interface(usb->devhdl, USB_INTERFACE);
	libusb_close(usb->devhdl);
	usb->devhdl = NULL;
	sdi->status = SR_ST_INACTIVE;

	return SR_OK;
}

static int cleanup(void)
{
	int ret;
	struct drv_context *drvc;

	if (!(drvc = di->priv))
		/* Can get called on an unused driver, doesn't matter. */
		return SR_OK;

	ret = dev_clear();
	g_free(drvc);
	di->priv = NULL;

	return ret;
}

static int config_get(int key, GVariant **data, const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	GVariant *rational[2];
	const uint64_t *si;
	int tmp, ret;

	devc = sdi->priv;
	switch (key) {
	case SR_CONF_LIMIT_SAMPLES:
		*data = g_variant_new_uint64(devc->limit_samples);
		break;
	case SR_CONF_SAMPLE_INTERVAL:
		si = sample_intervals[devc->sample_interval];
		rational[0] = g_variant_new_uint64(si[0]);
		rational[1] = g_variant_new_uint64(si[1]);
		*data = g_variant_new_tuple(rational, 2);
		break;
	case SR_CONF_DATALOG:
		if ((ret = kecheng_kc_330b_recording_get(sdi, &tmp)) == SR_OK)
			*data = g_variant_new_boolean(tmp);
		else
			return SR_ERR;
		break;
	case SR_CONF_SPL_WEIGHT_FREQ:
		if (devc->mqflags & SR_MQFLAG_SPL_FREQ_WEIGHT_A)
			*data = g_variant_new_string("A");
		else
			*data = g_variant_new_string("C");
		break;
	case SR_CONF_SPL_WEIGHT_TIME:
		if (devc->mqflags & SR_MQFLAG_SPL_TIME_WEIGHT_F)
			*data = g_variant_new_string("F");
		else
			*data = g_variant_new_string("S");
		break;
	case SR_CONF_DATA_SOURCE:
		if (devc->data_source == DATA_SOURCE_LIVE)
			*data = g_variant_new_string("Live");
		else
			*data = g_variant_new_string("Memory");
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int config_set(int key, GVariant *data, const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	uint64_t p, q;
	unsigned int i;
	int tmp, ret;
	const char *tmp_str;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR_DEV_CLOSED;

	if (!di->priv) {
		sr_err("Driver was not initialized.");
		return SR_ERR;
	}

	devc = sdi->priv;
	ret = SR_OK;
	switch (key) {
	case SR_CONF_LIMIT_SAMPLES:
		devc->limit_samples = g_variant_get_uint64(data);
		sr_dbg("Setting sample limit to %" PRIu64 ".",
		       devc->limit_samples);
		break;
	case SR_CONF_SAMPLE_INTERVAL:
		g_variant_get(data, "(tt)", &p, &q);
		for (i = 0; i < ARRAY_SIZE(sample_intervals); i++) {
			if (sample_intervals[i][0] != p || sample_intervals[i][1] != q)
				continue;
			devc->sample_interval = i;
			kecheng_kc_330b_configure(sdi);
			break;
		}
		if (i == ARRAY_SIZE(sample_intervals))
			ret = SR_ERR_ARG;
		break;
	case SR_CONF_SPL_WEIGHT_FREQ:
		tmp_str = g_variant_get_string(data, NULL);
		if (!strcmp(tmp_str, "A"))
			tmp = SR_MQFLAG_SPL_FREQ_WEIGHT_A;
		else if (!strcmp(tmp_str, "C"))
			tmp = SR_MQFLAG_SPL_FREQ_WEIGHT_C;
		else
			return SR_ERR_ARG;
		devc->mqflags &= ~(SR_MQFLAG_SPL_FREQ_WEIGHT_A | SR_MQFLAG_SPL_FREQ_WEIGHT_C);
		devc->mqflags |= tmp;
		kecheng_kc_330b_configure(sdi);
		break;
	case SR_CONF_SPL_WEIGHT_TIME:
		tmp_str = g_variant_get_string(data, NULL);
		if (!strcmp(tmp_str, "F"))
			tmp = SR_MQFLAG_SPL_TIME_WEIGHT_F;
		else if (!strcmp(tmp_str, "S"))
			tmp = SR_MQFLAG_SPL_TIME_WEIGHT_S;
		else
			return SR_ERR_ARG;
		devc->mqflags &= ~(SR_MQFLAG_SPL_TIME_WEIGHT_F | SR_MQFLAG_SPL_TIME_WEIGHT_S);
		devc->mqflags |= tmp;
		kecheng_kc_330b_configure(sdi);
		break;
	case SR_CONF_DATA_SOURCE:
		tmp_str = g_variant_get_string(data, NULL);
		if (!strcmp(tmp_str, "Live"))
			devc->data_source = DATA_SOURCE_LIVE;
		else if (!strcmp(tmp_str, "Memory"))
			devc->data_source = DATA_SOURCE_MEMORY;
		else
			return SR_ERR;
		break;
	default:
		ret = SR_ERR_NA;
	}

	return ret;
}

static int config_list(int key, GVariant **data, const struct sr_dev_inst *sdi)
{
	GVariant *tuple, *rational[2];
	GVariantBuilder gvb;
	unsigned int i;

	(void)sdi;

	switch (key) {
	case SR_CONF_DEVICE_OPTIONS:
		*data = g_variant_new_fixed_array(G_VARIANT_TYPE_INT32,
				hwcaps, ARRAY_SIZE(hwcaps), sizeof(int32_t));
		break;
	case SR_CONF_SAMPLE_INTERVAL:
		g_variant_builder_init(&gvb, G_VARIANT_TYPE_ARRAY);
		for (i = 0; i < ARRAY_SIZE(sample_intervals); i++) {
			rational[0] = g_variant_new_uint64(sample_intervals[i][0]);
			rational[1] = g_variant_new_uint64(sample_intervals[i][1]);
			tuple = g_variant_new_tuple(rational, 2);
			g_variant_builder_add_value(&gvb, tuple);
		}
		*data = g_variant_builder_end(&gvb);
		break;
	case SR_CONF_SPL_WEIGHT_FREQ:
		*data = g_variant_new_strv(weight_freq, ARRAY_SIZE(weight_freq));
		break;
	case SR_CONF_SPL_WEIGHT_TIME:
		*data = g_variant_new_strv(weight_time, ARRAY_SIZE(weight_time));
		break;
	case SR_CONF_DATA_SOURCE:
		*data = g_variant_new_strv(data_sources, ARRAY_SIZE(data_sources));
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int dev_acquisition_start(const struct sr_dev_inst *sdi,
				    void *cb_data)
{
	struct drv_context *drvc;
	struct dev_context *devc;
	struct sr_usb_dev_inst *usb;
	const struct libusb_pollfd **pfd;
	int len, ret, i;
	unsigned char cmd;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR_DEV_CLOSED;

	drvc = di->priv;
	devc = sdi->priv;
	usb = sdi->conn;

	devc->cb_data = cb_data;
	devc->num_samples = 0;

	/* Send header packet to the session bus. */
	std_session_send_df_header(cb_data, LOG_PREFIX);

	pfd = libusb_get_pollfds(drvc->sr_ctx->libusb_ctx);
	for (i = 0; pfd[i]; i++) {
		/* Handle USB events every 100ms, for decent latency. */
		sr_source_add(pfd[i]->fd, pfd[i]->events, 100,
				kecheng_kc_330b_handle_events, (void *)sdi);
		/* We'll need to remove this fd later. */
		devc->usbfd[i] = pfd[i]->fd;
	}
	devc->usbfd[i] = -1;

	cmd = CMD_GET_LIVE_SPL;
	ret = libusb_bulk_transfer(usb->devhdl, EP_OUT, &cmd, 1, &len, 5);
	if (ret != 0 || len != 1) {
		sr_dbg("Failed to start acquisition: %s", libusb_error_name(ret));
		return SR_ERR;
	}

	return SR_OK;
}

static int dev_acquisition_stop(struct sr_dev_inst *sdi, void *cb_data)
{
	(void)cb_data;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR_DEV_CLOSED;

	/* TODO: stop acquisition. */

	return SR_OK;
}

SR_PRIV struct sr_dev_driver kecheng_kc_330b_driver_info = {
	.name = "kecheng-kc-330b",
	.longname = "Kecheng KC-330B",
	.api_version = 1,
	.init = init,
	.cleanup = cleanup,
	.scan = scan,
	.dev_list = dev_list,
	.dev_clear = dev_clear,
	.config_get = config_get,
	.config_set = config_set,
	.config_list = config_list,
	.dev_open = dev_open,
	.dev_close = dev_close,
	.dev_acquisition_start = dev_acquisition_start,
	.dev_acquisition_stop = dev_acquisition_stop,
	.priv = NULL,
};