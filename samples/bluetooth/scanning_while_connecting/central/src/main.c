#include <zephyr/kernel.h>
#include <zephyr/sys/ring_buffer.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/addr.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(app);

typedef enum {
	/** The central scans for connectable addresses, then
	 * disables the scanner before starting to establish a
	 * connection to a scanned connectable address.
	 * Once the connection is established, the central must start the
	 * scanner again to repeat the process to establish more connections.
	 */
	SEQUENTIAL_SCAN_AND_CONNECT,

	/** The central scans for connectable addresses, and starts
	 * to establish a connection. While establishing a connection,
	 * the central can continue to scan and cache other connectable
	 * addresses. Once the pending connection is established, the
	 * central can immediately establish a connection to one of the
	 * cached addresses.
	 */
	CONCURRENT_SCAN_AND_CONNECT,

	/** The central scans for connectable addresses, and starts
	 * to establish a connection. While establishing a connection,
	 * the central can continue to scan and cache other connectable
	 * addresses. Once the pending connection is established, the
	 * central can add the cached addresses to the whitelist, and
	 * establish a connection using a whitelist.
	 */
	CONCURRENT_SCAN_AND_CONNECT_WITH_WHITELIST,
} connection_establishment_mode_t;

connection_establishment_mode_t conn_establishment_modes[] = {
	SEQUENTIAL_SCAN_AND_CONNECT,
	CONCURRENT_SCAN_AND_CONNECT,
	CONCURRENT_SCAN_AND_CONNECT_WITH_WHITELIST,
};

#define NUM_CONN_ESTABLISHMENT_MODES (sizeof(conn_establishment_modes) / sizeof(conn_establishment_modes[0]))

connection_establishment_mode_t active_conn_establishment_mode;

/** Semaphore is used to help demonstrate the feature in the
 * sample application. Not used to synchronize bluetooth state
 * or other.
 */
K_SEM_DEFINE(sem, 0, 1);

/** Mutex is used to protect from concurrent reads
 * and writes in the ring buffer.
 */
K_MUTEX_DEFINE(mutex_ring_buf);

static char adv_name[] = "scanning_while_connecting";
#define ADV_NAME_STR_MAX_LEN (sizeof(adv_name))

#define PEER_ADDR_CACHE_SIZE (10u)
RING_BUF_DECLARE(connectable_peers_ring_buf, PEER_ADDR_CACHE_SIZE * sizeof(bt_addr_le_t));

static bt_addr_le_t connectable_peer_addr_buf;
static bool cached_connectable_peer;


static void connect_to_cached_advertisers(struct k_work * work);
static K_WORK_DEFINE(connect_to_cached_advertisers_worker, connect_to_cached_advertisers);

static void add_cached_advertisers_to_filter_accept_list(struct k_work * work);
static K_WORK_DEFINE(add_cached_advertisers_to_filter_accept_list_worker, add_cached_advertisers_to_filter_accept_list);

static void scan_start();
static void scan_stop();

static bool connection_establishment_ongoing;

static uint32_t num_connections;

static void connect_to_cached_advertisers(struct k_work * work) {
	bt_addr_le_t addr;
	size_t bytes_read;

	k_mutex_lock(&mutex_ring_buf, K_FOREVER);
	bytes_read = ring_buf_get(&connectable_peers_ring_buf, (uint8_t *) &addr, sizeof(bt_addr_le_t));
	k_mutex_unlock(&mutex_ring_buf);

	/** We might cache the same peer address twice. In that situation, we might
	 * already have connected to the peer device/advertiser address we now
	 * read from the ring buffer. To avoid attempting to establish a connection
	 * to the same peer twice, we check if we have already established a connection.
	 */
	struct bt_conn * conn = bt_conn_lookup_addr_le(BT_ID_DEFAULT, &addr);
	if (conn) {
		bt_conn_unref(conn);
		return;
	}

	if (bytes_read > 0) {

		char addr_str[BT_ADDR_LE_STR_LEN] = {0};
		bt_addr_le_to_str(&addr, addr_str, sizeof(addr_str));

		printk("Connecting to previously cached %s\n", addr_str);
		try_connect(&addr);

	}
}

static void add_cached_advertisers_to_filter_accept_list(struct k_work * work) {
	size_t bytes_read = 0;
	bt_addr_le_t addr;

	while (true) {
		k_mutex_lock(&mutex_ring_buf, K_FOREVER);
		bytes_read = ring_buf_get(&connectable_peers_ring_buf, (uint8_t *) &addr, sizeof(bt_addr_le_t));
		k_mutex_unlock(&mutex_ring_buf);

		if (bytes_read > 0) {
			bt_le_filter_accept_list_add(&addr);
		} else {
			break;
		}
	}

	connection_establishment_ongoing = false;
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	char addr_str[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr_str, sizeof(addr_str));

	printk("Disconnected from addr %s\n", addr_str);

	num_connections--;
	if (num_connections == 0) {
		k_sem_give(&sem);
	}
}

static void connected(struct bt_conn *conn, uint8_t err)
{
	connection_establishment_ongoing = false;
	char addr_str[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr_str, sizeof(addr_str));

	if (err) {
		printk("Failed to connect to %s (%u)\n", addr_str, err);
		return;
	} else {
		printk("Connected to %s\n", addr_str);
		num_connections++;
		if (num_connections == CONFIG_BT_MAX_CONN) {
			/** We have connected to all advertisers.
			 * Give the semaphore to move on to the
			 * next round of connecting to peer advertisers.
			 * */
			k_sem_give(&sem);
			return;
		}
	}

	switch (active_conn_establishment_mode) {
		case SEQUENTIAL_SCAN_AND_CONNECT:
			connection_establishment_ongoing = false;
			scan_start();
			break;
		case CONCURRENT_SCAN_AND_CONNECT:
			connection_establishment_ongoing = false;
			if (cached_connectable_peer) {

				/* Check that we have not already connected to the cached address */
				struct bt_conn * conn = bt_conn_lookup_addr_le(BT_ID_DEFAULT, &connectable_peer_addr_buf);
				if (conn) {
					bt_conn_unref(conn);
					return;
				}

				bt_addr_le_to_str(&connectable_peer_addr_buf, addr_str, sizeof(addr_str));
				printk("Connecting to previously cached %s\n", addr_str);

				try_connect(&connectable_peer_addr_buf);
				cached_connectable_peer = false;
			}

			break;
		case CONCURRENT_SCAN_AND_CONNECT_WITH_WHITELIST:
			/** This is submitted to the work queue to avoid looping in the callback.
			 *
			 * The flag connection_establishment_ongoing that guards establishing a
			 * connection when connection establishment is already pending is set to false
			 * inside the worker, after we have added all cached addresses to the filter accept list.
			 * */
			k_work_submit(&add_cached_advertisers_to_filter_accept_list_worker);
			break;
		default:
			break;
	}

}

static void try_connect(const bt_addr_le_t *addr)
{
	connection_establishment_ongoing = true;
	int err;
	struct bt_conn *unused_conn = NULL;

	/** Interval and window of the create connection parameters
	 * must be the same as the interval and window of the scanner
	 * parameters to enable scanning and connecting concurrently.
	 * */
	const struct bt_conn_le_create_param create_param = {
		.options = BT_CONN_LE_OPT_NONE,
		.interval = BT_GAP_SCAN_FAST_INTERVAL_MIN,
		.window = BT_GAP_SCAN_FAST_INTERVAL_MIN,
		.interval_coded = 0,
		.window_coded = 0,
		.timeout = 0,
	};

	if (active_conn_establishment_mode == SEQUENTIAL_SCAN_AND_CONNECT) {
		scan_stop();
	}

	err = bt_conn_le_create(addr, &create_param, BT_LE_CONN_PARAM_DEFAULT,
						&unused_conn);
	if (err) {
		connection_establishment_ongoing = false;
		printk("bt_conn_le_create failed (err %d)\n", err);

		if (active_conn_establishment_mode == SEQUENTIAL_SCAN_AND_CONNECT) {
			scan_start();
		}
	}

	if (unused_conn) {
		bt_conn_unref(unused_conn);
	}
}

static struct bt_conn_cb conn_callbacks = {
	.connected = connected,
	.disconnected = disconnected,
};

static bool adv_data_parse_cb(struct bt_data *data, void *user_data)
{
	char * rcvd_name = user_data;
	uint8_t len;

	switch (data->type) {
		case BT_DATA_NAME_SHORTENED:
		case BT_DATA_NAME_COMPLETE:
			len = MIN(data->data_len, ADV_NAME_STR_MAX_LEN - 1);
			memcpy(rcvd_name, data->data, len);
			rcvd_name[len] = '\0';
			return false;
		default:
			return true;
	}
}

static void scan_recv(const struct bt_le_scan_recv_info *info, struct net_buf_simple *buf)
{
	/** We're only interested in connectable advertisers to
	 * show faster connection establishment
	 * */
	if (info->adv_type != BT_GAP_ADV_TYPE_ADV_IND && info->adv_type != BT_GAP_ADV_TYPE_EXT_ADV) {
		return;
	}

	/* connect only to devices in close proximity */
	if (info->rssi < -50) {
		return;
	}

	char name_str[ADV_NAME_STR_MAX_LEN] = {0};
	bt_data_parse(buf, adv_data_parse_cb, name_str);

	if (strncmp(name_str, adv_name, ADV_NAME_STR_MAX_LEN) == 0) {

		char addr_str[BT_ADDR_LE_STR_LEN] = {0};
		bt_addr_le_to_str(info->addr, addr_str, sizeof(addr_str));

		if (connection_establishment_ongoing) {
			if (active_conn_establishment_mode == CONCURRENT_SCAN_AND_CONNECT) {
				memcpy(&connectable_peer_addr_buf, info->addr, sizeof(connectable_peer_addr_buf));
				cached_connectable_peer = true;

			} else if (active_conn_establishment_mode == CONCURRENT_SCAN_AND_CONNECT) {
				k_mutex_lock(&mutex_ring_buf, K_FOREVER);
				/** We might scan more than one advertising packets coming from the same advertiser.
				 * In that situation, we might cache the same address twice in this sample application.
				*/
				uint32_t bytes_written = ring_buf_put(&connectable_peers_ring_buf, (uint8_t *) info->addr, sizeof(bt_addr_le_t));
				k_mutex_unlock(&mutex_ring_buf);

				if (bytes_written > 0) {
					printk("Scanned and cached connectable addr %s\n", addr_str);
				}

			}


		} else {
			printk("Connecting to addr %s\n", addr_str);
			try_connect(info->addr);
		}
	}
}

static struct bt_le_scan_cb scan_callbacks = {
	.recv = scan_recv,
};

static void scan_start() {
	int err = bt_le_scan_start(BT_LE_SCAN_ACTIVE_CONTINUOUS, NULL);
	if (err) {
		printk("Scanning failed to start (err %d)\n", err);
	}
	printk("Started scanning\n");
}

static void scan_stop() {
	int err = bt_le_scan_stop();
	if (err) {
		printk("Failed to stop scanning (err %d)\n", err);
	}
	printk("Stopped scanning\n");
}

static void disconnect(struct bt_conn *conn, void *data)
{
	char addr[BT_ADDR_LE_STR_LEN];
	int err;

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	err = bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
	if (err) {
		printk("Failed disconnection %s.\n", addr);
	}
}

static void print_conn_establishment_mode(connection_establishment_mode_t active_conn_establishment_mode) {
	switch (active_conn_establishment_mode) {
			case SEQUENTIAL_SCAN_AND_CONNECT:
				printk("SEQUENTIAL_SCAN_AND_CONNECT: ");
				break;
			case CONCURRENT_SCAN_AND_CONNECT:
				printk("CONCURRENT_SCAN_AND_CONNECT: ");
				break;
			case CONCURRENT_SCAN_AND_CONNECT_WITH_WHITELIST:
				printk("CONCURRENT_SCAN_AND_CONNECT_WITH_WHITELIST: ");
				break;
			default:
				break;
	}
}

int main(void)
{
	int err;

	int64_t uptime_start_scan_ms;
	int64_t uptime_create_all_connections_ms;
	int64_t total_uptime_create_all_connections_ms;

	err = bt_enable(NULL);
	if (err) {
	printk("Bluetooth init failed (err %d)\n", err);
		return 0;
	}

	printk("Bluetooth initialized\n");

	err = bt_le_scan_cb_register(&scan_callbacks);
	if (err) {
		printk("Scan callback register failed (err %d)\n", err);
		return 0;
	}
	err = bt_conn_cb_register(&conn_callbacks);
	if (err) {
		printk("Conn callback register failed (err %d)\n", err);
		return 0;
	}

	for (uint8_t i = 0; i < NUM_CONN_ESTABLISHMENT_MODES - 1; i++) {

		num_connections = 0;
		total_uptime_create_all_connections_ms = 0;

		active_conn_establishment_mode = conn_establishment_modes[i];
		print_conn_establishment_mode(active_conn_establishment_mode);
		printk("starting sample benchmark\n");

		uptime_start_scan_ms = k_uptime_get();
		scan_start();

		/* Wait until the connect callback is called for all connections */
		k_sem_take(&sem, K_FOREVER);
		uptime_create_all_connections_ms = k_uptime_get();

		scan_stop();

		total_uptime_create_all_connections_ms += (uptime_create_all_connections_ms - uptime_start_scan_ms);

		print_conn_establishment_mode(active_conn_establishment_mode);
		printk("%llums to create %u connections\n", total_uptime_create_all_connections_ms,
			CONFIG_BT_MAX_CONN);

		printk("Disconnecting connections...\n");
		bt_conn_foreach(BT_CONN_TYPE_LE, disconnect, NULL);

		/* Wait until the disconnect callback is called for all connections */
		k_sem_take(&sem, K_FOREVER);
		printk("---------------------------------------------------------------------\n");
		printk("---------------------------------------------------------------------\n");
	}
}
