#
# Copyright (c) 2020 Nordic Semiconductor
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(app_with_child)

# NORDIC SDK APP START
target_sources(app PRIVATE src/main.c)
# NORDIC SDK APP END

# child compilation fails with "pm_config.h: No such file or directory"
add_child_image(
    NAME "dummy_child"
    SOURCE_DIR "${NRF_DIR}/samples/app_with_child/dummy_child"
    DOMAIN "CPUNET"
    BOARD nrf5340dk_nrf5340_cpunet
)

# Also tried with some existing child image, didn't work either
# add_child_image(
#     NAME "hci_rpmsg"
#     SOURCE_DIR "${ZEPHYR_BASE}/samples/bluetooth/hci_rpmsg"
#     DOMAIN "CPUNET"
#     BOARD nrf5340dk_nrf5340_cpunet
# )
