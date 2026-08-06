#include <Arduino.h>
#include <WiFi.h>
#include <Wire.h>
#include <WebServer.h>
#include <WebSocketsServer.h>

#include "SinricPro.h"
#include "SinricProTemperaturesensor.h"
#include "SinricProBlinds.h"

#define WIFI_SSID       "YOUR_WIFI_SSID"
#define WIFI_PASS       "YOUR_WIFI_PASSWORD"

#define APP_KEY         "YOUR_SINRICPRO_APP_KEY"
#define APP_SECRET      "YOUR_SINRICPRO_APP_SECRET"

#define TEMP_SENSOR_ID  "YOUR_TEMP_SENSOR_DEVICE_ID"
#define BLIND_ID        "YOUR_BLINDS_DEVICE_ID"

#define BAUD_RATE       115200

#define SDA_PIN         21
#define SCL_PIN         22
#define SHT31_ADDRESS   0x44

#define EVENT_WAIT_TIME 20000

float temperatureF = 0.0;
float humidity = 0.0;

float lastTemperatureF = NAN;
float lastHumidity = NAN;

unsigned long lastSensorRead = 0;
unsigned long lastSinricEvent = 0;

const unsigned long SENSOR_READ_INTERVAL = 2000;

const int stepPin = 26;
const int directPin = 23;
const int enabPin = 25;

const int STEPPER_MAX = 1250;
const unsigned long STEPPER_HALF_PERIOD_US = 3000;

long currentStep = 0;
long targetStep = 0;

int blindsPosition = 0;
bool blindsPower = true;

bool stepPulseHigh = false;
unsigned long lastStepPulseTime = 0;

WebServer webServer(80);
WebSocketsServer webSocket(81);

const char MAIN_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>ESP32 Smart Blinds</title>

    <style>
        body {
            font-family: Arial, sans-serif;
            background: #111;
            color: #eee;
            text-align: center;
            margin: 0;
            padding: 25px;
        }

        .card {
            background: #222;
            max-width: 500px;
            margin: auto;
            padding: 25px;
            border-radius: 16px;
        }

        h1 {
            margin-top: 0;
        }

        button {
            font-size: 18px;
            padding: 12px 20px;
            margin: 6px;
            border: none;
            border-radius: 8px;
            cursor: pointer;
        }

        .open {
            background: #28a745;
            color: white;
        }

        .close {
            background: #dc3545;
            color: white;
        }

        .stop {
            background: #777;
            color: white;
        }

        .power {
            background: #007bff;
            color: white;
        }

        input[type=range] {
            width: 90%;
        }

        .value {
            font-size: 28px;
            font-weight: bold;
        }

        .sensor {
            background: #333;
            margin-top: 20px;
            padding: 15px;
            border-radius: 10px;
        }

        #status {
            margin-top: 15px;
            font-size: 14px;
            color: #aaa;
        }
    </style>
</head>

<body>

<div class="card">

    <h1>Smart Blinds</h1>

    <div class="value">
        <span id="position">0</span>%
    </div>

    <input
        id="slider"
        type="range"
        min="0"
        max="100"
        value="0"
    >

    <br><br>

    <button class="open" onclick="sendCommand('open')">
        Open
    </button>

    <button class="close" onclick="sendCommand('close')">
        Close
    </button>

    <button class="stop" onclick="sendCommand('stop')">
        Stop
    </button>

    <br>

    <button class="power" onclick="sendCommand('power:on')">
        Enable
    </button>

    <button class="power" onclick="sendCommand('power:off')">
        Disable
    </button>

    <div class="sensor">

        <h3>Room Conditions</h3>

        Temperature:
        <strong>
            <span id="temperature">--</span> °F
        </strong>

        <br><br>

        Humidity:
        <strong>
            <span id="humidity">--</span> %
        </strong>

    </div>

    <div id="status">
        Connecting...
    </div>

</div>

<script>

let socket;

const slider = document.getElementById("slider");
const positionText = document.getElementById("position");
const statusText = document.getElementById("status");

function connectWebSocket() {

    socket = new WebSocket(
        "ws://" + window.location.hostname + ":81/"
    );

    socket.onopen = function() {
        statusText.innerHTML = "WebSocket connected";
        socket.send("getStatus");
    };

    socket.onclose = function() {
        statusText.innerHTML = "Disconnected - reconnecting...";
        setTimeout(connectWebSocket, 2000);
    };

    socket.onerror = function() {
        statusText.innerHTML = "WebSocket error";
    };

    socket.onmessage = function(event) {

        const message = event.data;

        if (message.startsWith("position:")) {

            const value = parseInt(message.substring(9));

            slider.value = value;
            positionText.innerHTML = value;
        }

        else if (message.startsWith("temperature:")) {

            document.getElementById("temperature").innerHTML =
                message.substring(12);
        }

        else if (message.startsWith("humidity:")) {

            document.getElementById("humidity").innerHTML =
                message.substring(9);
        }

        else if (message.startsWith("power:")) {

            statusText.innerHTML =
                "Blinds " + message.substring(6);
        }
    };
}

slider.oninput = function() {
    positionText.innerHTML = this.value;
};

slider.onchange = function() {

    if (socket.readyState === WebSocket.OPEN) {
        socket.send("position:" + this.value);
    }
};

function sendCommand(command) {

    if (socket.readyState === WebSocket.OPEN) {
        socket.send(command);
    }
}

connectWebSocket();

</script>

</body>
</html>
)rawliteral";

uint8_t sht31CRC(const uint8_t *data, int len) {

    uint8_t crc = 0xFF;

    for (int i = 0; i < len; i++) {

        crc ^= data[i];

        for (int bit = 0; bit < 8; bit++) {

            if (crc & 0x80) {
                crc = (crc << 1) ^ 0x31;
            } else {
                crc <<= 1;
            }
        }
    }

    return crc;
}

bool readSHT31(float &tempF, float &hum) {

    Wire.beginTransmission(SHT31_ADDRESS);

    Wire.write(0x24);
    Wire.write(0x00);

    if (Wire.endTransmission() != 0) {
        Serial.println("SHT31 command failed");
        return false;
    }

    delay(20);

    int bytes = Wire.requestFrom(SHT31_ADDRESS, 6);

    if (bytes != 6) {
        Serial.println("SHT31 did not return 6 bytes");
        return false;
    }

    uint8_t data[6];

    for (int i = 0; i < 6; i++) {
        data[i] = Wire.read();
    }

    if (sht31CRC(data, 2) != data[2]) {
        Serial.println("SHT31 temperature CRC error");
        return false;
    }

    if (sht31CRC(&data[3], 2) != data[5]) {
        Serial.println("SHT31 humidity CRC error");
        return false;
    }

    uint16_t rawTemperature =
        ((uint16_t)data[0] << 8) | data[1];

    uint16_t rawHumidity =
        ((uint16_t)data[3] << 8) | data[4];

    float tempC =
        -45.0 +
        175.0 *
        ((float)rawTemperature / 65535.0);

    tempF =
        (tempC * 9.0 / 5.0) + 32.0;

    hum =
        100.0 *
        ((float)rawHumidity / 65535.0);

    hum = constrain(hum, 0.0, 100.0);

    return true;
}

void setBlindsPosition(int position) {

    position = constrain(position, 0, 100);

    blindsPosition = position;

    targetStep =
        round(
            ((float)position / 100.0)
            * STEPPER_MAX
        );

    Serial.printf(
        "Target position: %d%% | Target step: %ld\n",
        blindsPosition,
        targetStep
    );

    webSocket.broadcastTXT(
        "position:" + String(blindsPosition)
    );
}

void handleStepper() {

    if (!blindsPower) {

        digitalWrite(enabPin, HIGH);
        digitalWrite(stepPin, LOW);

        return;
    }

    if (currentStep == targetStep) {

        digitalWrite(stepPin, LOW);
        digitalWrite(enabPin, HIGH);

        stepPulseHigh = false;

        return;
    }

    unsigned long now = micros();

    if (
        now - lastStepPulseTime
        < STEPPER_HALF_PERIOD_US
    ) {
        return;
    }

    lastStepPulseTime = now;

    digitalWrite(enabPin, LOW);

    if (!stepPulseHigh) {

        if (targetStep > currentStep) {
            digitalWrite(directPin, HIGH);
        } else {
            digitalWrite(directPin, LOW);
        }

        digitalWrite(stepPin, HIGH);

        stepPulseHigh = true;
    }

    else {

        digitalWrite(stepPin, LOW);

        stepPulseHigh = false;

        if (targetStep > currentStep) {
            currentStep++;
        } else {
            currentStep--;
        }
    }
}

void stopBlinds() {

    targetStep = currentStep;

    blindsPosition =
        round(
            ((float)currentStep /
             STEPPER_MAX)
            * 100.0
        );

    blindsPosition =
        constrain(
            blindsPosition,
            0,
            100
        );

    Serial.printf(
        "Stopped at %d%%\n",
        blindsPosition
    );

    webSocket.broadcastTXT(
        "position:" +
        String(blindsPosition)
    );
}

bool onPowerState(
    const String &deviceId,
    bool &state
) {

    Serial.printf(
        "SinricPro power: %s\n",
        state ? "ON" : "OFF"
    );

    blindsPower = state;

    if (!blindsPower) {
        stopBlinds();
    }

    webSocket.broadcastTXT(
        String("power:") +
        (
            blindsPower
            ? "enabled"
            : "disabled"
        )
    );

    return true;
}

bool onRangeValue(
    const String &deviceId,
    int &position
) {

    if (!blindsPower) {

        Serial.println(
            "Ignoring position command: blinds disabled"
        );

        return true;
    }

    setBlindsPosition(position);

    Serial.printf(
        "SinricPro blinds position: %d%%\n",
        position
    );

    return true;
}

void handleTemperatureSensor() {

    unsigned long now = millis();

    if (
        now - lastSensorRead
        < SENSOR_READ_INTERVAL
    ) {
        return;
    }

    lastSensorRead = now;

    float newTemperature;
    float newHumidity;

    if (
        !readSHT31(
            newTemperature,
            newHumidity
        )
    ) {
        return;
    }

    temperatureF = newTemperature;
    humidity = newHumidity;

    Serial.printf(
        "Temperature: %.2f F | Humidity: %.2f%%\n",
        temperatureF,
        humidity
    );

    webSocket.broadcastTXT(
        "temperature:" +
        String(temperatureF, 1)
    );

    webSocket.broadcastTXT(
        "humidity:" +
        String(humidity, 1)
    );

    if (
        now - lastSinricEvent
        < EVENT_WAIT_TIME
    ) {
        return;
    }

    if (
        !isnan(lastTemperatureF) &&
        !isnan(lastHumidity)
    ) {

        if (
            abs(
                temperatureF -
                lastTemperatureF
            ) < 0.1
            &&
            abs(
                humidity -
                lastHumidity
            ) < 0.5
        ) {
            return;
        }
    }

    SinricProTemperaturesensor &sensor =
        SinricPro[TEMP_SENSOR_ID];

    bool success =
        sensor.sendTemperatureEvent(
            temperatureF,
            humidity
        );

    if (success) {

        Serial.println(
            "Sensor event sent to SinricPro"
        );

        lastTemperatureF = temperatureF;
        lastHumidity = humidity;
        lastSinricEvent = now;
    }
}

void handleWebSocketCommand(
    uint8_t clientNum,
    String message
) {

    Serial.printf(
        "[WebSocket] %s\n",
        message.c_str()
    );

    if (message == "getStatus") {

        webSocket.sendTXT(
            clientNum,
            "position:" +
            String(blindsPosition)
        );

        webSocket.sendTXT(
            clientNum,
            "temperature:" +
            String(temperatureF, 1)
        );

        webSocket.sendTXT(
            clientNum,
            "humidity:" +
            String(humidity, 1)
        );

        webSocket.sendTXT(
            clientNum,
            String("power:") +
            (
                blindsPower
                ? "enabled"
                : "disabled"
            )
        );

        return;
    }

    if (message == "open") {

        if (blindsPower) {
            setBlindsPosition(100);
        }

        return;
    }

    if (message == "close") {

        if (blindsPower) {
            setBlindsPosition(0);
        }

        return;
    }

    if (message == "stop") {

        stopBlinds();

        return;
    }

    if (message == "power:on") {

        blindsPower = true;

        webSocket.broadcastTXT(
            "power:enabled"
        );

        return;
    }

    if (message == "power:off") {

        stopBlinds();

        blindsPower = false;

        webSocket.broadcastTXT(
            "power:disabled"
        );

        return;
    }

    if (
        message.startsWith(
            "position:"
        )
    ) {

        int position =
            message
            .substring(9)
            .toInt();

        if (blindsPower) {
            setBlindsPosition(position);
        }

        return;
    }
}

void webSocketEvent(
    uint8_t clientNum,
    WStype_t type,
    uint8_t *payload,
    size_t length
) {

    switch (type) {

        case WStype_CONNECTED:
        {

            IPAddress ip =
                webSocket.remoteIP(
                    clientNum
                );

            Serial.printf(
                "[WebSocket] Client %u connected from %s\n",
                clientNum,
                ip.toString().c_str()
            );

            break;
        }

        case WStype_DISCONNECTED:
        {

            Serial.printf(
                "[WebSocket] Client %u disconnected\n",
                clientNum
            );

            break;
        }

        case WStype_TEXT:
        {

            String message =
                String(
                    (char *)payload
                );

            handleWebSocketCommand(
                clientNum,
                message
            );

            break;
        }

        default:
            break;
    }
}

void setupWiFi() {

    Serial.println();
    Serial.print("[WiFi] Connecting");

    WiFi.mode(WIFI_STA);

    WiFi.setSleep(false);
    WiFi.setAutoReconnect(true);

    WiFi.begin(
        WIFI_SSID,
        WIFI_PASS
    );

    unsigned long startTime =
        millis();

    while (
        WiFi.status()
        != WL_CONNECTED
    ) {

        Serial.print(".");
        delay(250);

        if (
            millis() - startTime
            > 15000
        ) {

            Serial.println();
            Serial.println("Wi-Fi failed.");
            Serial.println("Starting access point...");

            WiFi.disconnect(true);
            WiFi.mode(WIFI_AP);

            WiFi.softAP(
                "SmartBlinds",
                "12345678"
            );

            Serial.print("AP IP: ");
            Serial.println(WiFi.softAPIP());

            return;
        }
    }

    Serial.println();
    Serial.println("Wi-Fi connected!");

    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
}

void setupSinricPro() {

    SinricProTemperaturesensor &sensor =
        SinricPro[TEMP_SENSOR_ID];

    SinricProBlinds &blinds =
        SinricPro[BLIND_ID];

    blinds.onPowerState(onPowerState);
    blinds.onRangeValue(onRangeValue);

    SinricPro.onConnected(
        []() {
            Serial.println(
                "Connected to SinricPro"
            );
        }
    );

    SinricPro.onDisconnected(
        []() {
            Serial.println(
                "Disconnected from SinricPro"
            );
        }
    );

    SinricPro.begin(
        APP_KEY,
        APP_SECRET
    );
}

void setupWebServer() {

    webServer.on(
        "/",
        []() {

            webServer.send_P(
                200,
                "text/html",
                MAIN_PAGE
            );
        }
    );

    webServer.on(
        "/status",
        []() {

            String json = "{";

            json +=
                "\"position\":" +
                String(blindsPosition);

            json +=
                ",\"temperature\":" +
                String(temperatureF, 1);

            json +=
                ",\"humidity\":" +
                String(humidity, 1);

            json +=
                ",\"power\":" +
                String(
                    blindsPower
                    ? "true"
                    : "false"
                );

            json += "}";

            webServer.send(
                200,
                "application/json",
                json
            );
        }
    );

    webServer.begin();

    Serial.println(
        "HTTP server started on port 80"
    );

    webSocket.begin();
    webSocket.onEvent(webSocketEvent);

    Serial.println(
        "WebSocket server started on port 81"
    );
}

void setup() {

    Serial.begin(BAUD_RATE);

    delay(500);

    Serial.println();
    Serial.println(
        "Starting ESP32 Smart Blinds..."
    );

    pinMode(stepPin, OUTPUT);
    pinMode(directPin, OUTPUT);
    pinMode(enabPin, OUTPUT);

    digitalWrite(stepPin, LOW);
    digitalWrite(enabPin, HIGH);

    Wire.begin(
        SDA_PIN,
        SCL_PIN
    );

    Wire.setClock(100000);

    Serial.println(
        "I2C initialized"
    );

    float testTemp;
    float testHumidity;

    if (
        readSHT31(
            testTemp,
            testHumidity
        )
    ) {

        Serial.println(
            "SHT31 detected"
        );

        Serial.printf(
            "Initial reading: %.2f F, %.2f%% RH\n",
            testTemp,
            testHumidity
        );

        temperatureF = testTemp;
        humidity = testHumidity;
    }

    else {

        Serial.println(
            "SHT31 not detected"
        );
    }

    setupWiFi();
    setupWebServer();

    if (
        WiFi.status()
        == WL_CONNECTED
    ) {
        setupSinricPro();
    }

    Serial.println();
    Serial.println(
        "Smart blinds ready."
    );

    if (
        WiFi.status()
        == WL_CONNECTED
    ) {

        Serial.print(
            "Local control: http://"
        );

        Serial.println(
            WiFi.localIP()
        );
    }

    else {

        Serial.println(
            "Connect to Wi-Fi: SmartBlinds"
        );

        Serial.println(
            "Password: 12345678"
        );

        Serial.print(
            "Open: http://"
        );

        Serial.println(
            WiFi.softAPIP()
        );
    }
}

void loop() {

    if (
        WiFi.status()
        == WL_CONNECTED
    ) {
        SinricPro.handle();
    }

    webServer.handleClient();
    webSocket.loop();

    handleTemperatureSensor();
    handleStepper();
}
