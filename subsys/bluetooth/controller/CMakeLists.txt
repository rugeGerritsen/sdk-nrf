#
# Copyright (c) 2018 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
#

zephyr_library()

zephyr_library_sources(
  multithreading_lock.c
  hci_driver.c
  power_clock_driver.c
  rng_driver.c
  )

zephyr_library_sources_ifdef(
  CONFIG_BT_CTLR_CRYPTO
  crypto.c
)

zephyr_library_sources_ifdef(
  CONFIG_SOC_FLASH_NRF_LL_NRFXLIB
  flash_driver.c)

zephyr_library_link_libraries(subsys__bluetooth)
