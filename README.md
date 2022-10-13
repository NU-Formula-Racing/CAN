# CAN Bus

By Charlie Kalousek - October 2022

## Breif

This is NFR's CAN Bus repository. Expect this to be a submodule in your own repository at some point. This might be merged into a larger shared repository with more than CAN code in the future.

That also means that be very careful when you're updating this repo because if you break something then you might break CAN for everyone. If you want to experiment, make your own branch and do your testing there, then submit a pull request (PR) to merge it back into main.

### CAN Background

If you don't know what CAN is, read this document [here](https://docs.google.com/document/d/1XAJNA9vFf0h5ruzI_uM2yF3VfZlPSxpRNXcBMb-HSx4/edit?usp=sharing)

This project requires VS Code's PlatformIO extension to work. If you aren't familiar with PlatformIO or VS Code, see this setup tutorial [here](https://docs.google.com/document/d/1lHxgOpmPJfi5fyBfCM1aA54dtm3wqHcFUew8G-NeXwE/edit?usp=sharing)

To read the CAN database (DBC file), use [Kvaser Database Editor 3](https://www.kvaser.com/download/)

### Coverage

This code base is intended to offer CAN functionality for all the hardware we have on the car via a single interface. This allows for the particular hardware you're working on to be abstracted away and for uniform libraries for all our hardware to be written.

This code base currently includes the following hardware platforms:

- Teensy 4.0, 4.1
- ESP32

More support to come in the future.

## How to Use

If you are on Teensy - 
    Include the library in your code by using: `#include "teensy_can.h"`

If you are on ESP32 -
    Include the library in your code by using: `#include "esp_can.h"`

### CAN Signals

Every CAN message, TX or RX, has signals, which need to be instantiated before the message. You should never put the same signal in multiple messages. The CANSignal class is used to create these signals.

The signal type, starting position, length, factor, offset, and signedness of the signal are all templated arguments.

A Signal type can be any basic type.

This is the format for constructing a signal:

`CANSignal<SignalType, start_position, length, factor (using CANTemplateConvertFloat due to C++ limitations), offset (using CANTemplateConvertFloat due to C++ limitations), is_signed>`

An Example Signal Constructor Could Look Like This:

`CANSignal<float, 0, 16, CANTemplateConvertFloat(0.01), CANTemplateConvertFloat(0), true> float_tx_signal{};`
