#include <ir_Panasonic.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <BlueDot_BME280.h>
#include <ArduinoOTA.h>

#define WIFI_SSID "yusin.me"
#define WIFI_PASSWORD "qwertyuiopasdfghjkl"

#define OTA_PASSWORD "Goni_Linx1"

#define MQTT_SERVER "nas"
#define MQTT_USER ""
#define MQTT_PASSWORD ""

#define ROOM_NAME "bedroom"
#define AVAILABILITY_TOPIC "home/" ROOM_NAME "/ac/available"
#define PAYLOAD_AVAILABLE "online"
#define PAYLOAD_NOT_AVAILABLE "offline"

#define POWER_COMMAND_TOPIC "home/" ROOM_NAME "/ac/power/set"
#define MODE_COMMAND_TOPIC "home/" ROOM_NAME "/ac/mode/set"
#define LOW_TEMPERATURE_COMMAND_TOPIC "home/" ROOM_NAME "/ac/low_temperature/set"
#define HIGH_TEMPERATURE_COMMAND_TOPIC "home/" ROOM_NAME "/ac/high_temperature/set"
#define FAN_COMMAND_TOPIC "home/" ROOM_NAME "/ac/fan/set"
#define SWING_COMMAND_TOPIC "home/" ROOM_NAME "/ac/swing/set"

#define TEMPERATURE_TOPIC "home/" ROOM_NAME "/temperature"
#define HUMIDITY_TOPIC "home/" ROOM_NAME "/humidity"
#define PRESSURE_TOPIC "home/" ROOM_NAME "/pressure"

#define HOST_NAME ROOM_NAME "_ac"

#define baro_corr_hpa 0.0 // pressure correction

#define IR_LED D0 // ESP8266 GPIO pin to use. Recommended: 4 (D2).

#define kPanasonicAcSmart 10
#define kPanasonicAcOff 11

#define TEMP_STATE_OK 0
#define TEMP_STATE_LOW 1
#define TEMP_STATE_HIGH 2
#define TEMP_STATE_UNKNOWN 3
#define TEMP_STATE_OFF 4

IRPanasonicAc irsend(IR_LED); // Set the GPIO to be used to sending the message.
WiFiClient espClient;
PubSubClient client(espClient);
BlueDot_BME280 bme; // I2C
long lastMsg = 0;
long ac_last_update_time = 0;
float ac_low_temperature = 20.0;
float ac_high_temperature = 26.0;
float current_temperature = 24.0;
uint8_t ac_vertical_swing = kPanasonicAcSwingVAuto;
uint8_t ac_fan = kPanasonicAcFanAuto;
uint8_t ac_mode = kPanasonicAcOff;
boolean ac_update = true;
uint8_t temperature_state = TEMP_STATE_UNKNOWN; // 0 - is ok, -1  - too low, 1 - too high

//char msg[50];

void setup()
{
    Serial.begin(115200, SERIAL_8N1, SERIAL_TX_ONLY);
    setup_wifi();
    setup_OTA();
    client.setServer(MQTT_SERVER, 1883);
    client.setCallback(callback);
    //irsend.setModel(kPanasonicCkp);
    irsend.begin();

    bme.parameter.communication = 0; //I2C communication for Sensor
    bme.parameter.I2CAddress = 0x76; //I2C Address for Sensor
    bme.parameter.sensorMode = 0b11; //Setup Sensor mode for Sensor 1
    bme.parameter.IIRfilter = 0b100; //IIR Filter for Sensor 1
    bme.parameter.humidOversampling = 0b101; //Humidity Oversampling for Sensor 1
    bme.parameter.tempOversampling = 0b101; //Temperature Oversampling for Sensor 1
    bme.parameter.pressOversampling = 0b101; //Pressure Oversampling for Sensor 1
    bme.parameter.pressureSeaLevel = 1013.25; //default value of 1013.25 hPa (Sensor 2)
    bme.parameter.tempOutsideCelsius = 15; //default value of 15°C
    bme.parameter.tempOutsideFahrenheit = 59; //default value of 59°F

    //start sensor
    if (bme.init() != 0x60) {
        Serial.println(F("Ops! BME280 Sensor not found!"));
        bme.parameter.I2CAddress = 0x76;
        if (bme.init() != 0x60) {
            Serial.println(F("Ops! BME280 Sensor not found!"));
            while (1)
                ;
        }
    }
}

void loop()
{
    if (!client.connected()) {
        reconnect();
    }
    client.loop();

    long now = millis();
    if (now - lastMsg > 5000) {
        lastMsg = now;
        bmeMeasurements();
        checkTemperature();
        client.publish(AVAILABILITY_TOPIC, PAYLOAD_AVAILABLE);
    }

    if (ac_update) {
        changeACState();
        ac_update = false;
    }

    ArduinoOTA.handle();
}

void bmeMeasurements()
{

    current_temperature = bme.readTempC();
    float hum = bme.readHumidity();
    float baro = bme.readPressure();

    Serial.print("New temperature:");
    Serial.print(String(current_temperature) + " degC   ");
    client.publish(TEMPERATURE_TOPIC, String(current_temperature).c_str(), true);

    Serial.print("New humidity:");
    Serial.println(String(hum) + " %");
    client.publish(HUMIDITY_TOPIC, String(hum).c_str(), true);

    float baro_hpa = baro + baro_corr_hpa; // hPa corrected to sea level
    float baro_mmhg = (baro + baro_corr_hpa) * 0.75006375541921F; // mmHg corrected to sea level
    Serial.print("New barometer:");
    Serial.print(String(baro_hpa) + " hPa   ");
    Serial.println(String(baro_mmhg) + " mmHg");
    client.publish(PRESSURE_TOPIC, String(baro_mmhg).c_str(), true);
}

void setup_wifi()
{
    delay(10);
    // We start by connecting to a WiFi network
    Serial.println();
    Serial.print("Connecting to ");
    Serial.println(WIFI_SSID);

    WiFi.hostname(HOST_NAME);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    while (WiFi.status() != WL_CONNECTED) {
        delay(480);
        Serial.print(".");
    }

    Serial.println("");
    Serial.println("WiFi connected");
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP());
}

void setup_OTA()
{
    // Hostname defaults to esp8266-[ChipID]
    ArduinoOTA.setHostname(HOST_NAME);

    // No authentication by default
    ArduinoOTA.setPassword(OTA_PASSWORD);

    // Password can be set with it's md5 value as well
    // MD5(admin) = 21232f297a57a5a743894a0e4a801fc3
    // ArduinoOTA.setPasswordHash("21232f297a57a5a743894a0e4a801fc3");

    ArduinoOTA.onStart([]() {
        String type;
        if (ArduinoOTA.getCommand() == U_FLASH) {
            type = "sketch";
        }
        else { // U_SPIFFS
            type = "filesystem";
        }

        // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
        Serial.println("Start updating " + type);
    });
    ArduinoOTA.onEnd([]() {
        Serial.println("\nEnd");
    });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
    });
    ArduinoOTA.onError([](ota_error_t error) {
        Serial.printf("Error[%u]: ", error);
        if (error == OTA_AUTH_ERROR) {
            Serial.println("Auth Failed");
        }
        else if (error == OTA_BEGIN_ERROR) {
            Serial.println("Begin Failed");
        }
        else if (error == OTA_CONNECT_ERROR) {
            Serial.println("Connect Failed");
        }
        else if (error == OTA_RECEIVE_ERROR) {
            Serial.println("Receive Failed");
        }
        else if (error == OTA_END_ERROR) {
            Serial.println("End Failed");
        }
    });
    ArduinoOTA.begin();
}

void reconnect()
{
    // Loop until we're reconnected
    while (!client.connected()) {
        Serial.print("Attempting MQTT connection...");
        // Create a random client ID
        String clientId = "ESP8266Client-";
        clientId += String(random(0xffff), HEX);
        // Attempt to connect
        if (client.connect(clientId.c_str(), AVAILABILITY_TOPIC, 1, true, PAYLOAD_NOT_AVAILABLE)) {
            Serial.println("connected");
            // Once connected, publish an announcement...
            client.publish(AVAILABILITY_TOPIC, PAYLOAD_AVAILABLE);
            // ... and resubscribe
            //client.subscribe(POWER_COMMAND_TOPIC);
            client.subscribe(MODE_COMMAND_TOPIC);
            client.subscribe(LOW_TEMPERATURE_COMMAND_TOPIC);
            client.subscribe(HIGH_TEMPERATURE_COMMAND_TOPIC);
            client.subscribe(FAN_COMMAND_TOPIC);
            client.subscribe(SWING_COMMAND_TOPIC);
        }
        else {
            Serial.print("failed, rc=");
            Serial.print(client.state());
            Serial.println(" try again in 5 seconds");
            // Wait 5 seconds before retrying
            delay(5000);
        }
    }
}

void callback(char* topic, byte* payload, unsigned int length)
{
    Serial.print("Message arrived [");
    Serial.print(topic);
    Serial.print("] ");
    for (int i = 0; i < length; i++) {
        Serial.print((char)payload[i]);
    }
    Serial.println();

    if (strcmp(topic, MODE_COMMAND_TOPIC) == 0) {
        if ((char)payload[0] == 'a' /* auto */) {
            ac_mode = kPanasonicAcSmart;
            temperature_state = TEMP_STATE_UNKNOWN;
        }
        else if ((char)payload[0] == 'h' /* heat */) {
            ac_mode = kPanasonicAcHeat;
        }
        else if ((char)payload[0] == 'c' /* cool */) {
            ac_mode = kPanasonicAcCool;
        }
        else if ((char)payload[0] == 'd' /* dry */) {
            ac_mode = kPanasonicAcDry;
        }
        else if ((char)payload[0] == 's' /* smart */) {
            ac_mode = kPanasonicAcSmart;
        }
        else /* off */ {
            ac_mode = kPanasonicAcOff;
        }
    }

    if (strcmp(topic, FAN_COMMAND_TOPIC) == 0) {
        if ((char)payload[0] == 'a' /* auto */) {
            ac_fan = kPanasonicAcFanAuto;
        }
        else if ((char)payload[0] == 'l' /* low */) {
            ac_fan = kPanasonicAcFanMin;
        }
        else if ((char)payload[0] == 'm' /* medium */) {
            ac_fan = 2;
        }
        else if ((char)payload[0] == 'h' /* high */) {
            ac_fan = kPanasonicAcFanMax;
        }
    }

    if (strcmp(topic, SWING_COMMAND_TOPIC) == 0) {
        if ((char)payload[0] == 'a' /* auto */) {
            ac_vertical_swing = kPanasonicAcSwingVAuto;
        }
        else if ((char)payload[0] == 'u' /* up */) {
            ac_vertical_swing = kPanasonicAcSwingVUp;
        }
        else if ((char)payload[0] == 'm' /* middle */) {
            ac_vertical_swing = 0x3;
        }
        else if ((char)payload[0] == 'd' /* down */) {
            ac_vertical_swing = kPanasonicAcSwingVDown;
        }
    }

    if (strcmp(topic, LOW_TEMPERATURE_COMMAND_TOPIC) == 0) {
        payload[length] = '\0';
        String s = String((char*)payload);
        ac_low_temperature = s.toFloat();
    }

    if (strcmp(topic, HIGH_TEMPERATURE_COMMAND_TOPIC) == 0) {
        payload[length] = '\0';
        String s = String((char*)payload);
        ac_high_temperature = s.toFloat();
    }

    ac_update = true;
}

void checkTemperature()
{
    boolean force_update = (millis() - ac_last_update_time) > 30 * 60 * 1000; //30 mins

    if (current_temperature > ac_high_temperature) {
        if (ac_mode == kPanasonicAcSmart && (temperature_state != TEMP_STATE_HIGH || force_update)) {
            ac_update = true;
        }
        temperature_state = TEMP_STATE_HIGH;
    }
    else if (current_temperature < ac_low_temperature) {
        if (ac_mode == kPanasonicAcSmart && (temperature_state != TEMP_STATE_LOW || force_update)) {
            ac_update = true;
        }
        temperature_state = TEMP_STATE_LOW;
    }
    else if ((current_temperature > ac_low_temperature) && (current_temperature < ac_high_temperature)) {
        if (ac_mode == kPanasonicAcSmart) {
            if (temperature_state == TEMP_STATE_OK && force_update) {
                temperature_state = TEMP_STATE_OFF;
                ac_update = true;
            }
            else if (temperature_state != TEMP_STATE_OFF && temperature_state != TEMP_STATE_OK) {
                temperature_state = TEMP_STATE_OK;
                ac_last_update_time = millis();
            }
        }
    }
    //client.publish("debug", "temp_state", true);
    //client.publish("debug", String(temperature_state).c_str(), true);
}

void changeACState()
{

    //client.publish("debug", "change", true);
    if (ac_mode == kPanasonicAcOff) {
        irsend.off();
    }
    else if (ac_mode == kPanasonicAcSmart) {
        if (temperature_state == TEMP_STATE_HIGH) {
            irsend.setMode(kPanasonicAcCool);
            irsend.setTemp((int)((ac_high_temperature + ac_low_temperature) / 2.0));
            irsend.on();
        }
        else if (temperature_state == TEMP_STATE_LOW) {
            irsend.setMode(kPanasonicAcHeat);
            irsend.setTemp((int)((ac_high_temperature + ac_low_temperature) / 2.0));
            irsend.on();
        }
        else if (temperature_state == TEMP_STATE_OFF) {
            irsend.off();
        }
    }
    else {
        irsend.on();
        irsend.setMode(ac_mode);
        irsend.setTemp((int)((ac_high_temperature + ac_low_temperature) / 2.0));
    }

    irsend.setFan(ac_fan);
    irsend.setSwingVertical(ac_vertical_swing);
    irsend.send();

    ac_last_update_time = millis();
}
