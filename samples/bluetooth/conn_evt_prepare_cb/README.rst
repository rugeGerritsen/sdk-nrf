.. _ble_conn_evt_prepare_cb:

Bluetooth: Connection event prepare callback
############################################

.. contents::
   :local:
   :depth: 2

The Connection event prepare callback sample demonstrates how to use the connection event prepare callback feature.
It uses the :ref:`latency_readme` and the :ref:`latency_client_readme` to showcase how this feature can be used to minimize the time between data sampling and data transmission.

Requirements
************

The sample supports the following development kits:

.. table-from-sample-yaml::

You can use any two of the development kits mentioned above and mix different development kits.

Additionally, the sample requires a connection to a computer with a serial terminal for each of the development kits.

Note that the feature involves triggering a (D)PPI task directly from the SoftDevice Controller link layer, and therefore it is expected that the application and SoftDevice Controller are running on the same core.

Building and running
********************
.. |sample path| replace:: :file:`samples/bluetooth/llpm`

.. include:: /includes/build_and_run.txt

Testing
=======

After programming the sample to both development kits, test it by performing the following steps:

1. Connect to both kits with a terminal emulator (for example, `nRF Connect Serial Terminal`_).
   See :ref:`test_and_optimize` for the required settings and steps.
#. Reset both kits.
#. In one of the terminal emulators, type "c" to start the application on the connected board in the central role.
#. In the other terminal emulator, type "p" to start the application in the peripheral role.
#. Observe that latency measurements are printed in the terminals.

Sample output
=============

The result should look similar to the following output.

- For the central::

   Starting connection event prepare sample.
   I: SoftDevice Controller build revision:
   I: 27 1c 81 55 59 3a 61 a4 |'..UY:a.
   I: 6b c4 55 46 f7 c5 a4 dc |k.UF....
   I: d3 69 67 45             |.igE
   I: HW Platform: Nordic Semiconductor (0x0002)
   I: HW Variant: nRF52x (0x0002)
   I: Firmware: Standard Bluetooth controller (0x00) Version 39.33052 Build 1631213909
   I: Identity: CF:99:32:A5:4B:11 (random)
   I: HCI: version 5.4 (0x0d) revision 0x11de, manufacturer 0x0059
   I: LMP: version 5.4 (0x0d) subver 0x11de
   Choose device role - type c (central) or p (peripheral):
   Central. Starting scanning
   Scanning started
   Device found: FA:BB:79:57:D6:45 (random) (RSSI -33)
   Connected: FA:BB:79:57:D6:45 (random)
   Service discovery completed
   Latency: 2764 us, round trip latency: 52764 us
   Latency: 2734 us, round trip latency: 52734 us
   Latency: 2734 us, round trip latency: 52734 us
   Latency: 2764 us, round trip latency: 52764 us
   Latency: 2764 us, round trip latency: 52764 us

- For the peripheral::

   Starting connection event prepare sample.
   I: SoftDevice Controller build revision:
   I: 27 1c 81 55 59 3a 61 a4 |'..UY:a.
   I: 6b c4 55 46 f7 c5 a4 dc |k.UF....
   I: d3 69 67 45             |.igE
   I: HW Platform: Nordic Semiconductor (0x0002)
   I: HW Variant: nRF52x (0x0002)
   I: Firmware: Standard Bluetooth controller (0x00) Version 39.33052 Build 1631213909
   I: Identity: FA:BB:79:57:D6:45 (random)
   I: HCI: version 5.4 (0x0d) revision 0x11de, manufacturer 0x0059
   I: LMP: version 5.4 (0x0d) subver 0x11de
   Choose device role - type c (central) or p (peripheral):
   Peripheral. Starting advertising
   Advertising started
   Connected: CF:99:32:A5:4B:11 (random)
   Service discovery completed
   Latency: 2612 us, round trip latency: 52612 us
   Latency: 2612 us, round trip latency: 52612 us
   Latency: 2642 us, round trip latency: 52642 us
   Latency: 2642 us, round trip latency: 52642 us
   Latency: 2642 us, round trip latency: 52642 us
   Latency: 2612 us, round trip latency: 52612 us
   Latency: 2612 us, round trip latency: 52612 us
   Latency: 2642 us, round trip latency: 52642 us

Dependencies
*************

This sample uses the following |NCS| libraries:

* :ref:`latency_readme`
* :ref:`latency_client_readme`

This sample uses the following `sdk-nrfxlib`_ libraries:

* :ref:`nrfxlib:softdevice_controller`

In addition, it uses the following Zephyr libraries:

* :file:`include/console.h`
* :ref:`zephyr:kernel_api`:

  * :file:`include/kernel.h`

* :file:`include/sys/printk.h`
* :file:`include/zephyr/types.h`
* :ref:`zephyr:bluetooth_api`:

  * :file:`include/bluetooth/bluetooth.h`
  * :file:`include/bluetooth/conn.h`
  * :file:`include/bluetooth/gatt.h`
  * :file:`include/bluetooth/scan.h`
  * :file:`include/bluetooth/gatt_dm.h`
  * :file:`include/bluetooth/conn_evt_prepare_cb.h`
