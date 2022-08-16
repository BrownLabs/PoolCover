/*
  Hardware Wiring

  Node MCU esp-12e  --> 2 Relay Module

  3.3v -->  VCC    (Relay Jumper needs to short VCC - VCC)
  D3   -->  IN1
  D1   -->  IN2
  GND  -->  GND

  2 Relay Module --> Switch
  GND  -->  Middle Pole (x2)
  Default Open Relay IN1  -->  Closed
  Default Open Relay IN2  -->  Open

 
  ===================================================================================
    Description:
  ===================================================================================
    Double pole switching relay for remotely controlling automatic pool cover.
    An automatic pool cover has a double pole, single throw switch that has to be
    located near the equipment by the pool. This sketch will allow for remote
    control of that double pole switch, thereby allowing me to open or close the
    pool cover from a more convenient location.

    This is not a substitute for, or a way around, safety.  Never open or close a
    pool cover if you are not able to visually confirm that it is safe to do so.

    The opening and closing function is auto stopped after a set period of time.
    In my case, 49 seconds for closing and 28.5 seconds for opening. For additional
    safety, the movement is also stopped if the device loses WiFi connection.

    If you use this sketch, you need to set your own WiFi SID/Password and set the
    timing to auto stop the opening or closing.

    The movement can also be stopped manually, but I thought it was reckless to
    not have an auto stopping function since many things can go wrong and damamge
    to the cover motor could occur if it continues to run at limits.

    This sketch could be altered to control anything with a DPST switch.  Wiring
    the relays into your switch is your own responsibility.  I did so to perserve 
    the manual switch function while adding this remote control function.

    This sketch also uses Over The Air OTA for programming so that the sketch can
    be updated without physically connecting to it.
  ===================================================================================

*/
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <TLog.h>
#include <ArduinoOTA.h>
#include <arduino-timer.h>
#include <ESPDateTime.h>


/*
  ===================================================================================
    Constants (Enter your own values for the fields below.):
  ===================================================================================
 
    SYSLOG_HOST       - setup a syslog server to receive log messages in a central
                        location when the device is not physically connected to
                        your worksatation

    STASSID           - your wifi ssid

    STAPSK            - your wifi password

    CUSTOM_HOST_NAME  - to make the device easy to locate on your network/router

    OTA_HOSTNAME      - the name of the device that the Arduino IDE uses as a
                        network port when uploading the sketch over the air
                        (the sketch must be uploaded via cable the first time)

    OTA_PASSWORD      - set a password to use so others can't upload sketches to
                        your device.

    REMOTE_HTTP_PATH  - offload advanced styling and favicon.ico files to a remote
                        web server. This is optional so you can style the UI to
                        meet your client device needs. Functionallity will not be
                        affected if this is missing or incorrect. You should omit
                        any trialing slash.

    RUNTIME_TO_CLOSE  - The number of milliseconds to run the motor when closing
    
    RUNTIME_TO_OPEN   - The number of milliseconds to run the motor when opening
    
    VERSION           - So you can easily tell if your new sketch is loaded
  ===================================================================================
*/
#define SYSLOG_HOST "192.168.1.100" 
#define STASSID "*********************"
#define STAPSK  "*************************"
#define CUSTOM_HOSTNAME "yourhostname"
#define OTA_HOSTNAME "yourhostname"
#define OTA_PASSWORD "yourOTApassword"
#define REMOTE_HTTP_PATH "https://yourwebsite.com/somepath"

// times in milliseconds
const int RUNTIME_TO_CLOSE = 49000;  //49 second runtime for closing (slower than opening)
const int RUNTIME_TO_OPEN = 28500;   //28.5 second runtime for opening

char VERSION[10] = "1.00";




// Only send it to syslog if we have a host defined.
#ifdef SYSLOG_HOST
#include <SyslogStream.h>
SyslogStream syslogStream = SyslogStream();
#endif

ESP8266WebServer server(80);


auto timer = timer_create_default(); // create a timer with default settings

const int OPEN_PIN = 5;  // D1 the Arduino pin, which connects to the IN1 pin of relay
const int CLOSE_PIN = 0;  // D3 the Arduino pin, which connects to the IN2 pin of relay

const int OFF = HIGH;
const int ON = LOW;

const char *ssid = STASSID;
const char *password = STAPSK;

char startdatetime[22];
char coverStatus[10];
int state = 0;
int runtime = 0;
int starttime = 0;
unsigned long hits = 0L;

unsigned long currentMillis = 0;
unsigned long previousMillis = 0;

void handleRoot() {
  char temp[1800];
  char msg[60];

  hits++;
  snprintf(temp, 1800,
     "<html>\
      <head>\
        <meta http-equiv='refresh' content='3'/>\
        <meta name='viewport' content='width=device-width, initial-scale=1.0'>\
        <link rel='icon' href='%s/favicon.ico' sizes='any'>\
        <title>Pool Cover</title>\
        <style>\
           body { background-color: #212121; font-family: Arial, Helvetica, Sans-Serif; color: #fff; font-size: 2.2em;} a { color: ffccbb; }\
        </style>\
        <link rel='stylesheet' href='%s/poolcover.css'>\
      </head>\
      <body>\
        <div id='header'>Remote Pool Cover</div>\
        <div id='statuslabel'>Cover is currently <span class='status%s'>%s</span></div>\
        <div id='openlabel' class='button'><a href='/open'>Open Cover</a></div>\
        <div id='stoplabel' class='button'><a href='/stop'>Stop</a></div>\
        <div id='closelabel' class='button'><a href='/close'>Close Cover</a></div>\
        <div id='versionlabel'>Version %s loaded %ld times since %s</div>\
      </body>\
    </html>",
    REMOTE_HTTP_PATH, REMOTE_HTTP_PATH, coverStatus, coverStatus, VERSION, hits, startdatetime
  );

  server.send(200, "text/html", temp);
  sprintf(msg, "Homepage (v %s) loaded %lu times", VERSION, hits);
  Log.println(msg);
  
}

void handleNotFound() {
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";

  for (uint8_t i = 0; i < server.args(); i++) {
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }

  server.send(404, "text/plain", message);
}

void setup() {
  Serial.begin(115200);

  // initialize digital pin as an output.
  pinMode(OPEN_PIN, OUTPUT);
  pinMode(CLOSE_PIN, OUTPUT);

  stop(); //ensure any movement is stopped by resetting the relays

  WiFi.mode(WIFI_STA);
  WiFi.setHostname(CUSTOM_HOSTNAME);
  wifi_set_sleep_type(NONE_SLEEP_T);  //required to prevent esp from sleeping and going offline
  WiFi.setSleep(false);               //required to prevent esp from sleeping and going offline
  WiFi.begin(ssid, password);
  wifi_set_sleep_type(NONE_SLEEP_T);  //required to prevent esp from sleeping and going offline
  WiFi.setSleep(false);               //required to prevent esp from sleeping and going offline

  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Log.println("Connection Failed! Rebooting...");
    delay(10000);
    ESP.restart();
  }

  #ifdef SYSLOG_HOST
    syslogStream.setDestination(SYSLOG_HOST);
    syslogStream.setRaw(true); // wether or not the syslog server is a modern(ish) unix.
    #ifdef SYSLOG_PORT
      syslogStream.setPort(SYSLOG_PORT);
    #endif
  
    const std::shared_ptr<LOGBase> syslogStreamPtr = std::make_shared<SyslogStream>(syslogStream);
    Log.addPrintStream(syslogStreamPtr);
  #endif

  Log.begin();

  ArduinoOTA.setHostname(OTA_HOSTNAME);
  ArduinoOTA.setPassword(OTA_PASSWORD);
  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else { // U_FS
      type = "filesystem";
    }

    // NOTE: if updating FS this would be the place to unmount FS using FS.end()
    Log.println("Start updating " + type);
  });
  ArduinoOTA.onEnd([]() {
    Log.println("Upload complete.");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Log.printf("Progress: %u%%\n", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Log.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) {
      Log.println("Auth Failed");
    } else if (error == OTA_BEGIN_ERROR) {
      Log.println("Begin Failed");
    } else if (error == OTA_CONNECT_ERROR) {
      Log.println("Connect Failed");
    } else if (error == OTA_RECEIVE_ERROR) {
      Log.println("Receive Failed");
    } else if (error == OTA_END_ERROR) {
      Log.println("End Failed");
    }
  });
  ArduinoOTA.begin();

  Log.print("Connected to Wifi: ");
  Log.println(ssid);
  Log.print("IP address: ");
  Log.println(WiFi.localIP());

  server.enableCORS(true);

  server.on("/", handleRoot);
  server.on("/inline", []() {
    server.send(200, "text/plain", "this works as well");
  });

  server.on(F("/stop"), HTTP_GET, stop);
  server.on(F("/open"), HTTP_GET, open);
  server.on(F("/close"), HTTP_GET, close);

  server.onNotFound(handleNotFound);
  server.begin();

  setupDateTime();
  strcpy(startdatetime, DateTime.format(DateFormatter::SIMPLE).c_str());

  Log.println("HTTP server started");


}

// the loop function runs over and over again forever
void loop() {
  char workingMsg[50]; 
  
  ArduinoOTA.handle();
  server.handleClient();
  timer.tick(); // tick the timer

  // state != to 0 then the cover is moving
    // if WiFi is down, stop the movment and try reconnecting.
  if (state != 0) {
    currentMillis = millis();
    if (currentMillis - previousMillis >= 2000) {
      if ((WiFi.status() != WL_CONNECTED)) {
        Log.println("Lost WiFi connection while working, stopping any movement");
        stop();
        Log.println("Reconnecting to WiFi...");
        WiFi.disconnect();
        WiFi.reconnect();
//      } else {
//        sprintf(workingMsg, "Good WiFi connection while %s.", coverStatus);
//        Log.println(workingMsg);
      }
      previousMillis = currentMillis;
    }
  }
}

bool auto_stop_close(void *) {
  stop();
  return false; // don't keep the timer active.
}

bool auto_stop_open(void *) {
  stop();
  return false; // don't keep the timer active.
}

void stop() {
  Log.println(F("Stopped"));
  digitalWrite (OPEN_PIN, OFF);
  digitalWrite (CLOSE_PIN, OFF);
  state = 0;
  snprintf (coverStatus, 10, "stopped");
  replyMsg();
  timer.cancel(); // cancel any exiting timer
}

void open() {
  Log.println(F("Opening"));
  digitalWrite (OPEN_PIN, ON);
  digitalWrite (CLOSE_PIN, OFF);
  state = 1;
  snprintf (coverStatus, 10, "opening");
  replyMsg();
  timer.cancel(); // cancel any exiting timer
  timer.in(RUNTIME_TO_OPEN, auto_stop_open);
}

void close() {
  Log.println(F("Closing"));
  digitalWrite (OPEN_PIN, OFF);
  digitalWrite (CLOSE_PIN, ON);
  state = -1;
  snprintf (coverStatus, 10, "closing");
  replyMsg();
  timer.cancel(); // cancel any exiting timer
  timer.in(RUNTIME_TO_CLOSE, auto_stop_close);
}

void replyMsg () {
  char temp[1800];

  snprintf(temp, 1800,
    "<html>\
    <head>\
    <meta http-equiv='refresh' content='1;url=/' />\
        <meta name='viewport' content='width=device-width, initial-scale=1.0'>\
        <link rel='icon' href='%s/favicon.ico' sizes='any'>\
        <title>Pool Cover</title>\
        <style>\
           body { background-color: #212121; font-family: Arial, Helvetica, Sans-Serif; color: #fff; font-size: 2.2em;} a { color: ffccbb; }\
        </style>\
        <link rel='stylesheet' href='%s/poolcover.css'>\
        <title>$s pool cover</title>\
    </head>\
    <body>\
    %s\
    </body>\
    </html>",
    REMOTE_HTTP_PATH, REMOTE_HTTP_PATH, coverStatus, coverStatus
    );
  server.send(200, "text/html", temp);
}

void setupDateTime() {
  // source: https://github.com/mcxiaoke/ESPDateTime
  DateTime.setTimeZone("CST6CDT,M3.2.0,M11.1.0"); //Chicago
  DateTime.begin(/* timeout param */);
  if (!DateTime.isTimeValid()) {
    Log.println("Failed to get time from server.");
  }
}
