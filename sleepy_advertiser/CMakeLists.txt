#
# Copyright (c) 2023-2024 Ezurio
#
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

message(NOTICE "Board: ${BOARD}")

# Allow testing of Ezurio FEM driver with Nordic DKs
# If child_image/hci_ipc.conf exists, then it must be explicitly set
# before using append.
if(MOCK_LCZ_FEM AND BOARD STREQUAL nrf5340dk/nrf5340/cpuapp
    AND hci_ipc_SHIELD STREQUAL nrf21540ek)
    message("Mocking Ezurio Zephyr FEM Driver on nrf5340dk")
    list(APPEND hci_ipc_OVERLAY_CONFIG ${CMAKE_SOURCE_DIR}/fem.conf)
    list(APPEND hci_ipc_DTC_OVERLAY_FILE ${CMAKE_SOURCE_DIR}/child_image/mock_lcz_fem.nrf21540ek.overlay)
endif()

# Make it easier to make FEM builds for multiple boards that are similar.
# BL5340PA configuration can be specified in dts/board file or overlay.
# (overlay takes precedence)
#
if(BOARD STREQUAL bl5340pa_dvk/nrf5340/cpuapp)
    list(APPEND hci_ipc_OVERLAY_CONFIG ${CMAKE_SOURCE_DIR}/fem.conf)  
endif()

if(MOCK_LCZ_FEM AND BOARD STREQUAL nrf21540dk/nrf52840)
    message("Mocking Ezurio Zephyr FEM Driver on nrf21540dk")
    list(APPEND OVERLAY_CONFIG ${CMAKE_SOURCE_DIR}/fem.conf)
    list(APPEND DTC_OVERLAY_FILE ${CMAKE_SOURCE_DIR}/boards/mock_lcz_fem.nrf21540dk_nrf52840.overlay)
endif()

# Network cpu doesn't have low power uart control
# If SPI control of FEM is used, then network UART isn't available and
# either RTT or USB can be used.
string(FIND "${BOARD}" "nrf5340" index)
if(NOT ${index} EQUAL -1)
if(LOG_TYPE STREQUAL rtt)
    message("RTT")
    list(APPEND hci_ipc_OVERLAY_CONFIG ${CMAKE_SOURCE_DIR}/child_image/rtt_log.conf)
    list(APPEND hci_ipc_DTC_OVERLAY_FILE ${CMAKE_SOURCE_DIR}/child_image/disable_uart.overlay)
else()
    message("NO LOG")
    list(APPEND hci_ipc_OVERLAY_CONFIG ${CMAKE_SOURCE_DIR}/child_image/no_log.conf)
    list(APPEND hci_ipc_DTC_OVERLAY_FILE ${CMAKE_SOURCE_DIR}/child_image/disable_uart.overlay)
endif()
endif() # nrf5340 found

# Generate build ID based on UTC timestamp
string(TIMESTAMP build_time "%s" UTC)
message("build time: ${build_time}")
set(BUILD_TIME ${build_time})
# This configuration file will get automatically merged.
configure_file(${CMAKE_SOURCE_DIR}/build_time.conf.in ${CMAKE_BINARY_DIR}/generated_build_time.conf)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(sleepy_advertiser)

target_sources(app PRIVATE src/main.c)
