#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/gatt.h>
#include <sdc_hci_vs.h>

LOG_MODULE_REGISTER(main);

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

static uint8_t dummy_val;

static ssize_t read_val(struct bt_conn *conn, const struct bt_gatt_attr *attr,
		       void *buf, uint16_t len, uint16_t offset)
{
	uint8_t *value = attr->user_data;

	return bt_gatt_attr_read(conn, attr, buf, len, offset, value,
				 sizeof(dummy_val));
}

BT_GATT_SERVICE_DEFINE(my_service,
    BT_GATT_PRIMARY_SERVICE(BT_UUID_CTS),
	BT_GATT_CHARACTERISTIC(BT_UUID_CTS_CURRENT_TIME, BT_GATT_CHRC_READ,
			       BT_GATT_PERM_READ,
			       read_val, NULL, &dummy_val),
);

#define BT_UUID_MY128_VAL \
	BT_UUID_128_ENCODE(0x0483dadd, 0x6c9d, 0x6ca9, 0x5d41, 0x03ad4fff4abb)

#define BT_UUID_MY128                                                     \
	BT_UUID_DECLARE_128(BT_UUID_MY128_VAL)

BT_GATT_SERVICE_DEFINE(my_128_bit_service,
    BT_GATT_PRIMARY_SERVICE(BT_UUID_MY128),
        BT_GATT_CHARACTERISTIC(BT_UUID_DECLARE_16(0x1524),
            BT_GATT_CHRC_READ,
            BT_GATT_PERM_READ,
            read_val, NULL, &dummy_val),
);

static void connected_callback(struct bt_conn *conn, uint8_t err)
{
    int retval;

    if (err) {
        LOG_INF("Connection establishment failed");
    }

    char addr[BT_ADDR_LE_STR_LEN];
	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

    LOG_INF("Connected to %s", addr);

    retval = bt_conn_le_phy_update(conn, BT_CONN_LE_PHY_PARAM_2M);
    if (retval) {
        LOG_ERR("Failed updating PHY");
    } else {
        LOG_INF("Initiated PHY update");
    }

    retval = bt_conn_le_param_update(conn, BT_LE_CONN_PARAM(0x40, 0x40, 10, 200));
    if (retval) {
        LOG_ERR("Failed updating conn params");
    } else {
        LOG_INF("Requested connection parameter update");
    }
}

static void disconnected_callback(struct bt_conn *conn, uint8_t reason)
{
    char addr[BT_ADDR_LE_STR_LEN];
	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
    LOG_INF("Disconnected from %s, reason %d", addr, reason);
}

static void security_changed_callback(struct bt_conn *conn, bt_security_t level,
				                      enum bt_security_err err)
{
    char addr[BT_ADDR_LE_STR_LEN];
	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

    if (err) {
        LOG_INF("Security changed for ID %s, level %d, err %d", addr, level, err);
        return;
    }

    LOG_INF("Security changed for ID %s, level %d", addr, level);
}

static void phy_updated_callback(struct bt_conn *conn,
			                     struct bt_conn_le_phy_info *param)
{
    char addr[BT_ADDR_LE_STR_LEN];
	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

    LOG_INF("PHY Updated for %s, TX %d, RX: %d", addr, param->tx_phy, param->rx_phy);
}

static void wait_for_ack_enable(struct bt_conn *conn)
{
    int err;
    uint16_t conn_handle;
    struct net_buf *hci_buf;
    sdc_hci_cmd_vs_peripheral_latency_mode_set_t *params;

    err = bt_hci_get_conn_handle(conn, &conn_handle);
    if (err){
        LOG_ERR("Failed obtaining conn handle");
        return;
    }

    hci_buf = bt_hci_cmd_create(SDC_HCI_OPCODE_CMD_VS_PERIPHERAL_LATENCY_MODE_SET,
                               sizeof(sdc_hci_cmd_vs_peripheral_latency_mode_set_t));
    if (!hci_buf) {
        LOG_ERR("Failed allocating peripheral latency HCI buf");
        return;
    }

    params = net_buf_add(hci_buf, sizeof(*params));
    params->conn_handle = conn_handle;
    params->mode = SDC_HCI_VS_PERIPHERAL_LATENCY_MODE_WAIT_FOR_ACK;

    err = bt_hci_cmd_send_sync(SDC_HCI_OPCODE_CMD_VS_PERIPHERAL_LATENCY_MODE_SET, hci_buf, NULL);
    if (err){
        LOG_ERR("Failed enabling WAIT_FOR_ACK mode");
    } else {
        LOG_INF("Enabled WAIT_FOR_ACK");
    }
}

static void conn_param_updated_callback(struct bt_conn *conn, uint16_t interval,
				                        uint16_t latency, uint16_t timeout)
{
    char addr[BT_ADDR_LE_STR_LEN];
	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

    LOG_INF("Conn params updated. Int %d, latency %d", interval, latency);

    wait_for_ack_enable(conn);
}

static struct bt_conn_cb conn_callbacks = {
    .connected = connected_callback,
    .disconnected = disconnected_callback,
    .security_changed = security_changed_callback,
    .le_phy_updated = phy_updated_callback,
    .le_param_updated = conn_param_updated_callback,
};

int main(void)
{
    int err;

    bt_conn_cb_register(&conn_callbacks);

    err = bt_enable(NULL);
    __ASSERT(err == 0, "BT enable");

    err = bt_le_adv_start(BT_LE_ADV_CONN, ad, ARRAY_SIZE(ad), NULL, 0);
    __ASSERT(err == 0, "Starting to advertise");

    LOG_INF("Advertising started");
    return 0;
}