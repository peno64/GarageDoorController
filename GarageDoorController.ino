// Works on both the Arduino Nano and Mega

// See https://community.platformio.org/t/mqtt-with-arduino-nano-and-enc2860/34236/7
// See https://pubsubclient.knolleary.net/
// See http://www.steves-internet-guide.com/using-arduino-pubsub-mqtt-client/

// See also for future reference: https://www.instructables.com/DIY-Smart-Garage-Door-Opener-Using-ESP8266-Wemos-D/
//  This uses https://www.home-assistant.io/integrations/cover.mqtt/

#include <PubSubClient.h>

#if defined(__AVR_ATmega328P__) || defined(__AVR_ATmega328__)
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

    - name: "GarageStatus"
      unique_id: "GarageStatus"
      state_topic: "Garage/Status"

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
const byte mac[] = {0x54, 0x34, 0x41, 0x30, 0x30, 0x31}; // physical mac address
const byte ip[] = {192, 168, 1, 179};                   // ip in lan
#if defined DHCP
IPAddress myDns(192, 168, 1, 1);
#endif
const char *mqtt_server = "192.168.1.121";         // => Localhost, MOSQUITTO on own PC
const char *mqttUser = "";                          // => I have no password and id !!!
const char *mqttPassword = "";

const int updateInterval = 1000; // Interval in milliseconds
EthernetClient espClient;
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
  { 3, 6, 8, 0, 2, 2, debounceTime * 2, }, // Garage 1
  { 4, 7, 9, 0, 2, 2, debounceTime * 2, }, // Garage 2
  // any number of garages can be added
};

#define nGarages (sizeof(garageData) / sizeof(*garageData))

const int resetPin = 5;

bool reset = true;
bool started = true;

#include "secret.h" // contains #define CODE "your secret code"

const char code[] = CODE;

#define maxRetries 3

int nFailedCodes = 0;
unsigned long prevFail = 0;

#define clearMessageTimeout 60000
unsigned long messageTime = 0;

void SetupReset()
{
  digitalWrite(resetPin, HIGH); // Set digital pin to 5V
  delay(200);
  pinMode(resetPin, OUTPUT); // Make sure it gets the 5V
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
}

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
        Serial.print("Attempting MQTT connection (");
        Serial.print(++count);
        Serial.print(") ...");
        // Attempt to connect
        if (client.connect("ArduinoGarage", mqttUser, mqttPassword, "Garage/Status", 1, true, "Offline"))
        {
            client.publish("Garage/Status", "Online", true);
            
            if (reset)
            {
              reset = false;
              message("Reset");
              started = true;
            }
            else
              message("Reconnected");
            Serial.println("connected");
            client.subscribe("GarageCmd/#");
        }
        else
        {
            if (count >= 10)
            {
              Serial.println("Unable to connect, reset arduino");
              delay(500);
              DoReset();
            }
            else
            {
              Serial.print("failed, rc=");
              Serial.print(client.state());
              Serial.println(" try again in 5 seconds");
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
      Serial.print("change garage ");
      Serial.print(index + 1);
      Serial.print(" status: ");
      Serial.print(statusGarageOpen0);
      Serial.print(" - ");
      Serial.println(statusGarageClose0);

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

      Serial.print(buf2);
      Serial.print(": ");
      Serial.println(buf1);
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

  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  memset(buf, 'x', sizeof(buf) - 1);
  for (int i=0;i<length;i++) {
    if (i < sizeof(code))      
      buf[i] = (char)payload[i];
    Serial.print((char)payload[i]);
  }  
  buf[min(sizeof(buf) - 1, length)] = 0;

  Serial.println();

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

  Serial.print("nFailedCodes: ");
  Serial.println(nFailedCodes);

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
  
    Serial.begin(9600);
    delay(100);

    Serial.println("Setup");

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

    // dht.begin();
    Serial.println("Initialize Ethernet ...");
    Ethernet.init(10); // ok
    //Ethernet.init(5);
    Serial.println("Initialize Ethernet done");
    
#if defined DHCP
  // start the Ethernet connection:
  Serial.println("Initialize Ethernet with DHCP:");
  if (Ethernet.begin(mac) == 0) {
    Serial.println("Failed to configure Ethernet using DHCP");
    // Check for Ethernet hardware present
    if (Ethernet.hardwareStatus() == EthernetNoHardware) {
      Serial.println("Ethernet shield was not found.  Sorry, can't run without hardware. :(");
      while (true) {
        delay(1); // do nothing, no point running without Ethernet hardware
      }
    }
    if (Ethernet.linkStatus() == LinkOFF) {
      Serial.println("Ethernet cable is not connected.");
    }
    // try to configure using IP address instead of DHCP:
    Ethernet.begin(mac, ip, myDns);
    Serial.print("My IP address: ");
    Serial.println(Ethernet.localIP());
  } else {
    Serial.print("  DHCP assigned IP ");
    Serial.println(Ethernet.localIP());
  }
#else
    Ethernet.begin(mac, ip);
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

void loop()
{
    if (!client.connected())
      reconnect();
    else
      statusGarages();

    client.loop();

    if (millis() - mytime > updateInterval)
    {
        if (started)
        {
          started = false;
          message("Started");
        }
        mytime = millis();
    }

    clearMessageWhenNeeded();
}