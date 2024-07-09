/*
 * Copyright (c) 2015-2016 Intel Corporation
 * Copyright (c) 2019 Nordic Semiconductor ASA
 * Copyright (c) 2023 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(main, CONFIG_LOG_DEFAULT_LEVEL);

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/init.h>
#include <zephyr/pm/device.h>
#include <zephyr/sys/poweroff.h>

#include <zephyr/bluetooth/bluetooth.h>

#define VERSION_STR "1.2.0." CONFIG_BUILD_TIME

#if defined(CONFIG_BT)
#define DEVICE_NAME	CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LEN (sizeof(DEVICE_NAME) - 1)

#if defined(CONFIG_ADVERTISE)

#if defined(CONFIG_ADVERTISE_FAST)
#define RATE BT_GAP_ADV_FAST_INT_MIN_1
#else
#define RATE BT_GAP_ADV_SLOW_INT_MIN
#endif

#if defined(CONFIG_SCANNABLE)
#define OPT BT_LE_ADV_OPT_SCANNABLE
#else
#define OPT BT_LE_ADV_OPT_NONE
#endif

#define ADV_PARAM BT_LE_ADV_PARAM(OPT, RATE, (RATE + 1), NULL)

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, BT_LE_AD_NO_BREDR),
	BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN),
};
#endif
#endif

#if defined(CONFIG_BT)
static void start_advertising(void)
{
#if defined(CONFIG_ADVERTISE)
	int rc;

	rc = bt_le_adv_start(ADV_PARAM, ad, ARRAY_SIZE(ad), NULL, 0);
	LOG_INF("Advertising start: %d", rc);
#endif
}

static void bt_ready(int err)
{
	LOG_INF("Bluetooth ready: %d", err);
	if (err) {
		return;
	}

	start_advertising();
}
#endif

int main(void)
{
	int rc;
	int rc2;
	const struct device *const cons = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));

	if (!device_is_ready(cons)) {
		LOG_ERR("%s: device not ready.", cons->name);
		return 0;
	}

	LOG_INF("BT sleepy advertiser on %s", CONFIG_BOARD);
	LOG_INF("Version %s\n", VERSION_STR);

#if defined(CONFIG_BT)
	rc = bt_enable(bt_ready);
	if (rc < 0) {
		LOG_ERR("Bluetooth init: %d", rc);
		return 0;
	}
	k_sleep(K_SECONDS(1));
#endif

	LOG_INF("Sleep %u s with UART off", CONFIG_SLEEP_DURATION_SECONDS);
	rc = pm_device_action_run(cons, PM_DEVICE_ACTION_SUSPEND);
	k_sleep(K_SECONDS(CONFIG_SLEEP_DURATION_SECONDS));
	rc2 = pm_device_action_run(cons, PM_DEVICE_ACTION_RESUME);
	LOG_INF("suspend status: %d resume status: %d", rc, rc2);

	LOG_INF("Entering system off; press reset button to restart");
	rc = pm_device_action_run(cons, PM_DEVICE_ACTION_SUSPEND);
	k_sleep(K_SECONDS(1));
	sys_poweroff();

	LOG_ERR("System off failed");
	return 0;
}
