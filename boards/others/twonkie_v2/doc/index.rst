.. SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
.. SPDX-License-Identifier: Apache-2.0

.. zephyr:board:: twonkie_v2

Overview
********

Twonkie v2 is an open-source hardware USB Power Delivery sniffer based on the
STM32F072CB microcontroller. It provides access to the USB Type-C CC signals
for monitoring and test signal injection, with onboard voltage and current
monitoring circuitry.

Hardware
********

Connections
===========

LEDs
----

.. list-table::
   :header-rows: 1

   * - Function
     - MCU pin
     - Notes
   * - Red LED
     - PB11
     - Active low; ``led0`` alias
   * - Green LED
     - PB10
     - Active low; ``led1`` alias
   * - Blue LED
     - PB14
     - Active low; ``led2`` alias

UART and USB
------------

.. list-table::
   :header-rows: 1

   * - Function
     - MCU pin
     - Notes
   * - USART1 TX
     - PA9
     - Default console transmit, 115200 baud
   * - USART1 RX
     - PA10
     - Default console receive
   * - USB D-
     - PA11
     - MCU USB device connection
   * - USB D+
     - PA12
     - MCU USB device connection

INA237 monitor bus
------------------

**I2C pin connections**

.. list-table::
   :header-rows: 1

   * - Function
     - MCU pin
     - Notes
   * - I2C1 SCL
     - PB6
     - Shared clock for the INA237 monitors, 100 kHz
   * - I2C1 SDA
     - PB7
     - Shared data for the INA237 monitors

**Monitor addresses and startup modes**

.. list-table::
   :header-rows: 1

   * - Monitor
     - I2C address
     - Default mode
   * - VBUS
     - ``0x40``
     - Continuous bus and shunt voltage measurement
   * - VCONN (CC1 only)
     - ``0x45``
     - Shutdown until enabled by the application

The VCONN INA237 is connected only to the CC1 path. It cannot measure VCONN
voltage or current when VCONN is carried on CC2.

CC voltage sensing
------------------

.. list-table::
   :header-rows: 1

   * - Function
     - MCU pin
     - Notes
   * - CC1 voltage sense
     - PA1
     - ADC channel 1, 12-bit resolution
   * - CC2 voltage sense
     - PA3
     - ADC channel 3, 12-bit resolution
   * - CC1 sense enable
     - PA0
     - Push-pull output, initially high (enabled)
   * - CC2 sense enable
     - PA2
     - Push-pull output, initially high (enabled)

USB PD injection
----------------

.. list-table::
   :header-rows: 1

   * - Function
     - MCU pin
     - Notes
   * - CC1 TX data
     - PB4
     - Push-pull output, initially low
   * - CC1 TX enable
     - PA14
     - Push-pull output, initially low (disabled)
   * - CC2 TX data
     - PA6
     - Push-pull output, initially low
   * - CC2 TX enable
     - PC13
     - Push-pull output, initially low (disabled)

Type-C injection
----------------

.. list-table::
   :header-rows: 1

   * - Function
     - MCU pin
     - Notes
   * - CC1 Rp (default USB current)
     - PA15
     - Open-drain output, initially high (disconnected)
   * - CC1 Rp (1.5 A)
     - PB5
     - Open-drain output, initially high (disconnected)
   * - CC1 Rp (3.0 A)
     - PA13
     - Open-drain output, initially high (disconnected)
   * - CC1 Rd
     - PB3
     - Open-drain output, initially high (disconnected)
   * - CC1 Ra
     - PB8
     - Open-drain output, initially high (disconnected)
   * - CC2 Rp (default USB current)
     - PB12
     - Open-drain output, initially high (disconnected)
   * - CC2 Rp (1.5 A)
     - PB15
     - Open-drain output, initially high (disconnected)
   * - CC2 Rp (3.0 A)
     - PC14
     - Open-drain output, initially high (disconnected)
   * - CC2 Rd
     - PB13
     - Open-drain output, initially high (disconnected)
   * - CC2 Ra
     - PA8
     - Open-drain output, initially high (disconnected)

Supported Features
==================

.. zephyr:board-supported-hw::

System Clock
============

The internal 8 MHz oscillator (HSI) feeds the PLL to generate a 48 MHz system
clock. The AHB and APB buses also run at 48 MHz.

USB uses the separate internal 48 MHz oscillator (HSI48), with automatic
trimming against USB start-of-frame packets through the clock recovery system
(CRS). No external crystal is required.

Programming and Debugging
*************************

.. zephyr:board-supported-runners::

The board supports flashing through the STM32 ROM USB DFU bootloader using
``dfu-util``. The MCU must be in DFU mode before flashing.

Entering DFU Mode
=================

#. Disconnect the board's Micro-B USB cable.
#. Hold the bootloader button while reconnecting the Micro-B USB cable.
#. Release the button after the STM32 bootloader enumerates as USB device
   ``0483:df11``.

The bootloader button selects the boot mode at startup; pressing it while the
application is running does not reset the MCU.

Flashing an Application
======================

Blinky
------

For example, build the :zephyr:code-sample:`blinky` sample, enter DFU mode as
described above, and flash the board:

.. zephyr-app-commands::
   :zephyr-app: samples/basic/blinky
   :board: twonkie_v2
   :goals: build flash

Blinky uses the red LED through the ``led0`` alias.

Hello World
-----------

To check console output, build the :zephyr:code-sample:`hello_world` sample,
enter DFU mode again, and flash it:

.. zephyr-app-commands::
   :zephyr-app: samples/hello_world
   :board: twonkie_v2
   :goals: build flash
   :build-dir: build/hello_world

This sample inherits the board's default UART console configuration.

To use the Micro-B USB cable for console output instead, build with the
:ref:`snippet-cdc-acm-console` snippet:

.. zephyr-app-commands::
   :zephyr-app: samples/hello_world
   :board: twonkie_v2
   :snippets: cdc-acm-console
   :goals: build flash
   :build-dir: build/hello_world_usb

The snippet enables the USB device stack, initializes the CDC ACM serial device
at boot, and selects it as the console.

After flashing, open the USB serial port in a terminal (for example,
``/dev/ttyACM0`` on Linux). Hello World prints the following greeting once per
boot:

.. code-block:: text

   Hello World! twonkie_v2/stm32f072xb

Serial Console
==============

The default console uses USART1 at 115200 baud, with 8 data bits, no parity,
and 1 stop bit. Connect a 3.3 V USB-to-UART adapter's RX input to PA9 (MCU TX),
its TX output to PA10 (MCU RX), and its ground to board ground.

Open the adapter's serial port with these settings before starting Hello World.
If the application has already started, leave the terminal open and power-cycle
the board without holding the bootloader button. The sample prints once at
startup:

.. code-block:: text

   Hello World! twonkie_v2/stm32f072xb

Applications can select a USB CDC ACM console instead. USB serial requires
application configuration; enabling the USB controller in the board devicetree
alone does not create a serial port.

References
**********

* `Twonkie hardware repository`_
* `Twonkie v2 schematic`_

.. _Twonkie hardware repository:
   https://github.com/dojoe/Twonkie
.. _Twonkie v2 schematic:
   https://github.com/dojoe/Twonkie/blob/master/hw/v2.0/twonkie.pdf
