/*
 * Copyright (c) 2019 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/kernel.h>
#include <zephyr/console/console.h>
#include <string.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/ring_buffer.h>
#include <zephyr/types.h>
#include <zephyr/irq.h>
#include <zephyr/logging/log.h>
#include <helpers/nrfx_gppi.h>

#include <mpsl_timeslot.h>
#include <mpsl.h>
#include <hal/nrf_timer.h>
#include <hal/nrf_radio.h>

#define USE_GPPI 1
#define USE_PPI_OR_DPPI_DIRECTLY 0

#if USE_GPPI
static uint8_t radio_enable_ppi_chan;
#elif USE_PPI_OR_DPPI_DIRECTLY && defined(CONFIG_SOC_COMPATIBLE_NRF52X)
static nrf_ppi_channel_t radio_enable_ppi_chan;
#else
static uint8_t radio_enable_ppi_chan;
#endif


LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

#define TIMESLOT_REQUEST_DISTANCE_US (100000)
#define TIMESLOT_LENGTH_US           (2000)
#define TIMER_EXPIRY_US (TIMESLOT_LENGTH_US - 50)

#define MPSL_THREAD_PRIO             CONFIG_MPSL_THREAD_COOP_PRIO
#define STACKSIZE                    CONFIG_MAIN_STACK_SIZE
#define THREAD_PRIORITY              K_LOWEST_APPLICATION_THREAD_PRIO

#if defined(CONFIG_SOC_COMPATIBLE_NRF53X)
	#define LOG_OFFLOAD_IRQn SWI1_IRQn
#elif defined(CONFIG_SOC_COMPATIBLE_NRF52X)
	#define LOG_OFFLOAD_IRQn SWI1_EGU1_IRQn
#elif defined(CONFIG_SOC_COMPATIBLE_NRF54LX)
	#define LOG_OFFLOAD_IRQn SWI00_IRQn

	/* Some simple defines to make the sample portable to 54L without too many changes */
	#define NRF_TIMER0 NRF_TIMER10
	#define RADIO_INTENSET_DISABLED_Msk RADIO_INTENSET10_DISABLED_Msk
	#define RADIO_IRQn RADIO_0_IRQn
#endif

static bool request_in_cb = true;
static uint8_t adv_packet[251];

/* MPSL API calls that can be requested for the non-preemptible thread */
enum mpsl_timeslot_call {
	OPEN_SESSION,
	MAKE_REQUEST,
	CLOSE_SESSION,
};

/* Timeslot requests */
static mpsl_timeslot_request_t timeslot_request_earliest = {
	.request_type = MPSL_TIMESLOT_REQ_TYPE_EARLIEST,
	.params.earliest.hfclk = MPSL_TIMESLOT_HFCLK_CFG_XTAL_GUARANTEED,
	.params.earliest.priority = MPSL_TIMESLOT_PRIORITY_NORMAL,
	.params.earliest.length_us = TIMESLOT_LENGTH_US,
	.params.earliest.timeout_us = 1000000
};
static mpsl_timeslot_request_t timeslot_request_normal = {
	.request_type = MPSL_TIMESLOT_REQ_TYPE_NORMAL,
	.params.normal.hfclk = MPSL_TIMESLOT_HFCLK_CFG_XTAL_GUARANTEED,
	.params.normal.priority = MPSL_TIMESLOT_PRIORITY_NORMAL,
	.params.normal.distance_us = TIMESLOT_REQUEST_DISTANCE_US,
	.params.normal.length_us = TIMESLOT_LENGTH_US
};

static mpsl_timeslot_signal_return_param_t signal_callback_return_param;

/* Two ring buffers for printing the signal type with different priority from timeslot callback */
RING_BUF_DECLARE(callback_high_priority_ring_buf, 10);
RING_BUF_DECLARE(callback_low_priority_ring_buf, 10);

/* Message queue for requesting MPSL API calls to non-preemptible thread */
K_MSGQ_DEFINE(mpsl_api_msgq, sizeof(enum mpsl_timeslot_call), 10, 4);

ISR_DIRECT_DECLARE(swi1_isr)
{
	uint8_t signal_type = 0;

	while (!ring_buf_is_empty(&callback_high_priority_ring_buf)) {
		if (ring_buf_get(&callback_high_priority_ring_buf, &signal_type, 1) == 1) {
			switch (signal_type) {
			case MPSL_TIMESLOT_SIGNAL_START:
				LOG_INF("Callback: Timeslot start\n");
				break;
			case MPSL_TIMESLOT_SIGNAL_TIMER0:
				LOG_INF("Callback: Timer0 signal\n");
				break;
			case MPSL_TIMESLOT_SIGNAL_RADIO:
				LOG_INF("Callback: RADIO signal\n");
				break;
			default:
				LOG_INF("Callback: Other signal: %d\n", signal_type);
				break;
			}
		}
	}

	while (!ring_buf_is_empty(&callback_low_priority_ring_buf)) {
		if (ring_buf_get(&callback_low_priority_ring_buf, &signal_type, 1) == 1) {
			switch (signal_type) {
			case MPSL_TIMESLOT_SIGNAL_SESSION_IDLE:
				LOG_INF("Callback: Session idle\n");
				break;
			case MPSL_TIMESLOT_SIGNAL_SESSION_CLOSED:
				LOG_INF("Callback: Session closed\n");
				break;
			default:
				LOG_INF("Callback: Other signal: %d\n", signal_type);
				break;
			}
		}
	}

	return 1;
}

static void build_adv_packet(void)
{
	/* AdvData */
	const uint8_t adv_data[] = {
		/* Flags */
		2, 1, 2 | 4,
		/* Complete local name */
		12, 9, 'h', 'e', 'l', 'l', 'o', '_', 'w', 'o', 'r', 'l', 'd', '\0'};

	memset(adv_packet, 0, sizeof(adv_packet));
	adv_packet[0] = 1 << 5 |1 << 6; /* Type=ADV_IND; RFU=0, ChSel=1, TxAdd=1, RxAdd=0 */
	adv_packet[1] = 6 + sizeof(adv_data); /* Length */
	adv_packet[2] = 0;

	/* AdvA */
	adv_packet[3] = 0xC0;
	adv_packet[4] = 0x23;
	adv_packet[5] = 0x24;
	adv_packet[6] = 0x25;
	adv_packet[7] = 0x26;
	adv_packet[8] = 0xC0;

	memcpy(&adv_packet[9], adv_data, sizeof(adv_data));
}

static void ppi_channel_alloc(void)
{
	nrfx_err_t err;

#if USE_GPPI
	err = nrfx_gppi_channel_alloc(&radio_enable_ppi_chan);
#elif USE_PPI_OR_DPPI_DIRECTLY && defined(CONFIG_SOC_COMPATIBLE_NRF52X)
	err = nrfx_ppi_channel_alloc(&radio_enable_ppi_chan);
#elif USE_PPI_OR_DPPI_DIRECTLY && defined(CONFIG_SOC_COMPATIBLE_NRF53X)
	err = nrfx_dppi_channel_alloc(&radio_enable_ppi_chan);
#elif USE_PPI_OR_DPPI_DIRECTLY && defined(CONFIG_SOC_COMPATIBLE_NRF54LX)
	err = NRFX_SUCCESS;
	radio_enable_ppi_chan = 1; /* No local domain allocator exists. */
#else
	#error Unknown soc series
#endif

	if (err != NRFX_SUCCESS) {
		LOG_ERR("Failed allocating channel");
		k_oops();
	}
}

static void configure_ppis(void)
{
#if USE_GPPI
	nrfx_gppi_channel_endpoints_setup(radio_enable_ppi_chan,
					  nrf_timer_event_address_get(NRF_TIMER0,
								      NRF_TIMER_EVENT_COMPARE1),
					  nrf_radio_task_address_get(NRF_RADIO,
								     NRF_RADIO_TASK_TXEN));
	nrfx_gppi_channels_enable(BIT(radio_enable_ppi_chan));
#elif USE_PPI_OR_DPPI_DIRECTLY && defined(CONFIG_SOC_COMPATIBLE_NRF52X)
	nrf_ppi_channel_endpoint_setup(NRF_PPI,
				       radio_enable_ppi_chan,
				       nrf_timer_event_address_get(NRF_TIMER0, NRF_TIMER_EVENT_COMPARE1),
				       nrf_radio_task_address_get(NRF_RADIO, NRF_RADIO_TASK_TXEN));
	nrf_ppi_channels_enable(NRF_PPI, BIT(radio_enable_ppi_chan));
#elif USE_PPI_OR_DPPI_DIRECTLY && defined(CONFIG_SOC_COMPATIBLE_NRF53X)
	nrf_timer_publish_set(NRF_TIMER0, NRF_TIMER_EVENT_COMPARE1, BIT(radio_enable_ppi_chan));
	nrf_radio_subscribe_set(NRF_RADIO, NRF_RADIO_TASK_TXEN, BIT(radio_enable_ppi_chan));
	nrf_dppi_channels_enable(NRF_DPPIC, BIT(radio_enable_ppi_chan));
#elif USE_PPI_OR_DPPI_DIRECTLY && defined(CONFIG_SOC_COMPATIBLE_NRF54LX)
	nrf_timer_publish_set(NRF_TIMER0, NRF_TIMER_EVENT_COMPARE1, BIT(radio_enable_ppi_chan));
	nrf_radio_subscribe_set(NRF_RADIO, NRF_RADIO_TASK_TXEN, BIT(radio_enable_ppi_chan));
	nrf_dppi_channels_enable(NRF_DPPIC10, BIT(radio_enable_ppi_chan));
#endif
}

static void disable_ppis(void)
{
#if USE_GPPI
	nrfx_gppi_channel_endpoints_clear(radio_enable_ppi_chan,
					  nrf_timer_event_address_get(NRF_TIMER0,
								      NRF_TIMER_EVENT_COMPARE1),
					  nrf_radio_task_address_get(NRF_RADIO,
								     NRF_RADIO_TASK_TXEN));
	nrfx_gppi_channels_disable(BIT(radio_enable_ppi_chan));
#elif USE_PPI_OR_DPPI_DIRECTLY && defined(CONFIG_SOC_COMPATIBLE_NRF52X)
	nrf_ppi_channels_disable(NRF_PPI, BIT(radio_enable_ppi_chan));
#elif USE_PPI_OR_DPPI_DIRECTLY && defined(CONFIG_SOC_COMPATIBLE_NRF53X)
	nrf_dppi_channels_disable(NRF_DPPIC, BIT(radio_enable_ppi_chan));
#elif USE_PPI_OR_DPPI_DIRECTLY && defined(CONFIG_SOC_COMPATIBLE_NRF54LX)
	nrf_dppi_channels_disable(NRF_DPPIC10, BIT(radio_enable_ppi_chan));
#endif
}

static void configure_radio(void)
{
	nrf_radio_crc_configure(NRF_RADIO, 3, NRF_RADIO_CRC_ADDR_SKIP, 0x65b);

	/* Advertising channel access address */
	uint32_t access_address = 0x8E89BED6;
	nrf_radio_prefix0_set(NRF_RADIO, (access_address >> 24) & 0xFF);
	nrf_radio_base0_set(NRF_RADIO, access_address << 8);

	nrf_radio_crcinit_set(NRF_RADIO, 0x00555555UL);

	nrf_radio_frequency_set(NRF_RADIO, 2402);
	nrf_radio_datawhiteiv_set(NRF_RADIO, 37);

	nrf_radio_mode_set(NRF_RADIO, NRF_RADIO_MODE_BLE_1MBIT);

	nrf_radio_packet_conf_t config = {
		.lflen = 8,  /**< Length on air of LENGTH field in number of bits. */
		.s0len = 1,  /**< Length on air of S0 field in number of bytes. */
		.s1len = 0,  /**< Length on air of S1 field in number of bits. */
		.s1incl = 1, /**< Include or exclude S1 field in RAM. */
		.cilen = 0, /**< Length of code indicator - long range. */
		.plen = NRF_RADIO_PREAMBLE_LENGTH_8BIT, /**< Length of preamble on air. Decision point: TASKS_START task. */
		.crcinc = 0,    /**< Indicates if LENGTH field contains CRC or not. */
		.termlen = 0,    /**< Length of TERM field in Long Range operation. */
		.maxlen = 251,   /**< Maximum length of packet payload. */
		.statlen = 0,    /**< Static length in number of bytes. */
		.balen = 3,      /**< Base address length in number of bytes. */
		.big_endian = 0, /**< On air endianness of packet. */
		.whiteen = 1,    /**< Enable or disable packet whitening. */
	};

	nrf_radio_packet_configure(NRF_RADIO, &config);
	nrf_radio_packetptr_set(NRF_RADIO, &adv_packet[0]);
	nrf_radio_txpower_set(NRF_RADIO, NRF_RADIO_TXPOWER_0DBM);

	nrf_radio_shorts_enable(NRF_RADIO,
		NRF_RADIO_SHORT_READY_START_MASK |
		NRF_RADIO_SHORT_PHYEND_DISABLE_MASK);

	nrf_radio_int_enable(NRF_RADIO, RADIO_INTENSET_DISABLED_Msk);
	NRFX_IRQ_ENABLE(RADIO_IRQn);
}

static mpsl_timeslot_signal_return_param_t *mpsl_timeslot_callback(
	mpsl_timeslot_session_id_t session_id,
	uint32_t signal_type)
{
	(void) session_id; /* unused parameter */
	uint8_t input_data = (uint8_t)signal_type;
	uint32_t input_data_len;

	mpsl_timeslot_signal_return_param_t *p_ret_val = NULL;

	switch (signal_type) {

	case MPSL_TIMESLOT_SIGNAL_START:
		/* No return action */
		signal_callback_return_param.callback_action =
			MPSL_TIMESLOT_SIGNAL_ACTION_NONE;
		p_ret_val = &signal_callback_return_param;

		configure_ppis();
		configure_radio();
		/* Trigger RADIO at time 200 */
		nrf_timer_cc_set(NRF_TIMER0, NRF_TIMER_CC_CHANNEL1, 200);

		/* Setup timer to trigger an interrupt (and thus the TIMER0
		 * signal) before timeslot end.
		 */
		nrf_timer_cc_set(NRF_TIMER0, NRF_TIMER_CC_CHANNEL0,
			TIMER_EXPIRY_US);
		nrf_timer_int_enable(NRF_TIMER0, NRF_TIMER_INT_COMPARE0_MASK);
		input_data_len = ring_buf_put(&callback_high_priority_ring_buf, &input_data, 1);
		if (input_data_len != 1) {
			LOG_ERR("Full ring buffer, enqueue data with length %d", input_data_len);
			k_oops();
		}
		break;
	case MPSL_TIMESLOT_SIGNAL_TIMER0:

		/* Clear event */
		nrf_timer_int_disable(NRF_TIMER0, NRF_TIMER_INT_COMPARE0_MASK);
		nrf_timer_event_clear(NRF_TIMER0, NRF_TIMER_EVENT_COMPARE0);

		disable_ppis();
		if (request_in_cb) {
			/* Request new timeslot when callback returns */
			signal_callback_return_param.params.request.p_next =
				&timeslot_request_normal;
			signal_callback_return_param.callback_action =
				MPSL_TIMESLOT_SIGNAL_ACTION_REQUEST;
		} else {
			/* Timeslot will be ended */
			signal_callback_return_param.callback_action =
				MPSL_TIMESLOT_SIGNAL_ACTION_END;
		}

		p_ret_val = &signal_callback_return_param;
		input_data_len = ring_buf_put(&callback_high_priority_ring_buf, &input_data, 1);
		if (input_data_len != 1) {
			LOG_ERR("Full ring buffer, enqueue data with length %d", input_data_len);
			k_oops();
		}
		break;
	case MPSL_TIMESLOT_SIGNAL_RADIO:
		/* No return action */
		signal_callback_return_param.callback_action =
			MPSL_TIMESLOT_SIGNAL_ACTION_NONE;
		p_ret_val = &signal_callback_return_param;

		nrf_radio_int_disable(NRF_RADIO, RADIO_INTENSET_DISABLED_Msk);
		NRFX_IRQ_PENDING_CLEAR(RADIO_IRQn);
		NRFX_IRQ_DISABLE(RADIO_IRQn);

		input_data_len = ring_buf_put(&callback_high_priority_ring_buf, &input_data, 1);
		if (input_data_len != 1) {
			LOG_ERR("Full ring buffer, enqueue data with length %d", input_data_len);
			k_oops();
		}

		break;
	case MPSL_TIMESLOT_SIGNAL_SESSION_IDLE:
		input_data_len = ring_buf_put(&callback_low_priority_ring_buf, &input_data, 1);
		if (input_data_len != 1) {
			LOG_ERR("Full ring buffer, enqueue data with length %d", input_data_len);
			k_oops();
		}
		break;
	case MPSL_TIMESLOT_SIGNAL_SESSION_CLOSED:
		input_data_len = ring_buf_put(&callback_low_priority_ring_buf, &input_data, 1);
		if (input_data_len != 1) {
			LOG_ERR("Full ring buffer, enqueue data with length %d", input_data_len);
			k_oops();
		}
		break;
	default:
		LOG_ERR("unexpected signal: %u", signal_type);
		k_oops();
		break;
	}

	NVIC_SetPendingIRQ(LOG_OFFLOAD_IRQn);

	return p_ret_val;
}

static void mpsl_timeslot_demo(void)
{
	int err;
	char input_char;
	enum mpsl_timeslot_call api_call;

	printk("-----------------------------------------------------\n");
	printk("Press a key to open session and request timeslots:\n");
	printk("* 'a' for a session where each timeslot makes a new request\n");
	printk("* 'b' for a session with a single timeslot request\n");
	input_char = 'a'; //console_getchar();
	printk("%c\n", input_char);

	if (input_char == 'a') {
		request_in_cb = true;
	} else if (input_char == 'b') {
		request_in_cb = false;
	} else {
		return;
	}

	api_call = OPEN_SESSION;
	err = k_msgq_put(&mpsl_api_msgq, &api_call, K_FOREVER);
	if (err) {
		LOG_ERR("Message sent error: %d", err);
		k_oops();
	}

	api_call = MAKE_REQUEST;
	err = k_msgq_put(&mpsl_api_msgq, &api_call, K_FOREVER);
	if (err) {
		LOG_ERR("Message sent error: %d", err);
		k_oops();
	}

	printk("Press any key to close the session.\n");
	k_sleep(K_FOREVER);
	//console_getchar();

	api_call = CLOSE_SESSION;
	err = k_msgq_put(&mpsl_api_msgq, &api_call, K_FOREVER);
	if (err) {
		LOG_ERR("Message sent error: %d", err);
		k_oops();
	}
}

/* To ensure thread safe operation, call all MPSL APIs from a non-preemptible
 * thread.
 */
static void mpsl_nonpreemptible_thread(void)
{
	int err;
	enum mpsl_timeslot_call api_call = 0;

	/* Initialize to invalid session id */
	mpsl_timeslot_session_id_t session_id = 0xFFu;

	while (1) {
		if (k_msgq_get(&mpsl_api_msgq, &api_call, K_FOREVER) == 0) {
			switch (api_call) {
			case OPEN_SESSION:
				err = mpsl_timeslot_session_open(
					mpsl_timeslot_callback,
					&session_id);
				if (err) {
					LOG_ERR("Timeslot session open error: %d", err);
					k_oops();
				}
				break;
			case MAKE_REQUEST:
				err = mpsl_timeslot_request(
					session_id,
					&timeslot_request_earliest);
				if (err) {
					LOG_ERR("Timeslot request error: %d", err);
					k_oops();
				}
				break;
			case CLOSE_SESSION:
				err = mpsl_timeslot_session_close(session_id);
				if (err) {
					LOG_ERR("Timeslot session close error: %d", err);
					k_oops();
				}
				break;
			default:
				LOG_ERR("Wrong timeslot API call");
				k_oops();
				break;
			}
		}
	}
}

int main(void)
{
	ppi_channel_alloc();

	build_adv_packet();

	int err = 0; //console_init();

	if (err) {
		LOG_ERR("Initialize console device error");
		k_oops();
	}

	printk("-----------------------------------------------------\n");
	printk("             Nordic MPSL Timeslot sample\n");

	IRQ_DIRECT_CONNECT(LOG_OFFLOAD_IRQn, 1, swi1_isr, 0);
	irq_enable(LOG_OFFLOAD_IRQn);

	while (1) {
		mpsl_timeslot_demo();
		k_sleep(K_MSEC(1000));
	}
}

K_THREAD_DEFINE(mpsl_nonpreemptible_thread_id, STACKSIZE,
		mpsl_nonpreemptible_thread, NULL, NULL, NULL,
		K_PRIO_COOP(MPSL_THREAD_PRIO), 0, 0);
