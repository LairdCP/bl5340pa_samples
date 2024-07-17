#
# Copyright (c) 2023-2024 Laird Connectivity
#
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

message(NOTICE "Board: ${BOARD}")

# Generate build ID based on UTC timestamp
string(TIMESTAMP build_time "%s" UTC)
message("build time: ${build_time}")
set(BUILD_TIME ${build_time})
# This configuration file will get automatically merged.
configure_file(${CMAKE_SOURCE_DIR}/build_time.conf.in ${CMAKE_BINARY_DIR}/generated_build_time.conf)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(sleepy_advertiser)

target_sources(app PRIVATE src/main.c)
