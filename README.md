# ESP32 Home Sentinel

A touch-only motion dashboard built with an ESP32-2432S028 CYD and an HC-SR501 PIR sensor.

## Features

- `SAFE`: green screen, alarm is disarmed.
- `ARMED`: amber screen, the PIR sensor is monitored.
- `ALERT`: red screen when motion is detected.
- The on-screen button changes between `ARM`, `DISARM`, and `RESET`.
- No external LEDs, buttons, or buzzer are used.

## Hardware

- ESP32-2432S028 (Cheap Yellow Display, CYD)
- HC-SR501 PIR Motion Sensor
- Breadboard and jumper wires for the PIR connection

## Wiring

Use the ESP32 3.3V pin for the PIR sensor. Connect every GND to the ESP32 GND.

| Component | Pin | ESP32 GPIO |
| --- | --- | ---: |
| HC-SR501 | VCC | 5V or VIN |
| HC-SR501 | GND | GND |
| HC-SR501 | OUT | GPIO 22 |

Do not connect an ESP32 GPIO directly to 5V. The HC-SR501 OUT pin is normally compatible with ESP32 logic, but power the sensor and check your module before final assembly.

## CYD display mapping

This project targets the common ESP32-2432S028 / TPM408-2.8 layout:

| Function | GPIO |
| --- | ---: |
| TFT SCLK / MOSI / MISO | 14 / 13 / 12 |
| TFT DC / CS | 2 / 15 |
| TFT backlight | 21 |
| XPT2046 touch SCLK / MOSI / MISO | 25 / 32 / 39 |
| XPT2046 touch CS / IRQ | 33 / 36 |

GPIO 22 is reserved for the PIR input. The firmware does not use external buttons, LEDs, or a buzzer.

## Run it

1. Open this folder in VS Code with the PlatformIO extension.
2. Connect the ESP32 by USB.
3. Select **Upload**.
4. Let the PIR warm up for about 30-60 seconds.
5. Tap ARM, wait a few seconds, then move in front of the sensor.

## Suggested GitHub roadmap

1. Add a startup countdown so the PIR can warm up before arming.
2. Add a settings mode for PIR sensitivity.
3. Add a small event counter and a wiring photo.
4. Add a short demo video or GIF to the GitHub README.
