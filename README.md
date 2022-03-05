# app_template

[![test](https://github.com/mdvorak-iot/esp-app-template/actions/workflows/test.yml/badge.svg)](https://github.com/mdvorak-iot/esp-app-template/actions/workflows/test.yml)

TODO description.

## Usage

To provision WiFi, use provisioning app:

* [Android BLE Provisioning app](https://play.google.com/store/apps/details?id=com.espressif.provble)
* [iOS BLE Provisioning app](https://apps.apple.com/in/app/esp-ble-provisioning/id1473590141)

To initiate provisioning mode, reset the device twice (double tap reset in about 1s interval). Status LED will start flashing rapidly.

## Development

Prepare [ESP-IDF development environment](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/index.html#get-started-get-prerequisites)
.

Configure application with

```
idf.py menuconfig
```

and select `Application configuration` in root menu and configure application parameters.

Flash it via

```
idf.py build flash monitor
```
