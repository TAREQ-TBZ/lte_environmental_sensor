#
# Copyright (c) 2024 Tareq Mhisen
#
# SPDX-License-Identifier: Apache-2.0
#

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(app LANGUAGES C)

target_sources(app PRIVATE
    src/main.c
    src/modem_svc.c
    src/coap_client.c
    #src/udp_client.c
)
