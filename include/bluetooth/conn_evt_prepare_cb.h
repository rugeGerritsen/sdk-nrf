/** @file
 *  @brief Connection Event Prepare callback APIs
 */

/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef BT_CONN_PREPARE_CB_H__
#define BT_CONN_PREPARE_CB_H__

#include <stdint.h>
#include <zephyr/bluetooth/conn.h>

/**
 * @file
 * @defgroup bt_conn_evt_prepare_cb Connection Event Prepare callback
 * @{
 * @brief APIs to setup a connection event prepare callback
 *
 * The connection event prepare callbacks are triggered right before
 * the start of a connection event. This allows the application to
 * reduce the total system latency as data can be sampled right before
 * it is sent on air.
 */

#ifdef __cplusplus
extern "C" {
#endif

/** Connection event prepare callback
 *
 * The callback is called from ISR context.
 * That is, the user needs to offload processing
 * to a thread or workqueue thread before it calls Bluetooth APIs.
 *
 * @param[in] conn      The connection context.
 * @param[in] user_data User provided data.
 */
typedef void (*bt_conn_evt_prepare_cb_t)(const struct bt_conn *conn,
					 void *user_data);

/** Set the connect event prepare callback for a specified connection context.
 *
 * @param[in] conn                The connection context.
 * @param[in] cb                  The callback to be used.
 * @param[in] prepare_distance_us The distance in time from the start of the
 *                                callback to the start of the connection event.
 */
int bt_conn_evt_prepare_cb_set(const struct bt_conn *conn,
			       bt_conn_evt_prepare_cb_t cb,
			       void *user_data,
			       uint32_t prepare_distance_us);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

/**
 * @}
 */

#endif /* BT_CONN_PREPARE_CB_H__ */
