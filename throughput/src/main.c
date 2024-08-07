/*
 * Copyright (c) 2018 Nordic Semiconductor ASA
 * Copyright (c) 2023-2024 Ezurio
 * 
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/kernel.h>
#include <zephyr/console/console.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/byteorder.h>
#include <string.h>
#include <stdlib.h>
#include <zephyr/types.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/crypto.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/hci_vs.h>
#include <zephyr/bluetooth/uuid.h>
#include <bluetooth/services/throughput.h>
#include <bluetooth/scan.h>
#include <bluetooth/gatt_dm.h>

#include <zephyr/shell/shell_uart.h>

#include <dk_buttons_and_leds.h>

#include "main.h"

#define VERSION_STR "2.3.0." CONFIG_BT_THROUGHPUT_BUILD_VERSION

#define DEVICE_NAME	CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LEN (sizeof(DEVICE_NAME) - 1)
#define INTERVAL_MIN	0x140	/* 320 units, 400 ms */
#define INTERVAL_MAX	0x140	/* 320 units, 400 ms */

#define THROUGHPUT_CONFIG_TIMEOUT K_SECONDS(20)

static K_SEM_DEFINE(throughput_sem, 0, 1);

static bool role_selected;
static bool role_central;
static int print_type = PRINT_TYPE_GRAPHICS;
static volatile bool data_length_req;
static volatile bool test_ready;
static struct bt_conn *default_conn;
static uint8_t handle_type;
static uint16_t handle;
static struct bt_throughput throughput;
static const struct bt_uuid *uuid128 = BT_UUID_THROUGHPUT;
static struct bt_gatt_exchange_params exchange_params;
static struct bt_le_conn_param *conn_param =
	BT_LE_CONN_PARAM(INTERVAL_MIN, INTERVAL_MAX, 0, 400);
static bool connection_params_set;

#if defined(CONFIG_BT_EXT_ADV)
static bool adv_set_created;
static bool adv_ext;
static struct bt_le_ext_adv *adv;
#endif

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA_BYTES(BT_DATA_UUID128_ALL,
		0xBB, 0x4A, 0xFF, 0x4F, 0xAD, 0x03, 0x41, 0x5D,
		0xA9, 0x6C, 0x9D, 0x6C, 0xDD, 0xDA, 0x83, 0x04),
};

static const struct bt_data sd[] = {
	BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN),
};

static const char img[] =
#include "img.file"
;

#if defined(CONFIG_DK_LIBRARY)
static void button_handler_cb(uint32_t button_state, uint32_t has_changed);
static void remove_button_handlers(void);
#endif

static void restart_ble_handler(struct k_work *work);

static K_WORK_DEFINE(restart_ble, restart_ble_handler);

static const char *phy2str(uint8_t phy)
{
	switch (phy) {
	case 0: return "No packets";
	case BT_GAP_LE_PHY_1M: return "LE 1M";
	case BT_GAP_LE_PHY_2M: return "LE 2M";
	case BT_GAP_LE_PHY_CODED: return "LE Coded";
	default: return "Unknown";
	}
}

static void instruction_print(void)
{
	printk("\nType 'config' to change the configuration parameters.\n");
	printk("You can use the Tab key to autocomplete your input.\n");
	printk("Type 'run' when you are ready to run the test.\n");
}

void scan_filter_match(struct bt_scan_device_info *device_info,
		       struct bt_scan_filter_match *filter_match,
		       bool connectable)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(device_info->recv_info->addr, addr, sizeof(addr));

	printk("Filters matched. Address: %s connectable: %d RSSI: %d\n", addr, connectable,
	       device_info->recv_info->rssi);
}

void scan_filter_no_match(struct bt_scan_device_info *device_info,
			  bool connectable)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(device_info->recv_info->addr, addr, sizeof(addr));

	printk("Discarded. Address: %s connectable: %d\n", addr, connectable);
}

void scan_connecting_error(struct bt_scan_device_info *device_info)
{
	printk("Connecting failed\n");
}

BT_SCAN_CB_INIT(scan_cb, scan_filter_match, scan_filter_no_match,
		scan_connecting_error, NULL);

static void exchange_func(struct bt_conn *conn, uint8_t att_err,
			  struct bt_gatt_exchange_params *params)
{
	struct bt_conn_info info = {0};
	int err;

	printk("MTU exchange %s\n", att_err == 0 ? "successful" : "failed");

	err = bt_conn_get_info(conn, &info);
	if (err) {
		printk("Failed to get connection info %d\n", err);
		return;
	}

	if (info.role == BT_CONN_ROLE_CENTRAL) {
		instruction_print();
		test_ready = true;
	}
}

static void discovery_complete(struct bt_gatt_dm *dm,
			       void *context)
{
	int err;
	struct bt_throughput *throughput = context;

	printk("Service discovery completed\n");

	bt_gatt_dm_data_print(dm);
	bt_throughput_handles_assign(dm, throughput);
	bt_gatt_dm_data_release(dm);

	exchange_params.func = exchange_func;

	err = bt_gatt_exchange_mtu(default_conn, &exchange_params);
	if (err) {
		printk("MTU exchange failed (err %d)\n", err);
	} else {
		printk("MTU exchange pending\n");
	}
}

static void discovery_service_not_found(struct bt_conn *conn,
					void *context)
{
	printk("Service not found\n");
}

static void discovery_error(struct bt_conn *conn,
			    int err,
			    void *context)
{
	printk("Error while discovering GATT database: (%d)\n", err);
}

struct bt_gatt_dm_cb discovery_cb = {
	.completed         = discovery_complete,
	.service_not_found = discovery_service_not_found,
	.error_found       = discovery_error,
};

static void connected(struct bt_conn *conn, uint8_t hci_err)
{
	struct bt_conn_info info = {0};
	int err;

	if (hci_err) {
		if (hci_err == BT_HCI_ERR_UNKNOWN_CONN_ID) {
			/* Canceled creating connection */
			return;
		}

		printk("Connection failed (err 0x%02x)\n", hci_err);
		return;
	}

	if (default_conn) {
		printk("Connection exists, disconnect second connection\n");
		bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
		return;
	}

	default_conn = bt_conn_ref(conn);

	err = bt_conn_get_info(default_conn, &info);
	if (err) {
		printk("Failed to get connection info %d\n", err);
		return;
	}

	printk("Connected as %s\n",
	       info.role == BT_CONN_ROLE_CENTRAL ? "central" : "peripheral");
	printk("Connection interval is %u units\n", info.le.interval);
	printk("Connection TX PHY %s and RX PHY %s\n", phy2str(info.le.phy->tx_phy),
	       phy2str(info.le.phy->rx_phy));

	if (info.role == BT_CONN_ROLE_CENTRAL) {
		err = bt_gatt_dm_start(default_conn,
				       BT_UUID_THROUGHPUT,
				       &discovery_cb,
				       &throughput);

		if (err) {
			printk("Discover failed (err %d)\n", err);
		}
	}
}

static void scan_init(void)
{
	int err;
	struct bt_le_scan_param scan_param = {
		.type = BT_LE_SCAN_TYPE_PASSIVE,
		.options = BT_LE_SCAN_OPT_FILTER_DUPLICATE | BT_LE_SCAN_OPT_CODED,
		.interval = BT_GAP_SCAN_FAST_INTERVAL,
		.window   = BT_GAP_SCAN_FAST_WINDOW,
	};

	struct bt_scan_init_param scan_init = {
		.connect_if_match = 1,
		.scan_param = &scan_param,
		.conn_param = conn_param
	};

	bt_scan_init(&scan_init);
	bt_scan_cb_register(&scan_cb);

	err = bt_scan_filter_add(BT_SCAN_FILTER_TYPE_UUID, uuid128);
	if (err) {
		printk("Scanning filters cannot be set\n");

		return;
	}

	err = bt_scan_filter_enable(BT_SCAN_UUID_FILTER, false);
	if (err) {
		printk("Filters cannot be turned on\n");
	}
}

static void scan_start(void)
{
	int r;

	r = bt_scan_start(BT_SCAN_TYPE_SCAN_PASSIVE);
	printk("Start scanning: %d\n", r);
}

static void adv_start_legacy(void)
{
	struct bt_le_adv_param *adv_param =
		BT_LE_ADV_PARAM(BT_LE_ADV_OPT_CONNECTABLE |
				BT_LE_ADV_OPT_ONE_TIME,
				BT_GAP_ADV_FAST_INT_MIN_2,
				BT_GAP_ADV_FAST_INT_MAX_2,
				NULL);
	int r;

	r = bt_le_adv_start(adv_param, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
	printk("Start advertiser: %d\n", r);
}

#if defined(CONFIG_BT_EXT_ADV)
static int adv_create_ext(const struct bt_conn_le_phy_param *phy)
{
	int err;
	struct bt_le_adv_param param =
		BT_LE_ADV_PARAM_INIT(BT_LE_ADV_OPT_CONNECTABLE |
				     BT_LE_ADV_OPT_EXT_ADV,
				     BT_GAP_ADV_FAST_INT_MIN_2,
				     BT_GAP_ADV_FAST_INT_MAX_2,
				     NULL);

	if (phy) {
		/* Configure options for PHY */
		switch (phy->options) {
		case BT_CONN_LE_PHY_OPT_CODED_S2:
			printk("Coded s=2 is not used for advertisements, but can be used during a "
			       "connection\n");
		case BT_CONN_LE_PHY_OPT_CODED_S8:
			param.options |= BT_LE_ADV_OPT_CODED;
			break;
		case BT_CONN_LE_PHY_OPT_NONE:
			if (phy->pref_rx_phy == BT_GAP_LE_PHY_1M) {
				param.options |= BT_LE_ADV_OPT_NO_2M;
			}
		default:
			break;
		}
	}

	err = bt_le_ext_adv_create(&param, NULL, &adv);
	if (err) {
		printk("Failed to create advertiser set (err %d)\n", err);
		return err;
	}

	printk("Created adv: %p\n", adv);

	err = bt_le_ext_adv_set_data(adv, ad, ARRAY_SIZE(ad), NULL, 0);
	if (err) {
		printk("Failed to set advertising data (err %d)\n", err);
		return err;
	}

	adv_set_created = true;

	return 0;
}

static void adv_start_extended(void)
{
	int r;

	r = bt_le_ext_adv_start(adv, NULL);
	printk("Advertiser %p set started: %d\n", adv, r);
}
#endif

static void adv_start(void)
{
#if defined(CONFIG_BT_EXT_ADV)
	if (adv_ext) {
		adv_start_extended();
	} else
#endif
	{
		adv_start_legacy();
	}
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	struct bt_conn_info info = {0};
	int err;

	printk("Disconnected (reason 0x%02x)\n", reason);

	connection_params_set = false;
	test_ready = false;
	if (default_conn) {
		bt_conn_unref(default_conn);
		default_conn = NULL;
	}

	err = bt_conn_get_info(conn, &info);
	if (err) {
		printk("Failed to get connection info (%d)\n", err);
		return;
	}

	err = k_work_submit(&restart_ble);
	if (err < 0) {
		printk("Failed to submit restart %d", err);
	}
}

static void restart_ble_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	/* Re-connect using previous role */
	if (role_central) {
		scan_start();
	} else {
		adv_start();
	}
}

static bool le_param_req(struct bt_conn *conn, struct bt_le_conn_param *param)
{
	printk("Connection parameters update request received.\n");
	printk("Minimum interval: %d, Maximum interval: %d\n",
	       param->interval_min, param->interval_max);
	printk("Latency: %d, Timeout: %d\n", param->latency, param->timeout);

	return true;
}

static void le_param_updated(struct bt_conn *conn, uint16_t interval,
			     uint16_t latency, uint16_t timeout)
{
	printk("Connection parameters updated.\n"
	       " interval: %d, latency: %d, timeout: %d\n",
	       interval, latency, timeout);

	k_sem_give(&throughput_sem);
}

static void le_phy_updated(struct bt_conn *conn,
			   struct bt_conn_le_phy_info *param)
{
	printk("LE PHY updated: TX PHY %s, RX PHY %s\n",
	       phy2str(param->tx_phy), phy2str(param->rx_phy));

	k_sem_give(&throughput_sem);
}

static void le_data_length_updated(struct bt_conn *conn,
				   struct bt_conn_le_data_len_info *info)
{
	if (!data_length_req) {
		return;
	}

	printk("LE data len updated: TX (len: %d time: %d)"
	       " RX (len: %d time: %d)\n", info->tx_max_len,
	       info->tx_max_time, info->rx_max_len, info->rx_max_time);

	data_length_req = false;
	k_sem_give(&throughput_sem);
}

int read_conn_rssi(int8_t *rssi)
{
	struct net_buf *buf, *rsp = NULL;
	struct bt_hci_cp_read_rssi *cp;
	struct bt_hci_rp_read_rssi *rp;
	uint16_t conn_handle;
	int r;

	if (default_conn == NULL) {
		return -ENOTCONN;
	}

	buf = bt_hci_cmd_create(BT_HCI_OP_READ_RSSI, sizeof(*cp));
	if (!buf) {
		printk("Unable to allocate command buffer\n");
		return -ENOMEM;
	}

	r = bt_hci_get_conn_handle(default_conn, &conn_handle);
	if (r) {
		printk("Unable to get conn handle\n");
		return r;
	}

	cp = net_buf_add(buf, sizeof(*cp));
	cp->handle = sys_cpu_to_le16(conn_handle);

	r = bt_hci_cmd_send_sync(BT_HCI_OP_READ_RSSI, buf, &rsp);
	if (r) {
		uint8_t reason = rsp ? ((struct bt_hci_rp_read_rssi *)rsp->data)->status : 0;
		printk("Read RSSI err: %d reason 0x%02x\n", r, reason);
		return r;
	}

	rp = (void *)rsp->data;
	*rssi = rp->rssi;

	net_buf_unref(rsp);

	return 0;
}

/* Populate handles for request for a connection or advertisement. */
static int assign_handle_info(void)
{
	int r;

	if (default_conn) {
		handle_type = BT_HCI_VS_LL_HANDLE_TYPE_CONN;
		r = bt_hci_get_conn_handle(default_conn, &handle);
		if (r < 0) {
			printk("No connection handle: %d\n", r);
		}
	} else {
		handle_type = BT_HCI_VS_LL_HANDLE_TYPE_ADV;
		handle = 0;
		r = 0;
	}

	return 0;
}

int set_tx_power(int8_t *tx_pwr_lvl)
{
	struct bt_hci_cp_vs_write_tx_power_level *cp;
	struct bt_hci_rp_vs_write_tx_power_level *rp;
	struct net_buf *buf = NULL;
	struct net_buf *rsp = NULL;
	int r = -EPERM;

	r = assign_handle_info();
	if (r < 0) {
		printk("Unable to assign handle information\n");
		return r;
	}

	buf = bt_hci_cmd_create(BT_HCI_OP_VS_WRITE_TX_POWER_LEVEL, sizeof(*cp));
	if (!buf) {
		printk("Unable to allocate command buffer\n");
		return -ENOMEM;
	}

	cp = net_buf_add(buf, sizeof(*cp));
	cp->handle = sys_cpu_to_le16(handle);
	cp->handle_type = handle_type;
	cp->tx_power_level = *tx_pwr_lvl;

	r = bt_hci_cmd_send_sync(BT_HCI_OP_VS_WRITE_TX_POWER_LEVEL, buf, &rsp);
	if (r) {
		uint8_t reason =
			rsp ? ((struct bt_hci_rp_vs_write_tx_power_level *)rsp->data)->status : 0;
		printk("Set Tx power err: %d reason 0x%02x\n", r, reason);
		return r;
	}

	rp = (void *)rsp->data;
	*tx_pwr_lvl = rp->selected_tx_power;

	net_buf_unref(rsp);

	return r;
}

int get_tx_power(int8_t *tx_pwr_lvl)
{
	struct bt_hci_cp_vs_read_tx_power_level *cp;
	struct bt_hci_rp_vs_read_tx_power_level *rp;
	struct net_buf *buf = NULL; 
	struct net_buf *rsp = NULL;
	int r = -EPERM;
		
	r = assign_handle_info();
	if (r < 0) {
		printk("Unable to assign handle information\n");
		return r;
	}

	buf = bt_hci_cmd_create(BT_HCI_OP_VS_READ_TX_POWER_LEVEL,
				sizeof(*cp));
	if (!buf) {
		printk("Unable to allocate command buffer\n");
		return -ENOMEM;
	}

	cp = net_buf_add(buf, sizeof(*cp));
	cp->handle = sys_cpu_to_le16(handle);
	cp->handle_type = handle_type;

	r = bt_hci_cmd_send_sync(BT_HCI_OP_VS_READ_TX_POWER_LEVEL,
				   buf, &rsp);
	if (r) {
		uint8_t reason = rsp ?
			((struct bt_hci_rp_vs_read_tx_power_level *)
			  rsp->data)->status : 0;
		printk("Read Tx power r: %d reason 0x%02x\n", r, reason);
		return r;
	}

	rp = (void *)rsp->data;
	*tx_pwr_lvl = rp->tx_power_level;

	net_buf_unref(rsp);

	return r;
}



static uint8_t throughput_read(const struct bt_throughput_metrics *met)
{
	printk("[peer] received %u bytes (%u KB)"
	       " in %u GATT writes at %u bps\n",
	       met->write_len, met->write_len / 1024, met->write_count,
	       met->write_rate);

	k_sem_give(&throughput_sem);

	return BT_GATT_ITER_STOP;
}

static void throughput_received(const struct bt_throughput_metrics *met)
{
	static uint32_t kb;
	int8_t rssi = 127;

	if (met->write_len == 0) {
		kb = 0;
		printk("\n");

		return;
	}

	if ((met->write_len / 1024) != kb) {
		kb = (met->write_len / 1024);
		if (print_type == PRINT_TYPE_GRAPHICS) {
			printk("=");
		} else if (print_type == PRINT_TYPE_RSSI) {
			if (read_conn_rssi(&rssi) == 0) {
				printk("%d\n", rssi);
			} else {
				printk("?\n");
			}
		}
	}
}

void select_print_type(const struct shell *shell, enum print_type type)
{
	switch(type) {
	case PRINT_TYPE_GRAPHICS:
		print_type = type;
		shell_print(shell, "Print graphics");
		break;
	case PRINT_TYPE_RSSI:
		print_type = type;
		shell_print(shell, "Print RSSI");
		break;
	default: 
		print_type = PRINT_TYPE_NONE;
		shell_print(shell, "Printing disabled");
		break;
	}
}

static void throughput_send(const struct bt_throughput_metrics *met)
{
	printk("\n[local] received %u bytes (%u KB)"
		" in %u GATT writes at %u bps\n",
		met->write_len, met->write_len / 1024,
		met->write_count, met->write_rate);
}

static const struct bt_throughput_cb throughput_cb = {
	.data_read = throughput_read,
	.data_received = throughput_received,
	.data_send = throughput_send
};

#if defined(CONFIG_DK_LIBRARY)
static struct button_handler button = {
	.cb = button_handler_cb,
};
#endif

void select_role(bool is_central, const struct bt_conn_le_phy_param *phy)
{
	if (role_selected) {
		printk("\nCannot change role after it was selected.\n");
		return;
	}

	if (is_central) {
		printk("\nCentral. Starting scanning\n");
		role_central = true;
		scan_start();
	} else {
#if defined(CONFIG_BT_EXT_ADV)
		if (phy) {
			adv_ext = true;
			adv_create_ext(phy);
		}
#endif
		printk("\nPeripheral. Starting advertising\n");
		adv_start();
	}

	role_selected = true;

#if defined(CONFIG_DK_LIBRARY)
	/* The role has been selected, buttons are not needed any more. */
	remove_button_handlers();
#endif
}

#if defined(CONFIG_DK_LIBRARY)
static void remove_button_handlers(void)
{
	int err;

	err = dk_button_handler_remove(&button);
	if (err) {
		printk("Button disable error: %d\n", err);
	}
}

static void button_handler_cb(uint32_t button_state, uint32_t has_changed)
{
	ARG_UNUSED(has_changed);

	if (button_state & DK_BTN1_MSK) {
		select_role(true, NULL);
	} else if (button_state & DK_BTN2_MSK) {
		select_role(false, NULL);
	}
}

static void buttons_init(void)
{
	int err;

	err = dk_buttons_init(NULL);
	if (err) {
		printk("Buttons initialization failed.\n");
		return;
	}

	/* Add dynamic buttons handler. Buttons should be activated only
	 * when choosing the board role.
	 */
	dk_button_handler_add(&button);
}
#endif

static int connection_configuration_set(const struct shell *shell,
			const struct bt_le_conn_param *conn_param,
			const struct bt_conn_le_phy_param *phy,
			const struct bt_conn_le_data_len_param *data_len)
{
	int err;
	struct bt_conn_info info = {0};

	err = bt_conn_get_info(default_conn, &info);
	if (err) {
		shell_error(shell, "Failed to get connection info %d", err);
		return err;
	}

	if (info.role != BT_CONN_ROLE_CENTRAL) {
		shell_error(shell,
		"'run' command shall be executed only on the central board");
	}

	/* Only change PHY if the user requests it. */
	if (phy) {
		err = bt_conn_le_phy_update(default_conn, phy);
		if (err) {
			shell_error(shell, "PHY update failed: %d\n", err);
			return err;
		}

		shell_print(shell, "PHY update pending");
		err = k_sem_take(&throughput_sem, THROUGHPUT_CONFIG_TIMEOUT);
		if (err) {
			shell_error(shell, "PHY update timeout");
			return err;
		}
	}

	if (info.le.data_len->tx_max_len != data_len->tx_max_len) {
		data_length_req = true;

		err = bt_conn_le_data_len_update(default_conn, data_len);
		if (err) {
			shell_error(shell, "LE data length update failed: %d",
				    err);
			return err;
		}

		shell_print(shell, "LE Data length update pending");
		err = k_sem_take(&throughput_sem, THROUGHPUT_CONFIG_TIMEOUT);
		if (err) {
			shell_error(shell, "LE Data Length update timeout");
			return err;
		}
	}

	if (info.le.interval != conn_param->interval_max) {
		err = bt_conn_le_param_update(default_conn, conn_param);
		if (err) {
			shell_error(shell,
				    "Connection parameters update failed: %d",
				    err);
			return err;
		}

		shell_print(shell, "Connection parameters update pending");
		err = k_sem_take(&throughput_sem, THROUGHPUT_CONFIG_TIMEOUT);
		if (err) {
			shell_error(shell,
				    "Connection parameters update timeout");
			return err;
		}
	}

	return 0;
}

int test_run(const struct shell *shell,
	     const struct bt_le_conn_param *conn_param,
	     const struct bt_conn_le_phy_param *phy,
	     const struct bt_conn_le_data_len_param *data_len)
{
	int err;
	uint64_t stamp;
	int64_t delta;
	uint32_t data = 0;
	int8_t rssi;

	const char *img_ptr = img;
	char str_buf[7];
	int str_len;


	/* a dummy data buffer */
	static char dummy[495];

	if (!default_conn) {
		shell_error(shell, "Device is disconnected %s",
			    "Connect to the peer device before running test");
		return -EFAULT;
	}

	if (role_selected && !role_central) {
		shell_error(shell,
		"'run' command shall be executed only on the central board");
		return -EPERM;
	}

	if (!test_ready) {
		shell_error(shell, "Device is not ready."
			"Please wait for the service discovery and MTU exchange end");
		return -EPERM;
	}

	shell_print(shell, "\n==== Starting throughput test ====");

	/* Some values can only be set once per connection */
	if (!connection_params_set) {
		err = connection_configuration_set(shell, conn_param, phy, data_len);
		if (err) {
			return err;
		} else {
			connection_params_set = true;
		}
	}

	/* Make sure that all BLE procedures are finished. */
	k_sleep(K_MSEC(500));

	/* reset peer metrics */
	err = bt_throughput_write(&throughput, dummy, 1);
	if (err) {
		shell_error(shell, "Reset peer metrics failed.");
		return err;
	}

	/* get cycle stamp */
	stamp = k_uptime_get_32();

	if (IS_ENABLED(CONFIG_BT_THROUGHPUT_FILE)) {
		while (*img_ptr) {
			err = bt_throughput_write(&throughput, dummy, 495);
			if (err) {
				shell_error(shell, "GATT write failed (err %d)", err);
				break;
			}

			/* The image size controls how much data is sent. */
			str_len = (*img_ptr == '\x1b') ? 6 : 1;
			memcpy(str_buf, img_ptr, str_len);
			str_buf[str_len] = '\0';
			img_ptr += str_len;
			if (print_type == PRINT_TYPE_GRAPHICS) {
				shell_fprintf(shell, SHELL_NORMAL, "%s", str_buf);
			} else if (print_type == PRINT_TYPE_RSSI) {
				if (read_conn_rssi(&rssi) == 0) {
					shell_fprintf(shell, SHELL_NORMAL, "%d\n", rssi);
				}
			}
			data += 495;
		}
	} else {
		delta = 0;
		while (true) {
			err = bt_throughput_write(&throughput, dummy, 495);
			if (err) {
				shell_error(shell, "GATT write failed (err %d)", err);
				break;
			}
			data += 495;
			if (k_uptime_get_32() - stamp > CONFIG_BT_THROUGHPUT_DURATION) {
				break;
			}
		}
	}

	delta = k_uptime_delta(&stamp);

	printk("\nDone\n");
	printk("[local] sent %u bytes (%u KB) in %lld ms at %llu kbps\n",
	       data, data / 1024, delta, ((uint64_t)data * 8 / delta));

	/* read back char from peer */
	err = bt_throughput_read(&throughput);
	if (err) {
		shell_error(shell, "GATT read failed (err %d)", err);
		return err;
	}

	k_sem_take(&throughput_sem, THROUGHPUT_CONFIG_TIMEOUT);

	instruction_print();

	return 0;
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = connected,
	.disconnected = disconnected,
	.le_param_req = le_param_req,
	.le_param_updated = le_param_updated,
	.le_phy_updated = le_phy_updated,
	.le_data_len_updated = le_data_length_updated
};

int main(void)
{
	int err;

	printk("Starting Bluetooth Throughput example on %s\n", CONFIG_BOARD);
	printk("Version %s\n", VERSION_STR);

	err = bt_enable(NULL);
	if (err) {
		printk("Bluetooth init failed (err %d)\n", err);
		return 0;
	}

	printk("Bluetooth initialized\n");

	scan_init();

	err = bt_throughput_init(&throughput, &throughput_cb);
	if (err) {
		printk("Throughput service initialization failed.\n");
		return 0;
	}

	printk("\n");
#if defined(CONFIG_DK_LIBRARY)
	printk("Press button 1 or type \"central\" on the central board.\n");
	printk("Press button 2 or type \"peripheral\" on the peripheral board.\n");
	buttons_init();
#else
	printk("Type \"central\" on the central board.\n");
	printk("Type \"peripheral\" on the peripheral board.\n");
#endif

	return 0;
}
