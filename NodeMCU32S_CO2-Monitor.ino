/* Created by Thiemo Vater
 * 
 * Board: ESP32 Dev Module
 * Upload Speed: 115200
 * CPU Frequenz: 240MHz (Wifi/BT)
 * Flash Frequenz: 80Mhz
 * Flash Mode: QIO
 * Flash Size: 4MB(32mb)
 * Partition Chema: Default 4MB with spiffs
 *
 * CCS811 Sensor
 * WS2801 LED stripe (2 LED)
 */

#include <WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>    // I2C library
#include <ccs811.h>
#include <FastLED.h>


// Wifi network:
const char* ssid = "SSID";
const char* password = "WIFIPASSWORD";

// MQTT data:
const char* mqtt_server = "mqttbrokerurl";
const char* mqtt_user = "mqttuser";
const char* mqtt_pw = "mqttpassword";
String clientId = "NodeMCU32S-";  // I use the micro controller type
#define MSG_BUFFER_SIZE  (50)
char co2[MSG_BUFFER_SIZE];
char voc[MSG_BUFFER_SIZE];

// Wiring for ESP8266 NodeMCU boards: VDD to 3V3, GND to GND, SDA to D2, SCL to D1, nWAKE to D3 (or GND)
CCS811 ccs811(17); // nWAKE on D3

//WS2801 stripe:
#define NUM_LEDS 2
#define DATA_PIN 16  //GPIO16
#define CLOCK_PIN 17  //GPIO17
CRGB leds[NUM_LEDS]; // Define the array of leds
int r = 255;
int g = 255;
int b = 255;

WiFiClient espClient;
PubSubClient client(espClient);
long lastMsg = 0;
float eppm = 0;
float evot = 0;

// We start by connecting to a WiFi network
void setup_wifi() {
  delay(10);
  Serial.println();
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
     delay(500);
     Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

// reconnect for MQTT
void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Create a random client ID
    clientId += String(random(0xffff), HEX);
    // Attempt to connect
    if (client.connect(clientId.c_str(), mqtt_user, mqtt_pw)) {
      Serial.println("connected");
      // Once connected, publish an announcement...
      client.publish("DHT11", "Startup");
      // ... and resubscribe
      client.subscribe("inTopic");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}


void setup() {
  delay(3000); // wait 3 seconds

  //setup for serial, wifi, mqtt and LED
  Serial.begin(115200);
  setup_wifi(); 
  client.setServer(mqtt_server, 1883);
  FastLED.addLeds<WS2801, DATA_PIN, CLOCK_PIN, RGB>(leds, NUM_LEDS);
  
  // Enable I2C
  Wire.begin(); 
  
  // Enable CCS811
  ccs811.set_i2cdelay(50); // Needed for ESP8266 because it doesn't handle I2C clock stretch correctly
  bool ok= ccs811.begin();
  if( !ok ) Serial.println("setup: CCS811 begin FAILED");

  // Print CCS811 versions
  Serial.print("setup: hardware    version: "); Serial.println(ccs811.hardware_version(),HEX);
  Serial.print("setup: bootloader  version: "); Serial.println(ccs811.bootloader_version(),HEX);
  Serial.print("setup: application version: "); Serial.println(ccs811.application_version(),HEX);
  
  // Start measuring
  ok= ccs811.start(CCS811_MODE_1SEC);
  if( !ok ) Serial.println("setup: CCS811 start FAILED");

}

void loop() {

  if (!client.connected()) {
    reconnect();
  }
  
  // Read
  uint16_t eco2, etvoc, errstat, raw;
  ccs811.read(&eco2,&etvoc,&errstat,&raw); 
  
  // work with results
  if( errstat==CCS811_ERRSTAT_OK ) { 
    Serial.print("CCS811: ");
    Serial.print("eco2=");  Serial.print(eco2);     Serial.print(" ppm  ");
    Serial.print("etvoc="); Serial.print(etvoc);    Serial.print(" ppb  ");
    //Serial.print("raw6=");  Serial.print(raw/1024); Serial.print(" uA  "); 
    //Serial.print("raw10="); Serial.print(raw%1024); Serial.print(" ADC  ");
    //Serial.print("R="); Serial.print((1650*1000L/1023)*(raw%1024)/(raw/1024)); Serial.print(" ohm");

    //identify color. 13 diffenrent colors are predifined
    if(eco2 >= 1600) {r=255;g=0;b=0;};
    if(eco2 < 1600) {r=234;g=22;b=0;};
    if(eco2 < 1500) { r=213;g=43;b=0;};
    if(eco2 < 1400) { r=192;g=64;b=0;};
    if(eco2 < 1300) { r=170;g=85;b=0;};
    if(eco2 < 1200) { r=149;g=107;b=0;};
    if(eco2 < 1100) { r=128;g=128;b=0;};
    if(eco2 < 1000) { r=107;g=149;b=0;};
    if(eco2 < 900) { r=85;g=170;b=0;};
    if(eco2 < 800) { r=64;g=192;b=0;};
    if(eco2 < 700) { r=43;g=213;b=0;};
    if(eco2 < 600) { r=22;g=234;b=0;};
    if(eco2 < 500) { r=0;g=255;b=0;};


    //Serial.print("Converted into color = ");   Serial.print(r);   Serial.print(";");   Serial.print(g);   Serial.print(";");   Serial.print(b);   Serial.print("  "); 
    Serial.println();

    // set color for all LED   
    for(int rgbLed = 0; rgbLed < NUM_LEDS; rgbLed = rgbLed + 1) {
       leds[rgbLed] = CRGB(r,g,b);
       FastLED.show();
    }

    // Publish to MQTT Server    
    // Convert float to string at first:
    gcvt(eco2,MSG_BUFFER_SIZE,co2);
    gcvt(etvoc,MSG_BUFFER_SIZE,voc);
    client.publish("CCS811/CO2", co2);
    client.publish("CCS811/TOV", voc);
  
  } else if( errstat==CCS811_ERRSTAT_OK_NODATA ) {
    Serial.println("CCS811: waiting for (new) data");
  } else if( errstat & CCS811_ERRSTAT_I2CFAIL ) { 
    Serial.println("CCS811: I2C error");
  } else {
    Serial.print("CCS811: errstat="); Serial.print(errstat,HEX); 
    Serial.print("="); Serial.println( ccs811.errstat_str(errstat) ); 
  }


  // Wait
  delay(1000); 

}

