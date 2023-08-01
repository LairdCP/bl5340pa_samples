#
# Copyright (c) 2018-2023 Nordic Semiconductor
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#

cmake_minimum_required(VERSION 3.20.0)

set(DTC_OVERLAY_FILE "dts.overlay")

# Allow testing of Laird Connectivity FEM driver with Nordic DKs
# If child_image/hci_rpmsg.conf exists, then it must be explicitly set
# before using append.
if(MOCK_LCZ_FEM AND BOARD STREQUAL nrf5340dk_nrf5340_cpuapp
    AND hci_rpmsg_SHIELD STREQUAL nrf21540_ek)
    message("Mocking Laird Connectivity Zephyr FEM Driver on nrf5340dk")
	set(hci_rpmsg_OVERLAY_CONFIG ${CMAKE_SOURCE_DIR}/child_image/hci_rpmsg.conf)
    list(APPEND hci_rpmsg_OVERLAY_CONFIG ${CMAKE_SOURCE_DIR}/fem.conf)
    list(APPEND hci_rpmsg_DTC_OVERLAY_FILE ${CMAKE_SOURCE_DIR}/child_image/mock_lcz_fem.nrf21540_ek.overlay)
endif()

# Make it easier to make FEM builds for multiple boards that are similar.
# BL5340PA configuration can be specified in dts/board file or overlay.
# (overlay takes precedence)
#
if(BOARD STREQUAL bl5340pa_dvk_cpuapp)
	set(hci_rpmsg_OVERLAY_CONFIG ${CMAKE_SOURCE_DIR}/child_image/hci_rpmsg.conf)
    list(APPEND hci_rpmsg_OVERLAY_CONFIG ${CMAKE_SOURCE_DIR}/fem.conf)
endif()

if(MOCK_LCZ_FEM AND BOARD STREQUAL nrf21540dk_nrf52840)
    message("Mocking Laird Connectivity Zephyr FEM Driver on nrf21540dk")
    list(APPEND OVERLAY_CONFIG ${CMAKE_SOURCE_DIR}/fem.conf)
    list(APPEND DTC_OVERLAY_FILE ${CMAKE_SOURCE_DIR}/boards/mock_lcz_fem.nrf21540dk_nrf52840.overlay)
endif()

# Generate build ID based on UTC timestamp
string(TIMESTAMP build_time "%s" UTC)
message("build time: ${build_time}")
set(BUILD_TIME ${build_time})
# This configuration file will get automatically merged.
configure_file(${CMAKE_SOURCE_DIR}/build_time.conf.in ${CMAKE_BINARY_DIR}/generated_build_time.conf)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(throughput)

FILE(GLOB app_sources src/*.c)
# NORDIC SDK APP START
target_sources(app PRIVATE
	${app_sources}
)
# NORDIC SDK APP END

zephyr_library_include_directories(${ZEPHYR_BASE}/samples/bluetooth)
