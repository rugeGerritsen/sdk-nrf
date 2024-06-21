/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/kernel.h>
#include <zephyr/console/console.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <bluetooth/services/latency.h>
#include <bluetooth/services/latency_client.h>
#include <bluetooth/gatt_dm.h>
#include <bluetooth/conn_evt_prepare_cb.h>

#define CONN_EVT_CB_PREPARE_DISTANCE_US 2000

static struct bt_latency latency;
static struct bt_latency_client latency_client;

void scan_start(void);
void adv_start(void);

static bool latency_request_time_in_use;
static uint32_t latency_request_time;
static uint32_t conn_interval_us;
static char role;

static void latency_request_work_handler(struct k_work *item)
{
	int err;

	if (latency_request_time_in_use) {
		return;
	}

	latency_request_time = k_cycle_get_32();
	err = bt_latency_request(&latency_client,
				 &latency_request_time,
				 sizeof(latency_request_time));
	if (err) {
		printk("Latency request failed (err %d)\n", err);
	}
	latency_request_time_in_use = true;
}

static K_WORK_DEFINE(latency_request_work, latency_request_work_handler);

static void conn_evt_prepare_cb(const struct bt_conn *conn, void *user_data)
{
	ARG_UNUSED(conn);
	ARG_UNUSED(user_data);

	/* Offload processing to thread context to allow using Bluetooth APIs. */
	k_work_submit(&latency_request_work);
}

static void discovery_complete(struct bt_gatt_dm *dm, void *context)
{
	struct bt_latency_client *latency = context;

	printk("Service discovery completed\n");

	bt_latency_handles_assign(dm, latency);
	bt_gatt_dm_data_release(dm);

	int err = bt_conn_evt_prepare_cb_set(bt_gatt_dm_conn_get(dm),
						    conn_evt_prepare_cb,
							NULL,
							CONN_EVT_CB_PREPARE_DISTANCE_US);
	if (err) {
		printk("Failed enabling notification callback\n");
	}
}

static void discovery_service_not_found(struct bt_conn *conn, void *context)
{
	printk("Service not found\n");
}

static void discovery_error(struct bt_conn *conn, int err, void *context)
{
	printk("Error while discovering GATT database: (err %d)\n", err);
}

struct bt_gatt_dm_cb discovery_cb = {
	.completed         = discovery_complete,
	.service_not_found = discovery_service_not_found,
	.error_found       = discovery_error,
};

static void connected(struct bt_conn *conn, uint8_t conn_err)
{
	char addr[BT_ADDR_LE_STR_LEN];
	struct bt_conn_info info;

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	if (conn_err) {
		printk("Failed to connect to %s (%u)\n", addr, conn_err);
		if (role == 'c') {
			scan_start();
		} else {
			adv_start();
		}
		return;
	}

	printk("Connected: %s\n", addr);

	if (bt_conn_get_info(conn, &info)) {
		printk("Failed getting conn info\n");
		return;
	}

	conn_interval_us = BT_CONN_INTERVAL_TO_US(info.le.interval);

	/*Start service discovery when link is encrypted*/
	int err = bt_gatt_dm_start(conn, BT_UUID_LATENCY, &discovery_cb,
			       &latency_client);
	if (err) {
		printk("Discover failed (err %d)\n", err);
	}
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	printk("Disconnected: %s (reason %u)\n", addr, reason);

	if (role == 'c') {
		scan_start();
	} else {
		adv_start();
	}
}

static void on_conn_param_updated(struct bt_conn *conn,
				  uint16_t interval,
				  uint16_t latency,
				  uint16_t timeout)
{
	ARG_UNUSED(conn);
	ARG_UNUSED(latency);
	ARG_UNUSED(timeout);

	conn_interval_us = BT_CONN_INTERVAL_TO_US(interval);
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = connected,
	.disconnected = disconnected,
	.le_param_updated = on_conn_param_updated,
};

static void latency_response_handler(const void *buf, uint16_t len)
{
	uint32_t latency_time;

	if (len == sizeof(latency_time)) {
		/* compute how long the time spent */

		latency_time = *((uint32_t *)buf);
		uint32_t cycles_spent = k_cycle_get_32() - latency_time;
		uint32_t total_latency_us = (uint32_t)k_cyc_to_us_floor64(cycles_spent);

		/* The latency service uses ATT Write Requests.
		 * The ATT Write Response is sent in the next connection event.
		 * The actual expected latency is therefore the total latency minus the connection
		 * interval.
		 */
		uint32_t latency_minus_conn_interval = total_latency_us - conn_interval_us;

		printk("Latency: %d us, round trip latency: %d us\n",
			latency_minus_conn_interval, total_latency_us);

		latency_request_time_in_use = false;
	}
}

static const struct bt_latency_client_cb latency_client_cb = {
	.latency_response = latency_response_handler
};

int main(void)
{
	int err;

	printk("Starting connection event prepare sample.\n");

	err = bt_enable(NULL);
	if (err) {
		printk("Bluetooth init failed (err %d)\n", err);
		return 0;
	}

	err = bt_latency_init(&latency, NULL);
	if (err) {
		printk("Latency service initialization failed (err %d)\n", err);
		return 0;
	}

	err = bt_latency_client_init(&latency_client, &latency_client_cb);
	if (err) {
		printk("Latency client initialization failed (err %d)\n", err);
		return 0;
	}

	console_init();
	do {
		printk("Choose device role - type c (central) or p (peripheral): ");

		role = console_getchar();

		switch (role) {
		case 'p':
			printk("\nPeripheral. Starting advertising\n");
			adv_start();
			break;
		case 'c':
			printk("\nCentral. Starting scanning\n");
			scan_start();
			break;
		default:
			printk("\n");
			break;
		}
	} while (role != 'c' && role != 'p');
}
