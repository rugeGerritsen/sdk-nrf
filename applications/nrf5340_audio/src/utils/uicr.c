// /*
//  * Copyright (c) 2021 Nordic Semiconductor ASA
//  *
//  * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
//  */

// #include "uicr.h"
#include <stdint.h>
// #include <errno.h>
// #include <nrfx_nvmc.h>

// /* Memory address to store segger number of the board */
// #define MEM_ADDR_UICR_SNR UICR_APP_BASE_ADDR
// /* Memory address to store the channel intended used for this board */
// #define MEM_ADDR_UICR_CH (MEM_ADDR_UICR_SNR + sizeof(uint32_t))

uint8_t uicr_channel_get(void)
{
	return 0;
}

int uicr_channel_set(uint8_t channel)
{
	return 0;
}

uint64_t uicr_snr_get(void)
{
	return 0;
}
