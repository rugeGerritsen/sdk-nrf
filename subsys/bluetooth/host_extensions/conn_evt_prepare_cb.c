/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/hci.h>
#include <bluetooth/conn_evt_prepare_cb.h>
#include <helpers/nrfx_gppi.h>
#include <bluetooth/hci_vs_sdc.h>

#include <nrfx_egu.h>

#if defined(CONFIG_BT_CONN_EVT_PREPARE_CB_EGU_INST_EGU0)
#define EGU_INST_IDX 0
#elif defined(CONFIG_BT_CONN_EVT_PREPARE_CB_EGU_INST_EGU1)
#define EGU_INST_IDX 1
#elif defined(CONFIG_BT_CONN_EVT_PREPARE_CB_EGU_INST_EGU2)
#define EGU_INST_IDX 2
#elif defined(CONFIG_BT_CONN_EVT_PREPARE_CB_EGU_INST_EGU3)
#define EGU_INST_IDX 3
#elif defined(CONFIG_BT_CONN_EVT_PREPARE_CB_EGU_INST_EGU4)
#define EGU_INST_IDX 4
#elif defined(CONFIG_BT_CONN_EVT_PREPARE_CB_EGU_INST_EGU5)
#define EGU_INST_IDX 5
#elif defined(CONFIG_BT_CONN_EVT_PREPARE_CB_EGU_INST_EGU020)
#define EGU_INST_IDX 020
#else
#error "Unknown EGU instance"
#endif

static nrfx_egu_t egu_inst = NRFX_EGU_INSTANCE(EGU_INST_IDX);

struct notification_cfg {
	bt_conn_evt_prepare_cb_t cb;
	const struct bt_conn *conn;
	void *user_data;
	uint32_t prepare_time_us;

	uint8_t ppi_channel;
	uint8_t egu_channel;
	uint16_t conn_interval_us;
};

static struct k_timer timers[CONFIG_BT_MAX_CONN];
static struct notification_cfg cfg[CONFIG_BT_MAX_CONN];

static void on_timer_expired(struct k_timer *timer)
{
	uint8_t index = ARRAY_INDEX(timers, timer);

	cfg[index].cb(cfg[index].conn, cfg[index].user_data);
}

static void egu_event_handler(uint8_t event_idx, void *context)
{
	ARG_UNUSED(context);

	__ASSERT_NO_MSG(event_idx < ARRAY_SIZE(timers));
	__ASSERT_NO_MSG(cfg[event_idx].conn_interval_us > cfg[event_idx].prepare_time_us);

	const uint32_t timer_distance_us =
		cfg[event_idx].conn_interval_us - cfg[event_idx].prepare_time_us;

	k_timer_start(&timers[event_idx], K_USEC(timer_distance_us), K_NO_WAIT);
}

static void on_conn_param_updated(struct bt_conn *conn,
				  uint16_t interval,
				  uint16_t latency,
				  uint16_t timeout)
{
	uint8_t index = bt_conn_index(conn);

	if (!cfg[index].cb) {
		return;
	}

	cfg[index].conn_interval_us = BT_CONN_INTERVAL_TO_US(interval);
}

static void on_disconnected(struct bt_conn *conn, uint8_t reason)
{
	uint8_t index = bt_conn_index(conn);

	if (!cfg[index].cb) {
		return;
	}

	cfg[index].cb = NULL;
	nrfx_egu_int_disable(&egu_inst, nrf_egu_channel_int_get(cfg[index].egu_channel));
	k_timer_stop(&timers[index]);

	nrfx_err_t err = nrfx_gppi_channel_free(cfg[index].ppi_channel);

	__ASSERT_NO_MSG(err == NRFX_SUCCESS);
}

BT_CONN_CB_DEFINE(conn_params_updated_cb) = {
	.le_param_updated = on_conn_param_updated,
	.disconnected = on_disconnected,
};

static int setup_connection_event_trigger(struct notification_cfg *cfg)
{
	int err;
	uint16_t conn_handle;

	err = bt_hci_get_conn_handle(cfg->conn, &conn_handle);
	if (err) {
		printk("Failed obtaining conn_handle (err %d)\n", err);
		return err;
	}

	sdc_hci_cmd_vs_get_next_conn_event_counter_t get_next_conn_event_count_params = {
		.conn_handle = conn_handle,
	};
	sdc_hci_cmd_vs_get_next_conn_event_counter_return_t get_next_conn_event_count_ret_params;

	err = hci_vs_sdc_get_next_conn_event_counter(
		&get_next_conn_event_count_params, &get_next_conn_event_count_ret_params);
	if (err) {
		printk("Failed obtaining next conn event count (err %d)\n", err);
		return err;
	}

	const sdc_hci_cmd_vs_set_conn_event_trigger_t params = {
		.conn_handle = conn_handle,
		.role = SDC_HCI_VS_CONN_EVENT_TRIGGER_ROLE_CONN,
		.ppi_ch_id = cfg->ppi_channel,
		.period_in_events = 1, /* Trigger every connection event. */
		.conn_evt_counter_start =
			get_next_conn_event_count_ret_params.next_conn_event_counter + 10,
		.task_endpoint = nrfx_egu_task_address_get(&egu_inst,
			nrf_egu_trigger_task_get(cfg->egu_channel)),
	};

	return hci_vs_sdc_set_conn_event_trigger(&params);
}

int bt_conn_evt_prepare_cb_set(const struct bt_conn *conn,
			       bt_conn_evt_prepare_cb_t cb,
			       void *user_data,
			       uint32_t prepare_distance_us)
{
	int err;
	struct bt_conn_info info;
	uint8_t index = bt_conn_index(conn);

	if (cfg[index].cb) {
		return -EALREADY;
	}

	if (nrfx_gppi_channel_alloc(&cfg[index].ppi_channel) != NRFX_SUCCESS) {
		return -ENOMEM;
	}

	err = bt_conn_get_info(conn, &info);
	if (err) {
		return err;
	}

	cfg[index].cb = cb;
	cfg[index].egu_channel = index;
	cfg[index].conn_interval_us = BT_CONN_INTERVAL_TO_US(info.le.interval);
	cfg[index].conn = conn;
	cfg[index].prepare_time_us = prepare_distance_us;
	cfg[index].user_data = user_data;
	k_timer_init(&timers[index], on_timer_expired, NULL);

	nrfx_egu_int_enable(&egu_inst, nrf_egu_channel_int_get(cfg[index].egu_channel));

	setup_connection_event_trigger(&cfg[index]);

	return 0;
}

static int driver_init(const struct device *dev)
{
	nrfx_err_t err;

	err = nrfx_egu_init(&egu_inst,
			    CONFIG_BT_CONN_EVENT_NOTIFICATION_TRIGGER_EGU_ISR_PRIO,
			    egu_event_handler,
			    NULL);
	if (err != NRFX_SUCCESS) {
		return -EALREADY;
	}

	IRQ_CONNECT(NRFX_IRQ_NUMBER_GET(NRF_EGU_INST_GET(EGU_INST_IDX)),
				CONFIG_BT_CONN_EVENT_NOTIFICATION_TRIGGER_EGU_ISR_PRIO,
				NRFX_EGU_INST_HANDLER_GET(EGU_INST_IDX),
				NULL, 0);
	irq_enable(NRFX_IRQ_NUMBER_GET(NRF_EGU_INST_GET(EGU_INST_IDX)));

	return 0;
}

DEVICE_DEFINE(conn_event_notification, "conn_event_notification",
	      driver_init, NULL,
	      NULL, NULL,
	      POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEVICE,
	      NULL);
