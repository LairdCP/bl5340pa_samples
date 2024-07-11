/*
 * Copyright (c) 2022 Nordic Semiconductor ASA
 * Copyright (c) 2023-2024 Ezurio
 * 
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef THROUGHPUT_MAIN_H_
#define THROUGHPUT_MAIN_H_

#include <zephyr/shell/shell.h>
#include <zephyr/bluetooth/conn.h>

/** These are the different options for what is printed during the throughput test. */
enum print_type {
	PRINT_TYPE_NONE = 0,
	PRINT_TYPE_GRAPHICS,
	PRINT_TYPE_RSSI,
};

/**
 * @brief Run the test
 *
 * @param shell       Shell instance where output will be printed.
 * @param conn_param  Connection parameters.
 * @param phy         Phy parameters (if non-null).
 * @param data_len    Maximum transmission payload.
 */
int test_run(const struct shell *shell,
	     const struct bt_le_conn_param *conn_param,
	     const struct bt_conn_le_phy_param *phy,
	     const struct bt_conn_le_data_len_param *data_len);

/**
 * @brief Set the board into a specific role.
 *
 * @param is_central true for central role, false for peripheral role.
 * @param phy is NULL for no change (1M), otherwise extended advertising will be attempted.
 */
void select_role(bool is_central, const struct bt_conn_le_phy_param *phy);

/**
 * @brief Select what is printed during test
 */
void select_print_type(const struct shell *shell, enum print_type type);

/* @brief Set power. Sets ad power if advertising or idle.
 * Sets connection power if in a connection.
 * Actual power is assigned to level.
 * Power table limits are not reflected in level.
 * Power model limits are reflected in level (CE).
 */
int set_tx_power(int8_t *tx_pwr_lvl);

/* @brief Get tx power.
 * @ref levels behave the same as set.
 */
int get_tx_power(int8_t *tx_pwr_lvl);

/* @brief Read connection RSSI */
int read_conn_rssi(int8_t *rssi);

#endif /* THROUGHPUT_MAIN_H_ */
