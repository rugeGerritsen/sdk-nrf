#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>

LOG_MODULE_REGISTER(main);

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

static void connected_callback(struct bt_conn *conn, uint8_t err)
{
    if (err) {
        LOG_INF("Connection establishment failed");
    }

    char addr[BT_ADDR_LE_STR_LEN];
	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

    LOG_INF("Connected to %s", addr);
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

static struct bt_conn_cb conn_callbacks = {
    .connected = connected_callback,
    .disconnected = disconnected_callback,
    .security_changed = security_changed_callback,
};

int main(void)
{
    int err;

    bt_conn_cb_register(&conn_callbacks);

    err = bt_enable(NULL);
    __ASSERT(err == 0, "BT enable");

    err = bt_le_adv_start(BT_LE_ADV_CONN, ad, ARRAY_SIZE(ad), NULL, 0);
    __ASSERT(err == 0, "Starting to advertise");

    LOG_INF("Hello, world");
    return 0;
}