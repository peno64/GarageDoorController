// Works on both the Arduino Nano and Mega

// See https://community.platformio.org/t/mqtt-with-arduino-nano-and-enc2860/34236/7
// See https://pubsubclient.knolleary.net/
// See http://www.steves-internet-guide.com/using-arduino-pubsub-mqtt-client/

// See also for future reference: https://www.instructables.com/DIY-Smart-Garage-Door-Opener-Using-ESP8266-Wemos-D/
//  This uses https://www.home-assistant.io/integrations/cover.mqtt/

#define SERIALBT

#if defined SERIALBT

#include "BluetoothSerial.h"

#if !defined(CONFIG_BT_ENABLED) || !defined(CONFIG_BLUEDROID_ENABLED)
#error Bluetooth is not enabled! Please run `make menuconfig` to and enable it
#endif

BluetoothSerial SerialBT;
#endif

#define ESP32_WROOM_32

#if defined ESP32_WROOM_32
#define WIFI
#endif

#include <PubSubClient.h>

#if defined WIFI
#include <WiFi.h>

const char* ssid = "peno2 2.4 GHz";
const char* password = "QueeQueg";

#elif defined(__AVR_ATmega328P__) || defined(__AVR_ATmega328__)
// Nano
#include <EthernetENC.h> // uses a bit less memory
//#include <UIPEthernet.h> // uses a bit more memory
#else
// Mega
#include <Ethernet.h> // does not work with an Arduino nano and its internet shield because it uses ENC28J60 which is a different instruction set
#endif

/*

Following must be added to configuration.yaml of HA:
This example is for 2 garage doors but can be any number

input_text:
  garagecode_input:
    name: Garage code
    initial: ""
    min: 0  # Optioneel: minimumwaarde
    max: 100  # Optioneel: maximumwaarde
    mode: password  # Optioneel: invoermodus (text of password)
    icon: mdi:keyboard  # Optioneel: pictogram voor de invoer

mqtt:
  sensor:
    - name: "Garage1"
      unique_id: "Garage1"
      state_topic: "Garage/Garage1"

    - name: "Garage2"
      unique_id: "Garage2"
      state_topic: "Garage/Garage2"

    - name: "GarageIP"
      unique_id: "GarageIP"
      state_topic: "Garage/IP"

    - name: "GarageMessage"
      unique_id: "GarageMessage"
      state_topic: "Garage/Message"

switch:
  - platform: template
    switches:
      mqtt_button1:
        friendly_name: "Garage Switch1"
        value_template: ""
        turn_on:
          service: mqtt.publish
          data:
            topic: "GarageCmd/Switch1"
            payload: "{{ states('input_text.garagecode_input') }}"
        turn_off:
          service: mqtt.publish
          data:
            topic: "GarageCmd/Switch1"
            payload: "{{ states('input_text.garagecde_input') }}"
  - platform: template
    switches:
      mqtt_button2:
        friendly_name: "Garage Switch2"
        value_template: ""
        turn_on:
          service: mqtt.publish
          data:
            topic: "GarageCmd/Switch2"
            payload: "{{ states('input_text.garagecode_input') }}"
        turn_off:
          service: mqtt.publish
          data:
            topic: "GarageCmd/Switch2"
            payload: "{{ states('input_text.garagecode_input') }}"

automations.yaml:

- id: '5154208397015'
  alias: Empty garage code on switch garage
  description: ''
  trigger:
  - platform: state
    entity_id:
    - sensor.GarageMessage
  action:
  - service: input_text.set_value
    target:
      entity_id: input_text.garagecode_input
    data:
      value: ''


*/
//#define DHCP

unsigned long mytime = 0;
const char *mqtt_server = "192.168.1.121";         // => Localhost, MOSQUITTO on own PC
const char *mqttUser = "";                          // => I have no password and id !!!
const char *mqttPassword = "";

const int updateInterval = 1000; // Interval in milliseconds

#if defined WIFI

WiFiClient espClient;

#else

EthernetClient espClient;

const byte mac[] = {0x54, 0x34, 0x41, 0x30, 0x30, 0x31}; // physical mac address
const byte ip[] = {192, 168, 1, 179};                   // ip in lan
#if defined DHCP
IPAddress myDns(192, 168, 1, 1);
#endif

#endif

PubSubClient client(espClient);

const unsigned long debounceTime = 100;

struct garageData
{
  int switchGaragePin;          // relais pin to open/close garage
  int statusGarageOpenPin;      // switch pin garage open - can be 0
  int statusGarageClosePin;     // switch pin garage closed - can be 0
  int statusGaragePin;          // relais pin showing open/close garage status - can be 0
  int statusGarageOpen;         // switch garage open
  int statusGarageClose;        // switch garage closed
  unsigned long debounceGarage; // debounce value
};

struct garageData garageData[] = 
{
#if defined ESP32_WROOM_32
  { 25, 18, 21, 0, 2, 2, debounceTime * 2, }, // Garage 1
  { 26, 19, 22, 0, 2, 2, debounceTime * 2, }, // Garage 2
  // any number of garages can be added
#else
  { 3, 6, 8, 0, 2, 2, debounceTime * 2, }, // Garage 1
  { 4, 7, 9, 0, 2, 2, debounceTime * 2, }, // Garage 2
  // any number of garages can be added
#endif  
};

#define nGarages (sizeof(garageData) / sizeof(*garageData))

//#define resetPin 5

bool reset = true;
bool started = true;

#include "secret.h" // contains #define CODE "your secret code"

const char code[] = CODE;

#define maxRetries 3

int nFailedCodes = 0;
unsigned long prevFail = 0;

#define clearMessageTimeout 60000
unsigned long messageTime = 0;

void printSerial(char *data)
{
  int l = strlen(data);
  while (l > 0 && (data[l - 1] == '\r' || data[l - 1] == '\n'))
    data[--l] = 0;
  Serial.print(data);
# if defined SERIAL1
    Serial1.print(data);
# endif
# if defined SERIALBT
    SerialBT.print(data);
# endif
}

void printSerialInt(int a)
{
  Serial.print(a);
# if defined SERIAL1
    Serial1.print(a);
# endif
# if defined SERIALBT
    SerialBT.print(a);
# endif
}

void printSerialln(char *data)
{
  if (data != NULL)
    printSerial(data);
  Serial.println();
# if defined SERIAL1
    Serial1.println();
# endif
# if defined SERIALBT
    SerialBT.println();
# endif
}

void printSerialln()
{
  printSerialln(NULL);
}

void(* resetFunc) (void) = 0;       //declare reset function at address 0

#if defined resetPin

void SetupReset()
{
  digitalWrite(resetPin, HIGH); // Set digital pin to HIGH
  delay(200);
  pinMode(resetPin, OUTPUT); // Make sure it gets the HIGH
  delay(200);
  pinMode(resetPin, INPUT_PULLUP); // Change this to a pullup resistor such that arduino can still reset itself (for example upload sketch)
}

void DoReset()
{
  pinMode(resetPin, OUTPUT);    // Change to an output
  digitalWrite(resetPin, LOW);  // Reset
  // Should not get here but if it does undo reset
  delay(500);
  SetupReset();
  resetFunc(); // try a soft reset
}

#else

#define SetupReset()
#define DoReset resetFunc

#endif

void clearMessageWhenNeeded()
{
  if (messageTime != 0 && (millis() - messageTime >= clearMessageTimeout))
  {
    message("");
    messageTime = 0;
  }
}

void message(char *msg)
{
  client.publish("Garage/Message", msg, true);
  messageTime = millis();
  if (messageTime == 0)
    messageTime = 1;
}

void reconnect()
{
    int count = 0;
    
    // Loop until we're reconnected
    while (!client.connected())
    {
        printSerial("Attempting MQTT connection (");
        printSerialInt(++count);
        printSerial(") ...");
        // Attempt to connect
        if (client.connect("ArduinoGarage", mqttUser, mqttPassword, "Garage/Message", 1, true, "Offline"))
        {
          IPAddress ip;
#         if defined WIFI          
            ip = WiFi.localIP();
#         else
            ip = Ethernet.localIP();
#         endif
          char sip[16];
          sprintf(sip, "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
          client.publish("Garage/IP", sip, true);
          if (reset)
          {
            reset = false;
            message("Reset");
            started = true;
          }
          else
            message("Reconnected");
          printSerialln("connected");
          client.subscribe("GarageCmd/#");
        }
        else
        {            
            printSerial("failed, rc=");
            printSerialInt(client.state());
            if (count >= 10)
            {
              printSerialln(" Unable to connect, reset");
              delay(500);
              DoReset();
            }
            else
            {
              printSerialln(" try again in 5 seconds");
              // Wait 5 seconds before retrying
              delay(5000);
            }
        }
    }
}

void statusGarage(int index, int statusGarageOpenPin, int statusGarageClosePin, int statusGaragePin, unsigned long &debounceGarage, int &statusGarageOpen, int &statusGarageClose)
{
  char buf1[50], buf2[50];
  int statusGarageOpen0;
  int statusGarageClose0;

  if (debounceGarage == 0 || millis() - debounceGarage > debounceTime)
  {
    debounceGarage = 0;

    if (statusGarageOpenPin != 0)
      statusGarageOpen0 = digitalRead(statusGarageOpenPin);
    else
      statusGarageOpen0 = statusGarageOpen;
    if (statusGarageClosePin != 0)
      statusGarageClose0 = digitalRead(statusGarageClosePin);
    else
      statusGarageClose0 = statusGarageClose;

    if (statusGarageOpen0 != statusGarageOpen || statusGarageClose0 != statusGarageClose)
    {
      printSerial("change garage ");
      printSerialInt(index + 1);
      printSerial(" status: ");
      printSerialInt(statusGarageOpen0);
      printSerial(" - ");
      printSerialInt(statusGarageClose0);
      printSerialln();

      statusGarageOpen = statusGarageOpen0;
      statusGarageClose = statusGarageClose0;

      if (statusGarageOpenPin == 0)
        statusGarageOpen = !statusGarageClose;
      if (statusGarageClosePin == 0)
        statusGarageClose = !statusGarageOpen;

      if (statusGaragePin != 0)
        digitalWrite(statusGaragePin, statusGarageOpen0);

      debounceGarage = millis();

      sprintf(buf2, "Garage/Garage%d", index + 1);
      sprintf(buf1, statusGarageOpen == 0 ? statusGarageClose == 0 ? "Open and Closed" : "Open" : statusGarageClose == 0 ? "Closed" : "In between");

      printSerial(buf2);
      printSerial(": ");
      printSerialln(buf1);
      client.publish(buf2, buf1, true);  
    }
  }
}

void statusGarages()
{
  for (int index = 0; index < nGarages; index++)
  {
    unsigned long debounceGarage = garageData[index].debounceGarage;
    int statusGarageOpen = garageData[index].statusGarageOpen;
    int statusGarageClose = garageData[index].statusGarageClose;
    statusGarage(index, garageData[index].statusGarageOpenPin, garageData[index].statusGarageClosePin, garageData[index].statusGaragePin, debounceGarage, statusGarageOpen, statusGarageClose);
    garageData[index].statusGarageOpen = statusGarageOpen;
    garageData[index].statusGarageClose = statusGarageClose;
    garageData[index].debounceGarage = debounceGarage;
  }
}

bool checkCode(char* payload)
{
  return (strlen(payload) == strlen(code)) && (strcmp(payload, code) == 0);
}

void callback(char* topic, byte* payload, unsigned int length)
{
  char buf[sizeof(code) + 1];
  char str[2] = " ";

  printSerial("Message arrived [");
  printSerial(topic);
  printSerial("] ");
  memset(buf, 'x', sizeof(buf) - 1);
  for (int i = 0; i < length; i++)
  {
    if (i < sizeof(code))      
      buf[i] = (char)payload[i];
    str[0] = (char)payload[i];
    printSerial(str);
  }  
  buf[min(sizeof(buf) - 1, length)] = 0;

  printSerialln();
  printSerial("length: ");
  printSerialInt(length);
  printSerialln();

  if (nFailedCodes >= maxRetries)
  {
    if (millis() - prevFail > 60000L)
      nFailedCodes = 0;
    else
      message("Wait a minute...");
  }
  
  if (nFailedCodes < maxRetries)
  {
    if (checkCode(buf))
      nFailedCodes = 0;
    else
    {
      message("Wrong code");
      prevFail = millis();
      nFailedCodes++;
    }
  }

  printSerial("nFailedCodes: ");
  printSerialInt(nFailedCodes);
  printSerialln();

  if (nFailedCodes == 0)
  {
    for (int index = 0; index < nGarages; index++)
    {
      char buf[18];

      sprintf(buf, "GarageCmd/Switch%d", index + 1);
      if (strcmp(topic, buf) == 0)
      {
        message("");
        sprintf(buf, "Switch garage %d", index + 1);
        message(buf);

        //digitalWrite(switchGarage1Pin, !digitalRead(switchGarage1Pin));
        digitalWrite(garageData[index].switchGaragePin, HIGH);
        delay(500);
        digitalWrite(garageData[index].switchGaragePin, LOW);
        break;
      }
    }
  }
}

void setup()
{
  SetupReset();
    
# if defined ESP32_WROOM_32
    Serial.begin(115200);
# else    
    Serial.begin(9600);
# endif

#if defined SERIALBT
    SerialBT.begin("GarageDoorController"); //Bluetooth device name
#endif

    delay(100);

    printSerialln("Setup");

    for (int index = 0; index < nGarages; index++)
    {
      pinMode(garageData[index].switchGaragePin, OUTPUT);
      if (garageData[index].statusGarageOpenPin != 0)
        pinMode(garageData[index].statusGarageOpenPin, INPUT_PULLUP); 
      if (garageData[index].statusGarageClosePin != 0)
        pinMode(garageData[index].statusGarageClosePin, INPUT_PULLUP);
      if (garageData[index].statusGaragePin != 0)
        pinMode(garageData[index].statusGaragePin, OUTPUT);
    }

#if defined WIFI

  delay(10);

  printSerialln();
  printSerial("Connecting to ");
  printSerialln((char*)ssid);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      printSerial(".");
  }

  printSerialln();
  printSerialln("WiFi connected");

  IPAddress ip = WiFi.localIP();

  char sip[16];
  sprintf(sip, "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);

  printSerial("IP address: ");
  printSerialln(sip);
/*
  byte mac[6];
  WiFi.macAddress(mac);
  printSerial("MAC address: ");
  printSerial(mac[0],HEX);
  printSerial(":");
  printSerial(mac[1],HEX);
  printSerial(":");
  printSerial(mac[2],HEX);
  printSerial(":");
  printSerial(mac[3],HEX);
  printSerial(":");
  printSerial(mac[4],HEX);
  printSerial(":");
  printSerialln(mac[5],HEX);
*/  
#else

    // dht.begin();
    printSerialln("Initialize Ethernet ...");
    Ethernet.init(10); // ok
    //Ethernet.init(5);
    printSerialln("Initialize Ethernet done");
    
#if defined DHCP
  // start the Ethernet connection:
  printSerialln("Initialize Ethernet with DHCP:");
  if (Ethernet.begin(mac) == 0) {
    printSerialln("Failed to configure Ethernet using DHCP");
    // Check for Ethernet hardware present
    if (Ethernet.hardwareStatus() == EthernetNoHardware) {
      printSerialln("Ethernet shield was not found.  Sorry, can't run without hardware. :(");
      while (true) {
        delay(1); // do nothing, no point running without Ethernet hardware
      }
    }
    if (Ethernet.linkStatus() == LinkOFF) {
      printSerialln("Ethernet cable is not connected.");
    }
    // try to configure using IP address instead of DHCP:
    Ethernet.begin(mac, ip, myDns);
    printSerial("My IP address: ");

    IPAddress ip = Ethernet.localIP();

    char sip[16];
    sprintf(sip, "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
  
    printSerialln(sip);
  } else {
    printSerial("  DHCP assigned IP ");
    IPAddress ip = Ethernet.localIP();

    char sip[16];
    sprintf(sip, "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
  
    printSerialln(sip);
  }
#else
    Ethernet.begin(mac, ip);
#endif
  
#endif

  // give the Ethernet shield a second to initialize:
  delay(1000);
    
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);

  mytime = millis();

  reset = true;
  started = true;
  nFailedCodes = 0;
}

unsigned long counter=0;

void loop()
{
    if (!client.connected())
      reconnect();
    else
      statusGarages();

    client.loop();

    if (millis() - mytime > updateInterval)
    {
      printSerial("Alive");
      printSerialInt(++counter);
      printSerialln();

      if (started)
      {
        started = false;
        message("Started");
      }
      mytime = millis();
    }

    clearMessageWhenNeeded();
}
