/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
 */

/** @file
 *  @brief Internal HCI interface
 */

#include <stdint.h>
#include <stdbool.h>

#ifndef HCI_INTERNAL_H__
#define HCI_INTERNAL_H__


/** @brief Send an HCI command packet to the SoftDevice Controller.
 *
 * @param[in] cmd_in  HCI Command packet. The first byte in the buffer should correspond to
 *                    OpCode, as specified by the Bluetooth Core Specification.
 *
 * @return Zero on success or (negative) error code otherwise.
 */
int hci_internal_cmd_put(uint8_t *cmd_in);

/** @brief Retrieve a a command complete event if available.
 *
 * This API is non-blocking.

 * @return A buffer on success or NULL otherwise.
 */
struct net_buf * hci_internal_cmd_complete_get(void);

#endif
