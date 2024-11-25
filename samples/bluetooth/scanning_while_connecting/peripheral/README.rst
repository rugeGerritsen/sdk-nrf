.. _bt_peripheral_with_multiple_identities:

Bluetooth: Peripheral with multiple identities
##############################################

.. contents::
   :local:
   :depth: 2

The sample demonstrates how to a single device can be used to pretend it is multiple devices.
This can be used to test a central device that requires connections to multiple peripheral devices when you don't have many development kits available.

Requirements
************

The sample supports the following development kits:

.. table-from-sample-yaml::

Overview
********

Each Bluetooth device is identified by its identity address.
When a peripheral is advertising, it uses this identity address or generates an address from this address using an identity resolving key (IRK).
The sample application starts multiple connectable advertisers, each with its own identity.

#. Identities are created with the API :c:func:`bt_id_create()`.
#. To assign a given identity to an advertiser, the field :c:member:`bt_le_adv_param.id` is set.
#. To obtain the identity used for a given connection, the :c:func:`bt_conn_get_info()` is called to obtain :c:member:`bt_conn_info.id`.

Configuration
*************

|config|

Configuration options
=====================

Check and configure the following Kconfig options:

.. _CONFIG_BT_ID_MAX:

CONFIG_BT_ID_MAX
   This configuration defines how many identities will be used.

.. _CONFIG_BT_MAX_CONN:

CONFIG_BT_MAX_CONN
   This configuration defines how many connections which can be established.
   The sample expects this configuration to be set to the same value as **CONFIG_BT_ID_MAX**.

.. _CONFIG_BT_EXT_ADV_MAX_ADV_SET:

CONFIG_BT_EXT_ADV_MAX_ADV_SET
   This configuration defines how the number of advertising sets that are available.
   The sample expects this configuration to be set to the same value as **CONFIG_BT_ID_MAX**.

Building and running
********************

.. |sample path| replace:: :file:`samples/bluetooth/scanning_while_connecting`

.. include:: /includes/build_and_run.txt

Testing
=======

|test_sample|

1. |connect_kit|
#. |connect_terminal|
#. Start the `nRF Connect for Mobile`_ application on your smartphone or tablet.

   There should now be multiple devices advertising with the name ``Nordic multi adv sets``
#. Connect to the device and observe .... is printed.

Alternatively, this sample can be tested with the sample :ref:`bt_scanning_while_connecting`.
That sample will connect to all the advertising identities.

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
