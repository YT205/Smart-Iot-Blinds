# ESP32 Smart Blinds

Smart motorized blinds built using an **ESP32**, stepper motor, environmental sensor, SinricPro, and local WebSocket control.

The blinds can be remotely opened, closed, or moved to a specific position while also reporting room temperature and humidity.

## Features

* ESP32-based smart blind controller
* Stepper motor position control
* 0–100% blind positioning
* SinricPro smart-home integration
* Local browser control
* Real-time WebSocket communication
* Temperature and humidity monitoring
* Direct I²C communication with an SHT31 sensor
* No external temperature sensor library required
* Automatic Wi-Fi access point fallback
* Non-blocking stepper motor control

## Hardware

* ESP32
* Stepper motor
* Stepper motor driver
* SHT31 temperature/humidity sensor
* Motorized blind mechanism
* Power supply

## Communication

The ESP32 supports two methods of controlling the blinds:

### SinricPro

SinricPro provides cloud-based smart-home control and allows the blind position to be changed remotely.

Temperature and humidity readings are also periodically sent to SinricPro.

### Local WebSocket Control

The ESP32 hosts its own web interface and WebSocket server.

Opening the ESP32's IP address in a browser provides controls for:

* Open
* Close
* Stop
* Enable/disable motor
* Set blind position from 0–100%
* View live temperature
* View live humidity

## Sensor Communication

The SHT31 communicates with the ESP32 using I²C.

Instead of relying on a sensor library, the ESP32 communicates directly with the SHT31 using the Arduino `Wire` interface, including:

* I²C commands
* Raw sensor data
* CRC validation
* Temperature conversion
* Humidity conversion

## Technologies

* C++ / Arduino
* ESP32
* I²C
* WebSockets
* HTTP
* Wi-Fi
* SinricPro
* Stepper motor control
* Embedded systems

## Setup

Install the required Arduino libraries:

* SinricPro
* WebSockets by Markus Sattler

Update the configuration values in the source code:

```cpp
#define WIFI_SSID       "YOUR_WIFI_SSID"
#define WIFI_PASS       "YOUR_WIFI_PASSWORD"

#define APP_KEY         "YOUR_SINRICPRO_APP_KEY"
#define APP_SECRET      "YOUR_SINRICPRO_APP_SECRET"

#define TEMP_SENSOR_ID  "YOUR_TEMP_SENSOR_DEVICE_ID"
#define BLIND_ID        "YOUR_BLINDS_DEVICE_ID"
```

Upload the code to the ESP32 and open the Serial Monitor at `115200` baud.

The ESP32 will print the local IP address used to access the smart blinds control page.

## Future Improvements

* Add physical limit switches for automatic calibration
* Store blind position in nonvolatile memory
* Add automatic opening based on temperature or time
* Add light-level sensing
* Add automatic startup homing
* Improve enclosure and mechanical design
