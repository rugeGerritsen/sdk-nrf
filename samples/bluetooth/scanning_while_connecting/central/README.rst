.. _bt_scanning_while_connecting:

Bluetooth: Scanning while connecting
####################################

.. contents::
   :local:
   :depth: 2

The sample demonstrates how to reduce the time to establish connections to many devices, typically done when provisioning devices to a network.
The total connection establishment time is reduced by scanning while connecting and by using the filter accept list.

Requirements
************

The sample supports the following development kits:

.. table-from-sample-yaml::

The sample also requires at least one other development kit running a sample advertising with the name set with :kconfig:option:`CONFIG_BT_DEVICE_NAME`.
It is recommended to use the sample :ref:`peripheral_uart`, because this sample acts as multiple peripheral devices.

Overview
********

You can use this sample as a starting point to implement an application designed to provision a network of Bluetooth peripherals.
The approach demonstrated in this sample will reduce the total provisioning time when onboarding more than a couple of devices.
A typical application is the gateway in a network of devices using Periodic Advertising with Responses.

To illustrate how this sample demonstrates improves connection establishment time, it measures the time needed to connect to *N* devices with three different modes.
It will connect to any device which matches the device name set with :kconfig:option:`CONFIG_BT_DEVICE_NAME`.

Sequential scanning and connection establishment
================================================

This is the slowest but also the simplest approach.
Sequential scanning and connection establishment is therefore recommended when the application only needs to connect a handful of devices.

The message sequence chart below illustrates the sequence of events.
After a device is discovered, the application stops scanning and attempts to connect to it.
Once the connection is established, the application starts the scanner again.

.. msc::
    hscale = "1.3";
    App, Stack, Peers;
    App=>Stack [label="scan_start()"];
    Peers=>Stack [label="ADV_IND(A)"];
    Stack=>App [label="scan_recv(A)"];
    App=>Stack [label="scan_stop()"];
    App=>Stack [label="bt_conn_le_create(A)"];
    Peers=>Stack [label="ADV_IND(A)"];
    Stack=>Peers [label="CONNECT_IND(A)"];
    Stack=>App [label="connected_cb(A)"];
    App=>Stack [label="scan_start()"];
    Peers=>Stack [label="ADV_IND(B)"];
    Stack=>App [label="scan_recv(B)"];
    App=>Stack [label="scan_stop()"];
    App=>Stack [label="bt_conn_le_create(B)"];

Concurrent scanning while connecting
====================================

This mode requires the application to enable :kconfig:option:`CONFIG_BT_SCAN_AND_INITIATE_IN_PARALLEL`.
In this mode the scanner is not stopped when the application creates connections.
During this time, the application simply caches the devices it wants to connect to later.
Once the connection establishment procedure is complete, it can continue establishing a connection to the cached device.
This approach saves the time corresponding to one advertising interval per device it wants to connect to.

.. msc::
    App, Stack, Peers;
    App=>Stack [label="scan_start()"];
    Peers=>Stack [label="ADV_IND(A)"];
    Stack=>App [label="scan_recv(A)"];
    App=>Stack [label="bt_conn_le_create(A)"];
    Peers=>Stack [label="ADV_IND(B)"];
    App rbox App [label="Cache address B"];
    Peers=>Stack [label="ADV_IND(A)"];
    Stack=>Peers [label="CONNECT_IND(A)"];
    Stack=>App [label="connected_cb(A)"];
    App=>Stack [label="bt_conn_le_create(B)"];
    Peers=>Stack [label="ADV_IND(B)"];
    Stack=>Peers [label="CONNECT_IND(B)"];
    Stack=>App [label="connected_cb(B)"];

Concurrent scanning while connecting with the filter accept list
================================================================

This mode requires the application to enable :kconfig:option:`CONFIG_BT_FILTER_ACCEPT_LIST` in addition to :kconfig:option:`CONFIG_BT_SCAN_AND_INITIATE_IN_PARALLEL`.
When the application starts the connection establishment procedure with the filter accept list, it can connect to any of the previously cached devices.
This reduces the total connection setup time even more, because the total time no longer relies on the presence of a single cached device.

.. msc::
    hscale = "1.3";
    App, Stack, Peers;
    App=>Stack [label="scan_start()"];
    Peers=>Stack [label="ADV_IND(A)"];
    Stack=>App [label="scan_recv(A)"];
    App=>Stack [label="bt_conn_le_create(A)"];
    Peers=>Stack [label="ADV_IND(B)"];
    Peers=>Stack [label="ADV_IND(C)"];
    Peers=>Stack [label="ADV_IND(D)"];
    App rbox App [label="Cache addresses B, C, D"];
    Peers=>Stack [label="ADV_IND(A)"];
    Stack=>Peers [label="CONNECT_IND(A)"];
    Stack=>App [label="connected_cb(A)"];
    App rbox App [label="Set filter accept list to\nB, C, D"];
    App=>Stack [label="bt_conn_le_create_auto()"];
    Stack rbox Stack [label="The stack will connect to any of\nB,C,D"];
    Peers=>Stack [label="ADV_IND(C)"];
    Stack=>Peers [label="CONNECT_IND(C)"];
    Stack=>App [label="connected_cb(C)"];

.. note::
   This sample application assumes it will never have to cache more devices than what can be stored in the filter accept list.
   For applications where this is not the case, the functions XXXX and XXX need to be updated to handle this.

Configuration
*************

|config|

Configuration options
=====================

Check and configure the following Kconfig options:

.. _CONFIG_BT_MAX_CONN:

CONFIG_BT_MAX_CONN
   This configuration defines how many connections which will be established.
   When set to a higher number, the measured time will depend less on other factors.

Building and running
********************

.. |sample path| replace:: :file:`samples/bluetooth/scanning_while_connecting`

.. include:: /includes/build_and_run.txt

Testing
=======

|test_sample|

1. |connect_kit|
#. |connect_terminal|
#. Observe that the sample connects and prints out how much time it takes to connect to all peripherals.

Sample output
=============

The result should look similar to the following output::

   Starting radio notification callback sample.
   I: SoftDevice Controller build revision:
   I: d6 da c7 ae 08 db 72 6f |......ro
   I: 2a a3 26 49 2a 4d a8 b3 |*.&I*M..
   I: 98 0e 07 7f             |....
   I: HW Platform: Nordic Semiconductor (0x0002)
   I: HW Variant: nRF52x (0x0002)
   I: Firmware: Standard Bluetooth controller (0x00) Version 214.51162 Build 1926957230
   I: Identity: FA:BB:79:57:D6:45 (random)
   I: HCI: version 5.4 (0x0d) revision 0x11fb, manufacturer 0x0059
   I: LMP: version 5.4 (0x0d) subver 0x11fb
