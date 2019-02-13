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

#define ROOM_NAME "livingroom"
#define AVAILABILITY_TOPIC "home/" ROOM_NAME "/ac/available"
#define PAYLOAD_AVAILABLE "online"
#define PAYLOAD_NOT_AVAILABLE "offline"

#define POWER_COMMAND_TOPIC "home/" ROOM_NAME "/ac/power/set"
#define MODE_COMMAND_TOPIC "home/" ROOM_NAME "/ac/mode/set"
#define TEMPERATURE_COMMAND_TOPIC "home/" ROOM_NAME "/ac/temperature/set"
#define FAN_COMMAND_TOPIC "home/" ROOM_NAME "/ac/fan/set"
#define SWING_COMMAND_TOPIC "home/" ROOM_NAME "/ac/swing/set"

#define TEMPERATURE_TOPIC "home/" ROOM_NAME "/temperature"
#define HUMIDITY_TOPIC "home/" ROOM_NAME "/humidity"
#define PRESSURE_TOPIC "home/" ROOM_NAME "/pressure"

#define HOST_NAME ROOM_NAME "_ac"

#define baro_corr_hpa 0.0 // pressure correction

#define IR_LED D0  // ESP8266 GPIO pin to use. Recommended: 4 (D2).

IRPanasonicAc irsend(IR_LED);  // Set the GPIO to be used to sending the message.
WiFiClient espClient;
PubSubClient client(espClient);
BlueDot_BME280 bme; // I2C
long lastMsg = 0;

//char msg[50];


void setup() {
  Serial.begin(115200, SERIAL_8N1, SERIAL_TX_ONLY);
  setup_wifi();
  setup_OTA();
  client.setServer(MQTT_SERVER, 1883);
  client.setCallback(callback);
  //irsend.setModel(kPanasonicCkp);
  irsend.begin();

  bme.parameter.communication = 0;                    //I2C communication for Sensor
  bme.parameter.I2CAddress = 0x76;                    //I2C Address for Sensor

  //Now choose on which mode your device will run
  //On doubt, just leave on normal mode, that's the default value

  //0b00:     In sleep mode no measurements are performed, but power consumption is at a minimum
  //0b01:     In forced mode a single measured is performed and the device returns automatically to sleep mode
  //0b11:     In normal mode the sensor measures continually (default value)
  bme.parameter.sensorMode = 0b11;                    //Setup Sensor mode for Sensor 1

  //Great! Now set up the internal IIR Filter
  //The IIR (Infinite Impulse Response) filter suppresses high frequency fluctuations
  //In short, a high factor value means less noise, but measurements are also less responsive
  //You can play with these values and check the results!
  //In doubt just leave on default
  
  //0b000:      factor 0 (filter off)
  //0b001:      factor 2
  //0b010:      factor 4
  //0b011:      factor 8
  //0b100:      factor 16 (default value)
  bme.parameter.IIRfilter = 0b100;                   //IIR Filter for Sensor 1

  //Next you'll define the oversampling factor for the humidity measurements
  //Again, higher values mean less noise, but slower responses
  //If you don't want to measure humidity, set the oversampling to zero

  //0b000:      factor 0 (Disable humidity measurement)
  //0b001:      factor 1
  //0b010:      factor 2
  //0b011:      factor 4
  //0b100:      factor 8
  //0b101:      factor 16 (default value)
  bme.parameter.humidOversampling = 0b101;            //Humidity Oversampling for Sensor 1

  //Now define the oversampling factor for the temperature measurements
  //You know now, higher values lead to less noise but slower measurements
  
  //0b000:      factor 0 (Disable temperature measurement)
  //0b001:      factor 1
  //0b010:      factor 2
  //0b011:      factor 4
  //0b100:      factor 8
  //0b101:      factor 16 (default value)
  bme.parameter.tempOversampling = 0b101;              //Temperature Oversampling for Sensor 1

  //Finally, define the oversampling factor for the pressure measurements
  //For altitude measurements a higher factor provides more stable values
  //On doubt, just leave it on default
  
  //0b000:      factor 0 (Disable pressure measurement)
  //0b001:      factor 1
  //0b010:      factor 2
  //0b011:      factor 4
  //0b100:      factor 8
  //0b101:      factor 16 (default value)  
  bme.parameter.pressOversampling = 0b101;             //Pressure Oversampling for Sensor 1

  
  //For precise altitude measurements please put in the current pressure corrected for the sea level
  //On doubt, just leave the standard pressure as default (1013.25 hPa);
  bme.parameter.pressureSeaLevel = 1013.25;            //default value of 1013.25 hPa (Sensor 2)

  //Also put in the current average temperature outside (yes, really outside!)
  //For slightly less precise altitude measurements, just leave the standard temperature as default (15°C and 59°F);
  bme.parameter.tempOutsideCelsius = 15;               //default value of 15°C
  bme.parameter.tempOutsideFahrenheit = 59;            //default value of 59°F

  //start sensor
  if (bme.init() != 0x60)
  {    
    Serial.println(F("Ops! BME280 Sensor not found!"));
    bme.parameter.I2CAddress = 0x76;
    if (bme.init() != 0x60)
    {    
      Serial.println(F("Ops! BME280 Sensor not found!"));
      while(1);
    }
  }
}

void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  long now = millis();
  if (now - lastMsg > 5000) {
    lastMsg = now;
    bmeMeasurements();
    client.publish(AVAILABILITY_TOPIC, PAYLOAD_AVAILABLE);
  }

  ArduinoOTA.handle();
}

void bmeMeasurements(){
  
  float temp_c = bme.readTempC();
  float hum = bme.readHumidity();
  float baro = bme.readPressure();

  Serial.print("New temperature:");
  Serial.print(String(temp_c) + " degC   ");
  client.publish(TEMPERATURE_TOPIC, String(temp_c).c_str(), true);

  Serial.print("New humidity:");
  Serial.println(String(hum) + " %");
  client.publish(HUMIDITY_TOPIC, String(hum).c_str(), true);

  float baro_hpa=baro+baro_corr_hpa; // hPa corrected to sea level
  float baro_mmhg=(baro+baro_corr_hpa)*0.75006375541921F; // mmHg corrected to sea level
  Serial.print("New barometer:");
  Serial.print(String(baro_hpa) + " hPa   ");
  Serial.println(String(baro_mmhg) + " mmHg");
  client.publish(PRESSURE_TOPIC, String(baro_mmhg).c_str(), true);
  
}

void setup_wifi() {
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

void setup_OTA() {
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
    } else { // U_SPIFFS
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
    } else if (error == OTA_BEGIN_ERROR) {
      Serial.println("Begin Failed");
    } else if (error == OTA_CONNECT_ERROR) {
      Serial.println("Connect Failed");
    } else if (error == OTA_RECEIVE_ERROR) {
      Serial.println("Receive Failed");
    } else if (error == OTA_END_ERROR) {
      Serial.println("End Failed");
    }
  });
  ArduinoOTA.begin();
  
}

void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Create a random client ID
    String clientId = "ESP8266Client-";
    clientId += String(random(0xffff), HEX);
    // Attempt to connect
    if (client.connect(clientId.c_str(),AVAILABILITY_TOPIC,1,true,PAYLOAD_NOT_AVAILABLE)) {
      Serial.println("connected");
      // Once connected, publish an announcement...
      client.publish(AVAILABILITY_TOPIC, PAYLOAD_AVAILABLE);
      // ... and resubscribe
      //client.subscribe(POWER_COMMAND_TOPIC);
      client.subscribe(MODE_COMMAND_TOPIC);
      client.subscribe(TEMPERATURE_COMMAND_TOPIC);
      client.subscribe(FAN_COMMAND_TOPIC);
      client.subscribe(SWING_COMMAND_TOPIC);
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();

//  if (strcmp(topic,POWER_COMMAND_TOPIC)==0){
//    if ((char)payload[0] == '1') {
//      irsend.on();
//    } else {
//      irsend.off();
//    }
//  }
  
  if (strcmp(topic,MODE_COMMAND_TOPIC)==0){
    if ((char)payload[0] == 'a' /* auto */) {
      irsend.setMode(kPanasonicAcAuto);
      irsend.on();
    } else if ((char)payload[0] == 'h' /* heat */) {
      irsend.setMode(kPanasonicAcHeat);
      irsend.on();
    } else if ((char)payload[0] == 'c' /* cool */) {
      irsend.setMode(kPanasonicAcCool);
      irsend.on();
    } else if ((char)payload[0] == 'd' /* dry */) {
      irsend.setMode(kPanasonicAcDry);
      irsend.on();
    } else /* off */ {
      irsend.off(); 
    }
  }

  if (strcmp(topic,FAN_COMMAND_TOPIC)==0){
    if ((char)payload[0] == 'a' /* auto */) {
      irsend.setFan(kPanasonicAcFanAuto);
    } else if ((char)payload[0] == 'l' /* low */) {
      irsend.setFan(kPanasonicAcFanMin);
    } else if ((char)payload[0] == 'm' /* medium */) {
      irsend.setFan(2);
    } else if ((char)payload[0] == 'h' /* high */) {
      irsend.setFan(kPanasonicAcFanMax);
    }
  }

  if (strcmp(topic,SWING_COMMAND_TOPIC)==0){
    if ((char)payload[0] == 'a' /* auto */) {
      irsend.setSwingVertical(kPanasonicAcSwingVAuto);
    } else if ((char)payload[0] == 'u' /* up */) {
      irsend.setSwingVertical(kPanasonicAcSwingVUp);
    } else if ((char)payload[0] == 'm' /* middle */) {
      irsend.setSwingVertical(0x3);
    } else if ((char)payload[0] == 'd' /* down */) {
      irsend.setSwingVertical(kPanasonicAcSwingVDown);
    }
  }
  
  if (strcmp(topic,TEMPERATURE_COMMAND_TOPIC)==0){
    payload[length] = '\0';
    String s = String((char*)payload);
    float f = s.toFloat();
    irsend.setTemp((int)f);
    Serial.print("temp to send:");
    Serial.println(int(f));    
  }
  
  irsend.send();

}
