// Works on both the Arduino Nano and Mega and on esp32 (tested with WROOM32 Devkit V1)

// See https://community.platformio.org/t/mqtt-with-arduino-nano-and-enc2860/34236/7
// See https://pubsubclient.knolleary.net/
// See http://www.steves-internet-guide.com/using-arduino-pubsub-mqtt-client/

// See also for future reference: https://www.instructables.com/DIY-Smart-Garage-Door-Opener-Using-ESP8266-Wemos-D/
//  This uses https://www.home-assistant.io/integrations/cover.mqtt/


/*
  Content of secret.h:

#define WIFISSID "<your wifi ssid>"
#define WIFIPASSWORD "<your wifi password>"

#define MQTTHOST "<your mqtt ip or name>"

#define MQTTUSER "<your mqtt user>"
#define MQTTPASSWORD "<your mqtt password>"

#define GARAGEDOORCODE "<your garagedoor code>"

#define UPLOADUSER "<your upload user>"
#define UPLOADPASSWORD "<your upload password>"

*/

//#define WOKWI // simulator https://wokwi.com/

#include "secret.h"

#define postfix ""

#define myName "GarageDoorController" postfix

#define VERSIONSTRING "Copyright peno " __DATE__ " " __TIME__

#if defined WOKWI
#define MQTTid "wokwiGarage" postfix
#else
#define MQTTid "Garage" postfix
#endif
#define MQTTcmd MQTTid "Cmd"

#define ReconnectWaitSeconds 5
#define ReconnectRebootMinutes 30

#define ESP32_DEVKIT_V1

#if defined ESP32_DEVKIT_V1 && !defined WOKWI
# define SERIALBT
#endif

#define LOGGING

#if defined LOGGING
#define maxNLogs 50
#define maxLogSize 100
int logsIndex = 0;
int logOffset = 0;
char logs[maxNLogs][maxLogSize];
#endif

#if defined SERIALBT

#include "BluetoothSerial.h"

#if !defined(CONFIG_BT_ENABLED) || !defined(CONFIG_BLUEDROID_ENABLED)
#error Bluetooth is not enabled! Please run `make menuconfig` to and enable it
#endif

BluetoothSerial SerialBT;
#endif

#if defined ESP32_DEVKIT_V1
#define WIFI
#if !defined WOKWI
#define OTA
#endif
#endif

#include <PubSubClient.h>

#if defined WIFI

#include <WiFi.h>

#if defined OTA

#include <WiFiClient.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Update.h>

#endif // OTA

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

    - name: "GarageUpTime"
      unique_id: "GarageUpTime"
      state_topic: "Garage/UpTime"

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

recorder:
  exclude:
    entities:
      - sensor.garageuptime

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

unsigned long mytime = 0;

#if defined WIFI

WiFiClient espClient;

#else

//#define DHCP

EthernetClient espClient;

const byte mac[] = {0x54, 0x34, 0x41, 0x30, 0x30, 0x31}; // physical mac address
const byte ip[] = {192, 168, 1, 179};                   // ip in lan
#if defined DHCP
IPAddress myDns(192, 168, 1, 1);
#endif

#endif

PubSubClient mqttClient(espClient);

const unsigned long debounceTime = /* 500 */ 0;

struct garageData
{
  int switchGaragePin;          // relais pin to open/close garage
  int statusGarageOpenPin;      // switch pin garage open - can be 0
  int statusGarageClosePin;     // switch pin garage closed - can be 0
  int statusGaragePin;          // relais pin showing open/close garage status - can be 0
  int statusGarageOpen;         // switch garage open
  int statusGarageClose;        // switch garage closed
  int statusGarageInbetween;    // in between status
  unsigned long debounceGarage; // debounce value
};

struct garageData garageData[] =
{
#if defined ESP32_DEVKIT_V1
  { 25, 18, /* 21 */ /* 23 */ /* 14 */ 17, 0, 2, 2, 0, }, // Garage 1
  { 26, /* 19 */ 16, 22, 0, 2, 2, 0, }, // Garage 2
  // any number of garages can be added
#else
  { 3, 6, 8, 0, 2, 2, 0, }, // Garage 1
  { 4, 7, 9, 0, 2, 2, 0, }, // Garage 2
  // any number of garages can be added
#endif
};

#define nGarages (sizeof(garageData) / sizeof(*garageData))

//#define resetPin 5

bool reset = true;
bool started = true;

#define maxRetries 3

int nFailedCodes = 0;
unsigned long prevFail = 0;

#define clearMessageTimeout 60000
unsigned long messageTime = 0;

unsigned int uptimeDays = 0;
unsigned char uptimeHours = 0;
unsigned char uptimeMinutes = 0;
unsigned char uptimeSeconds = 0;

#if defined LOGGING
void KeepMessage(char *data)
{
  if (logOffset == 0)
    logOffset += sprintf(logs[logsIndex], "%u:%02d:%02d:%02d: ", uptimeDays, (int)uptimeHours, (int)uptimeMinutes, (int)uptimeSeconds);
  logOffset += snprintf(logs[logsIndex] + logOffset, maxLogSize - logOffset - 1, data);
}
#endif

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
# if defined LOGGING
    KeepMessage(data);
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
# if defined LOGGING
    char data[10];
    sprintf(data, "%d", a);
    KeepMessage(data);
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
# if defined LOGGING
    if (++logsIndex >= maxNLogs)
      logsIndex = 0;
    logOffset = 0;
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

#if defined ESP32
#define DoReset() ESP.restart()
#else
#define DoReset() resetFunc()
#endif

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
  if (mqttClient.connected())
    mqttClient.publish(MQTTid "/Message", msg, true);
  messageTime = millis();
  if (messageTime == 0)
    messageTime = 1;
}

void reconnect()
{
    static int count = 0;
    static unsigned long msWait = 0;

    // Loop until we're reconnected
    if ((count == 0 || millis() - msWait > ReconnectWaitSeconds * 1000) &&
        (!mqttClient.connected()))
    {
        printSerial("Attempting MQTT connection (");
        printSerialInt(++count);
        printSerial(") ...");
        // Attempt to connect
        if (mqttClient.connect(myName, MQTTUSER, MQTTPASSWORD, MQTTid "/Message", 1, true, "Offline"))
        {
          IPAddress ip;
#         if defined WIFI
            ip = WiFi.localIP();
#         else
            ip = Ethernet.localIP();
#         endif
          char sip[16];
          sprintf(sip, "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
          mqttClient.publish(MQTTid "/IP", sip, true);
          if (reset)
          {
            reset = false;
            message("Reset");
            started = true;
          }
          else
            message("Reconnected");
          printSerialln("connected");
          mqttClient.subscribe(MQTTcmd "/#");
          count = 0;
        }
        else
        {
            printSerial("failed, rc=");
            printSerialInt(mqttClient.state());
            if (count >= ReconnectRebootMinutes * 60 / ReconnectWaitSeconds)
            {
              printSerialln(" Unable to connect, reset");
              delay(500);
              count = 0;
              DoReset();
            }
            else
            {
              printSerialln(" try again in 5 seconds");
              // Wait 5 seconds before retrying
              msWait = millis();
            }
        }
    }

    if (count != 0 && mqttClient.connected())
      count = 0;
}

void statusGarage(int index, int statusGarageOpenPin, int statusGarageClosePin, int statusGaragePin, unsigned long &debounceGarage, int &statusGarageOpen, int &statusGarageClose, int &statusGarageInbetween)
{
  char buf1[50], buf2[50];
  int statusGarageOpen0;
  int statusGarageClose0;

  if (statusGarageOpenPin != 0)
    statusGarageOpen0 = digitalRead(statusGarageOpenPin);
  else
    statusGarageOpen0 = statusGarageOpen;
  if (statusGarageClosePin != 0)
    statusGarageClose0 = digitalRead(statusGarageClosePin);
  else
    statusGarageClose0 = statusGarageClose;

  if (statusGarageOpen0 == statusGarageOpen && statusGarageClose0 == statusGarageClose)
    debounceGarage = 0;
  else if (debounceTime != 0 && debounceGarage == 0)
    debounceGarage = millis();
  else if ((debounceTime == 0) || (millis() - debounceGarage > debounceTime))
  {
    debounceGarage = 0;

    if (statusGarageOpenPin == 0)
      statusGarageOpen0 = !statusGarageClose0;
    if (statusGarageClosePin == 0)
      statusGarageClose0 = !statusGarageOpen0;

    printSerial("change garage ");
    printSerialInt(index + 1);
    printSerial(" status: ");
    printSerialInt(statusGarageOpen0);
    printSerial(" - ");
    printSerialInt(statusGarageClose0);
    printSerial(" - statusGarageOpenPin: ");
    printSerialInt(statusGarageOpenPin);
    printSerial(" - statusGarageClosePin: ");
    printSerialInt(statusGarageClosePin);
    printSerialln();

    if ((statusGarageOpen0 && statusGarageClose0) ||
        (statusGarageOpenPin == 0) ||
        (statusGarageClosePin == 0))
      statusGarageInbetween = statusGarageOpen;

    statusGarageOpen = statusGarageOpen0;
    statusGarageClose = statusGarageClose0;

    if (statusGaragePin != 0)
      digitalWrite(statusGaragePin, statusGarageClose == 0 && statusGarageOpen != 0 ? LOW : HIGH);

    sprintf(buf2, MQTTid "/Garage%d", index + 1);
    sprintf(buf1, statusGarageOpen == 0 ? statusGarageClose == 0 ? "Open and Closed" : "Open" : statusGarageClose == 0 ? "Closed" : statusGarageInbetween ? "Opening" : "Closing");

    if ((statusGarageOpen == 0 && statusGarageClose != 0) ||
        (statusGarageOpen != 0 && statusGarageClose == 0))
      message("");

    printSerial(buf2);
    printSerial(": ");
    printSerialln(buf1);
    if (mqttClient.connected())
      mqttClient.publish(buf2, buf1, true);
  }
}

void statusGarages()
{
  for (int index = 0; index < nGarages; index++)
  {
    unsigned long debounceGarage = garageData[index].debounceGarage;
    int statusGarageOpen = garageData[index].statusGarageOpen;
    int statusGarageClose = garageData[index].statusGarageClose;
    int statusGarageInbetween = garageData[index].statusGarageInbetween;
    statusGarage(index, garageData[index].statusGarageOpenPin, garageData[index].statusGarageClosePin, garageData[index].statusGaragePin, debounceGarage, statusGarageOpen, statusGarageClose, statusGarageInbetween);
    garageData[index].debounceGarage = debounceGarage;
    garageData[index].statusGarageOpen = statusGarageOpen;
    garageData[index].statusGarageClose = statusGarageClose;
    garageData[index].statusGarageInbetween = statusGarageInbetween;
  }
}

bool checkCode(char* payload)
{
  return (strlen(payload) == (sizeof(GARAGEDOORCODE) - 1)) && (strcmp(payload, GARAGEDOORCODE) == 0);
}

void switchGarage(int index)
{
  digitalWrite(garageData[index].switchGaragePin, HIGH);
  delay(500);
  digitalWrite(garageData[index].switchGaragePin, LOW);
}

void callback(char* topic, byte* payload, unsigned int length)
{
  char buf[sizeof(GARAGEDOORCODE) + 1];
  char str[2] = " ";
  bool print = true;

  printSerial("Message arrived [");
  printSerial(topic);
  printSerial("] ");
  memset(buf, 'x', sizeof(buf) - 1);
  buf[min(sizeof(buf) - 1, length)] = 0;
  for (int i = 0; i < length; i++)
  {
    if (i < sizeof(buf) - 2)
      buf[i] = (char)payload[i];
    else
    {
      print = false;
      if (i == sizeof(buf) - 2)
      {
        char c = buf[i];
        buf[i] = 0;
        printSerial(buf);
        buf[i] = c;
      }
      str[0] = (char)payload[i];
      printSerial(str);
    }
  }
  buf[min(sizeof(buf) - 1, length)] = 0;
  if (print)
  {
    if (checkCode(buf))
      printSerial("*");
    else
      printSerial(buf);
  }

  printSerialln();
  printSerial("length: ");
  printSerialInt(length);
  printSerialln();

  if (*buf)
  {
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
        char buf[255];

        sprintf(buf, MQTTcmd "/Switch%d", index + 1);
        if (strcmp(topic, buf) == 0)
        {
          message("");
          sprintf(buf, "Switch garage %d", index + 1);
          message(buf);

          switchGarage(index);
          break;
        }
      }
    }
  }
}

#if defined OTA

#include "fs_WebServer.h"

fs_WebServer server(80);

void garagesContent()
{
  server.chunkedResponseModeStart(200, "text/html");

  server.sendContent("<html><head><meta name='viewport' content='width=device-width, initial-scale=1.0'></head><body><h3>" myName "</h3><table>");

  for (int index = 0; index < nGarages; index++)
  {
    char buf[3];

    server.sendContent("<tr><td>Garage ");
    sprintf(buf, "%d", index + 1);
    server.sendContent(buf);
    server.sendContent(":</td><td>");
    int statusGarageOpen = garageData[index].statusGarageOpen;
    int statusGarageClose = garageData[index].statusGarageClose;
    int statusGarageInbetween = garageData[index].statusGarageInbetween;
    server.sendContent(statusGarageOpen == 0 ? statusGarageClose == 0 ? "Open and Closed" : "Open" : statusGarageClose == 0 ? "Closed" : statusGarageInbetween ? "Opening" : "Closing");
    server.sendContent("</td><td><a href='/switchgarage");
    server.sendContent(buf);
    server.sendContent("'><button>Switch</button></a></td></tr>");
  }

  server.sendContent("</table><br><a href='/garages'><button>Refresh</button></a><br><br><a href='/'><button>Main menu</button></a>");

  server.chunkedResponseFinalize();
}

void logContent()
{
  server.chunkedResponseModeStart(200, "text/html");

#if defined LOGGING
  server.sendContent("<html><head><meta name='viewport' content='width=device-width, initial-scale=1.0'></head><body>" myName " - current version " VERSIONSTRING " - Uptime: ");
  char buf[14];
  sprintf(buf, "%u:%02d:%02d:%02d", uptimeDays, (int)uptimeHours, (int)uptimeMinutes, (int)uptimeSeconds);
  server.sendContent(buf);
  server.sendContent("<br><br><fieldset style='display: inline-block; border: 2px solid black; padding: 10px;'><legend style='font-weight: bold; padding: 0 5px;'>Message history</legend>");
  int j = logsIndex - 1;
  for (int i = 0; i < maxNLogs; i++)
  {
    if (++j >= maxNLogs)
      j = 0;
    if (logs[j][0])
    {
      server.sendContent(logs[j]);
      server.sendContent("<br>");
    }
  }
  server.sendContent("</fieldset><br><br><a href='/log'><button>Refresh</button></a><br><br><a href='/'><button>Main menu</button></a></body></html>");
#else
  server.sendContent("<html><body>" myName " - current version " VERSIONSTRING "<br><a href='/'><button>Main menu</button></a></body></html>");
#endif
  server.chunkedResponseFinalize();
}

void serverSendContent(char *data)
{
  server.sendContent(data);
  server.sendContent("<br>");
}

void info()
{
  char buf[50];

  server.chunkedResponseModeStart(200, "text/html");

  server.sendContent("<html><head><meta name='viewport' content='width=device-width, initial-scale=1.0'></head><body>" myName " - current version " VERSIONSTRING " - Uptime: ");
  sprintf(buf, "%u:%02d:%02d:%02d", uptimeDays, (int)uptimeHours, (int)uptimeMinutes, (int)uptimeSeconds);
  server.sendContent(buf);
  server.sendContent("<br><br>");

# if defined WIFI
  extern void printWiFi(void (*callback)(char *));

  printWiFi(serverSendContent);
#endif

  server.sendContent("<br><br><a href='/'><button>Main menu</button></a></body></html>");

  server.chunkedResponseFinalize();
}

#define UPDATEPAGE "/jsdlmkfjcnsdjkqcfjdlkckslcndsjfsdqfjksd" // a name that nobody can figure out

const char *uploadContent =
"<head><meta name='viewport' content='width=device-width, initial-scale=1.0'></head>"
"<script src='https://ajax.googleapis.com/ajax/libs/jquery/3.2.1/jquery.min.js'></script>"
"<form method='POST' action='#' enctype='multipart/form-data' id='upload_form'>"
   "Update firmware " myName " (current version " VERSIONSTRING ")"
   "<br>"
   "<br>"
   "<input type='file' name='update'>"
   "<br>"
   "<br>"
   "<input type='submit' value='Update'>"
"</form>"
 "<div id='prg'>progress: 0%</div>"
 "<br><a href='/'><button>Main menu</button></a>"
 "<script>"
  "$('form').submit(function(e){"
  "e.preventDefault();"
  "var form = $('#upload_form')[0];"
  "var data = new FormData(form);"
  " $.ajax({"
  "url: '" UPDATEPAGE "',"
  "type: 'POST',"
  "data: data,"
  "contentType: false,"
  "processData:false,"
  "xhr: function() {"
  "var xhr = new window.XMLHttpRequest();"
  "xhr.upload.addEventListener('progress', function(evt) {"
  "if (evt.lengthComputable) {"
  "var per = evt.loaded / evt.total;"
  "$('#prg').html('progress: ' + Math.round(per*100) + '%');"
  "}"
  "}, false);"
  "return xhr;"
  "},"
  "success:function(d, s) {"
  "console.log('success!')"
 "},"
 "error: function (a, b, c) {"
 "}"
 "});"
 "});"
 "</script>";

const char *host = myName;

void setupOTA()
{
  /*use mdns for host name resolution*/
  if (!MDNS.begin(host))
  { //http://esp32.local
    printSerialln("Error setting up MDNS responder!");
    while (1)
    {
      delay(1000);
    }
  }
  printSerialln("mDNS responder started");

 /*home*/
 server.on("/", HTTP_GET, []()
 {
    // See https://github.com/espressif/arduino-esp32/blob/master/libraries/WebServer/examples/HttpBasicAuth/HttpBasicAuth.ino
    if (!server.authenticate(UPLOADUSER, UPLOADPASSWORD))
    {
      return server.requestAuthentication();
    }

    server.sendHeader("Connection", "close");
    server.send(200, "text/html",
    "<html><head><meta name='viewport' content='width=device-width, initial-scale=1.0'></head><body>"
    "<h3>" myName "</h3>"
    "<table>"
    "<tr><td><a href='/garages'><button>Garages</button></a></td></tr>"
    "<tr><td><a href='/log'><button>Log</button></a></td></tr>"
    "<tr><td><a href='/info'><button>Info</button></a></td></tr>"
    "<tr><td><a href='/reset'><button>Reset</button></a></td></tr>"
    "<tr><td><a href='/upload'><button>Update firmware</button></a></td></tr>"
    "</table>"
    "</body></html>");
  });

/*garages*/
 server.on("/garages", HTTP_GET, []()
 {
    // See https://github.com/espressif/arduino-esp32/blob/master/libraries/WebServer/examples/HttpBasicAuth/HttpBasicAuth.ino
    if (!server.authenticate(UPLOADUSER, UPLOADPASSWORD))
    {
      return server.requestAuthentication();
    }
    garagesContent();
  });

  for (int index = 0; index < nGarages; index++)
  {
    char buf[16];

    sprintf(buf, "/switchgarage%d", index + 1);

    server.on(buf, HTTP_GET, [index]()
    {
        // See https://github.com/espressif/arduino-esp32/blob/master/libraries/WebServer/examples/HttpBasicAuth/HttpBasicAuth.ino
        if (!server.authenticate(UPLOADUSER, UPLOADPASSWORD))
        {
          return server.requestAuthentication();
        }

        switchGarage(index);

        delay(1000);

        server.sendHeader("Connection", "close");
        server.send(200, "text/html", "<html><head><meta http-equiv='refresh' content='0;url=/garages'></head><body><a href='/'><button>Main menu</button></a></body></html>");
      });
  }

 /*logs*/
 server.on("/log", HTTP_GET, []()
 {
    // See https://github.com/espressif/arduino-esp32/blob/master/libraries/WebServer/examples/HttpBasicAuth/HttpBasicAuth.ino
    if (!server.authenticate(UPLOADUSER, UPLOADPASSWORD))
    {
      return server.requestAuthentication();
    }
    logContent();
  });

 /*info*/
 server.on("/info", HTTP_GET, []()
 {
    // See https://github.com/espressif/arduino-esp32/blob/master/libraries/WebServer/examples/HttpBasicAuth/HttpBasicAuth.ino
    if (!server.authenticate(UPLOADUSER, UPLOADPASSWORD))
    {
      return server.requestAuthentication();
    }
    info();
  });

 /*reset*/
 server.on("/reset", HTTP_GET, []()
 {
    // See https://github.com/espressif/arduino-esp32/blob/master/libraries/WebServer/examples/HttpBasicAuth/HttpBasicAuth.ino
    if (!server.authenticate(UPLOADUSER, UPLOADPASSWORD))
    {
      return server.requestAuthentication();
    }
    DoReset();
  });

  /*return upload page which is stored in uploadContent */
  server.on("/upload", HTTP_GET, []()
  {
    // See https://github.com/espressif/arduino-esp32/blob/master/libraries/WebServer/examples/HttpBasicAuth/HttpBasicAuth.ino
    if (!server.authenticate(UPLOADUSER, UPLOADPASSWORD))
    {
      return server.requestAuthentication();
    }
    server.sendHeader("Connection", "close");
    server.send(200, "text/html", uploadContent);
  });
  /*handling uploading firmware file */
  server.on(UPDATEPAGE, HTTP_POST, []()
  {
    // See https://github.com/espressif/arduino-esp32/blob/master/libraries/WebServer/examples/HttpBasicAuth/HttpBasicAuth.ino
    if (!server.authenticate(UPLOADUSER, UPLOADPASSWORD))
    {
      return server.requestAuthentication();
    }
    server.sendHeader("Connection", "close");
    server.send(200, "text/plain", (Update.hasError()) ? "FAIL" : "OK");
    ESP.restart();
  }, []()
  {
    HTTPUpload& upload = server.upload();
    if (upload.status == UPLOAD_FILE_START)
    {
      Serial.printf("Update: %s\n", upload.filename.c_str());
      if (!Update.begin(UPDATE_SIZE_UNKNOWN))
      { //start with max available size
        Update.printError(Serial);
      }
    } else if (upload.status == UPLOAD_FILE_WRITE)
    {
      /* flashing firmware to ESP*/
      if (Update.write(upload.buf, upload.currentSize) != upload.currentSize)
      {
        Update.printError(Serial);
      }
    } else if (upload.status == UPLOAD_FILE_END)
    {
      if (Update.end(true))
      { //true to set the size to the current progress
        Serial.printf("Update Success: %u\nRebooting...\n", upload.totalSize);
      } else
      {
        Update.printError(Serial);
      }
    }
  });

  server.begin();
}

#endif // OTA

#if defined WIFI

uint8_t BSSID[6];

void printWiFi(void (*callback)(char *))
{
  char buf[50];

  sprintf(buf, "SSID: %s", WiFi.SSID());
  callback(buf);

  IPAddress ip = WiFi.localIP();
  sprintf(buf, "IP address: %d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
  callback(buf);

  byte mac[6];
  WiFi.macAddress(mac);
  sprintf(buf, "MAC address: %02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  callback(buf);

  uint8_t* currentBSSID = WiFi.BSSID();
  sprintf(buf, "BSSID: %02X:%02X:%02X:%02X:%02X:%02X", currentBSSID[0], currentBSSID[1], currentBSSID[2], currentBSSID[3], currentBSSID[4], currentBSSID[5]);
  callback(buf);

  sprintf(buf, "RRSI: %d dBm", (int)WiFi.RSSI());
  callback(buf);
}

void wifiBegin()
{
  WiFi.disconnect(); // Ensure no active connection during the scan
  delay(1000);

  // Search all routers that give this SSID signal
  // Determine the best signal
  int numNetworks = WiFi.scanNetworks(false, false, false, 0, 300, WIFISSID);
  int maxSignal = -1000;
  for (int i = 0; i < numNetworks; i++)
  {
    char buf[255];

    sprintf(buf, "%d: SSID: %s, RRSI: %d dBm, BSSID: %02X:%02X:%02X:%02X:%02X:%02X",
                  i + 1,
                  WiFi.SSID(i).c_str(),
                  WiFi.RSSI(i),
                  WiFi.BSSID(i)[0], WiFi.BSSID(i)[1], WiFi.BSSID(i)[2],
                  WiFi.BSSID(i)[3], WiFi.BSSID(i)[4], WiFi.BSSID(i)[5]);
    if (WiFi.RSSI(i) > maxSignal)
    {
      maxSignal = WiFi.RSSI(i);
      for (int j = 0; j < 6; j++)
        BSSID[j] = WiFi.BSSID(i)[j];
    }
    printSerialln(buf);
  }

  if (maxSignal != -1000)
  {
    char buf[255];
    sprintf(buf, "Selected BSSID: %02X:%02X:%02X:%02X:%02X:%02X", BSSID[0], BSSID[1], BSSID[2], BSSID[3], BSSID[4], BSSID[5]);
    printSerialln(buf);
  }
  else
    printSerialln("SSID not found...");

  WiFi.scanDelete();

  if (maxSignal != -1000)
    WiFi.begin(WIFISSID, WIFIPASSWORD, 0, BSSID);
  else
    WiFi.begin(WIFISSID, WIFIPASSWORD);

  printSerial("Connecting WiFi");
  unsigned long t1 = millis();
  while (WiFi.status() != WL_CONNECTED)
  {
      unsigned long t2 = millis();
      if (t2 - t1 > 5 * 60 * 1000) // try 5 minutes
        DoReset();

      delay(500);
      printSerial(".");
  }

  printSerialln();
  printSerialln("WiFi connected");

  uint8_t* currentBSSID = WiFi.BSSID();
  for (int j = 0; j < 6; j++)
    BSSID[j] = currentBSSID[j];

  printWiFi(printSerialln);
}

void checkBSSID()
{
  int j;

  uint8_t* currentBSSID = WiFi.BSSID();
  for (j = 0; j < 6 && BSSID[j] == currentBSSID[j]; j++)
    ;

  if (j < 6)
  {
    printSerialln("BSSID changed");
    wifiBegin();
  }
}

#endif

void setup()
{
# if defined LOGGING
    for (int i = 0; i < maxNLogs; i++)
      logs[i][0] = logs[i][maxLogSize - 1] = 0;
# endif

  SetupReset();

# if defined ESP32_DEVKIT_V1
    Serial.begin(115200);
# else
    Serial.begin(9600);
# endif

#if defined SERIALBT
    SerialBT.begin(myName); //Bluetooth device name
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
  printSerialln((char*)WIFISSID);

  WiFi.mode(WIFI_STA);

  wifiBegin();

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
    if (Ethernet.hardwareStatus() == EthernetNoHardware)
    {
      printSerialln("Ethernet shield was not found.  Sorry, can't run without hardware. :(");
      while (true)
      {
        delay(1); // do nothing, no point running without Ethernet hardware
      }
    }
    if (Ethernet.linkStatus() == LinkOFF)
    {
      printSerialln("Ethernet cable is not connected.");
    }
    // try to configure using IP address instead of DHCP:
    Ethernet.begin(mac, ip, myDns);
    printSerial("My IP address: ");

    IPAddress ip = Ethernet.localIP();

    char sip[16];
    sprintf(sip, "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);

    printSerialln(sip);
  } else
  {
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

  mqttClient.setServer(MQTTHOST, MQTTPORT);
  mqttClient.setKeepAlive(60);
  mqttClient.setCallback(callback);

#if defined OTA
  setupOTA();
#endif

  mytime = millis();

  reset = true;
  started = true;
  nFailedCodes = 0;
}

void loop()
{
    reconnect();
    if (mqttClient.connected())
    {
      statusGarages();
      mqttClient.loop();
    }

#if defined OTA
    server.handleClient();
#endif

    if (millis() - mytime > 1000) // every second
    {
      mytime = millis();

      if (started)
      {
        started = false;
        message("Started");

        uptimeDays = uptimeHours = uptimeMinutes = uptimeSeconds = 0;
      }

      if (++uptimeSeconds == 60)
      {
        uptimeSeconds = 0;
        if (++uptimeMinutes == 60)
        {
          uptimeMinutes = 0;
          if (++uptimeHours == 24)
          {
            uptimeHours = 0;
            uptimeDays++;
          }
        }
      }

      if (uptimeSeconds == 0)
        checkBSSID();

      char buf[50];

      sprintf(buf, "%u:%02d:%02d:%02d", uptimeDays, (int)uptimeHours, (int)uptimeMinutes, (int)uptimeSeconds);
      //printSerial("Uptime: ");
      //printSerialln(buf);
      if (mqttClient.connected())
        mqttClient.publish(MQTTid "/UpTime", buf, true);
      else
      {
        printSerialln("MQTT not connected...");
        printSerial("Uptime: ");
        printSerialln(buf);
      }
    }

    clearMessageWhenNeeded();
}
