/*
2DO
-connect to WIFI only when email is being sent
  - this means we can't have a running web server
  - we can add a push button to force an info email

*/

#include <Arduino.h>
//#include <WiFi.h>
#include <EEPROM.h>
#include <WiFiManager.h>      // Captive Portal Wifi Manager - https://github.com/tzapu/WiFiManager
#include <HTTPClient.h>       // Simple Web Server
#include <LinkedList.h>       // Linked List object
#include <AsyncTCP.h>         // Low level networking
#include <ESPAsyncWebSrv.h>   // Simple Web Server
#include <ESPmDNS.h>          // Broadcast DNS entry on local network
#include "time.h"             // Manage time data type
#include "sntp.h"             // Simple Network Time Protocal
#include <ESP_Mail_Client.h>  // Email client

// --------------------------------------------------------------------------------------
// Task config
// --------------------------------------------------------------------------------------

// Task for reading sensor
void taskReadSensor(void *pvParameters);

// Task to process sensor value
void taskProcessSensor(void *pvParameters);

// Define Queue handle - Queue is used to send messages from taskReadSensor() to taskProcessSensor()
QueueHandle_t queueHandle;
const int queueElementSize = 10;  // max number of messages in the queue

// Define the contents of the message
typedef struct {
  char sensorStatus;   // sensor reading (1 on; 0 off)
  struct tm timeInfo;  // timestamp of when the sensor was read
} message_t;

// Linked list used to keep sensor history
GenericLinkedList<message_t> sensorHist = GenericLinkedList<message_t>();
const int sensorHistSize = 100;  // Number of sensor readings to keep in history. Note: The history will be gone on reboot or power loss unless it is saved to SD card.

// Pointers to task instance; not used in this code
TaskHandle_t taskReadSensorHandler;
TaskHandle_t taskProcessSensorHandler;


// --------------------------------------------------------------------------------------
// Wifi Manager config
// --------------------------------------------------------------------------------------

WiFiManager wm;
unsigned int startTime = millis();
bool portalRunning = false;
bool startAP = true;  // start AP and webserver if true, else start only webserver
bool forcePortalRun = false;
bool forcePortalStop = false;
bool forceServerConnect = false;  // force connecting to sump pump information server
bool forceServerDisconnect = false;

struct CustomWiFiParameters {
  char gmail_account[101];              // extra char is for \0
  char gmail_application_password[33];  // extra char is for \0
} wifiParams;
WiFiManagerParameter param_gmail_account("gmail_account", "gmail account", "?", 1);
WiFiManagerParameter param_gmail_application_password("gmail_application_password", "gmail application password", "?", 1);

String prevWLStatusString = "initialize";  //previous WiFi status
String WLStatusString = "initialize";      //current WiFi status

// Callback function to save custom parameters from captive portal (WiFi manager)
void saveParamsCallback() {
  Serial.print("param_gmail_account: ");
  Serial.println(param_gmail_account.getValue());
  Serial.print("param_gmail_application_password: ");
  Serial.println(param_gmail_application_password.getValue());

  if (strcmp(wifiParams.gmail_application_password, param_gmail_application_password.getValue()) != 0 || strcmp(wifiParams.gmail_account, param_gmail_account.getValue()) != 0) {
    strncpy(wifiParams.gmail_application_password, param_gmail_application_password.getValue(), 101);
    strncpy(wifiParams.gmail_account, param_gmail_account.getValue(), 33);
    wifiParams.gmail_account[100] = '\0';
    wifiParams.gmail_application_password[32] = '\0';

    Serial.print("wifiParams.gmail_account: ");
    Serial.println(wifiParams.gmail_account);
    Serial.print("wifiParams.gmail_application_password: ");
    Serial.println(wifiParams.gmail_application_password);

    EEPROM.put(0, wifiParams);
    if (EEPROM.commit()) {
      Serial.println("Settings saved");
    } else {
      Serial.println("EEPROM error");
    }
  }
}

// --------------------------------------------------------------------------------------
// Web Server config
// --------------------------------------------------------------------------------------

// Start web server on port 80
AsyncWebServer server(80);
const char *PARAM_MESSAGE = "message";  //2do - remove this if not needed; store message from GET request

void GenerateHtml(String &pStatus, String &pStatusTime, String &pInitialTime, String &pTable, long cntOnStatusDuration, long totOnStatusDuration, int maxOnStatusDuration, int minOnStatusDuration, int avgOnStatusDuration, long cntOffStatusDuration, long totOffStatusDuration, int maxOffStatusDuration, int minOffStatusDuration, int avgOffStatusDuration);  // function to generate HTML

String HtmlContent = "<HTML><BODY><CENTER><B>Not initialized. Try reloading.</B></CENTER></BODY></HTML>";  // contents of generated HTML (shared by multiple tasks)

// Function to display 404 not found message on invalid URL
void notFound(AsyncWebServerRequest *request) {
  request->send(404, "text/plain", "Not found");
}

// --------------------------------------------------------------------------------------
// Time config
// --------------------------------------------------------------------------------------

// Time server URL and setting
const char *ntpServer1 = "pool.ntp.org";
const char *ntpServer2 = "time.nist.gov";
const long gmtOffset_sec = -18000;
const int daylightOffset_sec = 3600;  // Set to 3600 for daylight savings time

// Function to print local time to serial port
void printLocalTime() {
  struct tm timeInfo;
  if (!getLocalTime(&timeInfo)) {
    Serial.println("Warning: No time available (yet)");
    return;
  }
  Serial.println(&timeInfo, "printLocalTime: %F %T");
}

// --------------------------------------------------------------------------------------
// Email config
// --------------------------------------------------------------------------------------

/** The smtp host name e.g. smtp.gmail.com for GMail or smtp.office365.com for Outlook or smtp.mail.yahoo.com */
#define SMTP_HOST "smtp.gmail.com"

/** The smtp port e.g.
 * 25  or esp_mail_smtp_port_25
 * 465 or esp_mail_smtp_port_465
 * 587 or esp_mail_smtp_port_587
 */
#define SMTP_PORT esp_mail_smtp_port_465


/* Declare the global used SMTPSession object for SMTP transport */
SMTPSession smtp;

// flag to trigger emai being sent (0 no; 1 yes)
int sendEmailFlag = 0;
int sendEmailType = 0;  //(1 ON; 2 OFF; 3 INFO)

const unsigned long emailSummaryInterval = 1UL * 24UL * 60UL * 60UL * 1000UL;  // send INFO email every 1 days
//const unsigned long emailSummaryInterval = 1UL * 60UL * 1000UL; // send INFO email every 1 min
unsigned long emailSummaryPrevMillis = 0;
unsigned long emailSummaryPrevMillisRollover = 0;

/* Callback function to get the Email sending status */
void smtpCallback(SMTP_Status status);
void sendMail();


// --------------------------------------------------------------------------------------
// General config
// --------------------------------------------------------------------------------------

char stringBuffer[100];          // generic string buffer
unsigned long lastTime = 0;      // The last time value recorded
int first_call_to_loop_ind = 1;  // flag to keep track of first call to loop() function
struct tm initialTimeInfo;       // timestamp of initial sensor reading
int initialTimeInfoSet = 0;      //flag to check if initialTimeInfo is set
long curStatusDuration = 0;      //keep track of how long a status is the same
int raiseAlarm = 0;              // flag used to turn alarm ON/OFF

#define timerDelay 60000           // Set timer interval; 1 second = 1000
#define sensorOnLimit 60           // max time in seconds that a sensor can be On before sending a notification email (set to 60)
#define sensorOnRepeatLimit 3600   // max time in seconds before repeating sending a notification email (set to 3600)
#define sensorOffLimit 7200        // max time in seconds that a sensor can be Off before sending a notification email
#define sensorOffRepeatLimit 3600  // max time in seconds before repeating sending a notification email (set to 3600)
#define SENSOR_PIN 34              //pin to read sensor value
#define LED_PIN 0                  //pin to display LED (green LED)
#define ALARM_PIN 4                //pin to raise alarm (red LED)
#define RESET_BUTTON_PIN 35        //pin to reset WiFi credentials and enter captive portal mode
#define WIFI_STATUS_PIN 15         //pin to show when WiFi is not connected (yellow LED)
#define SENSOR_LOW_THRESHOLD 150   // max value of sensor pin considered LOW (OFF)
// --------------------------------------------------------------------------------------
// Setup - put your setup code here, to run once
// --------------------------------------------------------------------------------------

void setup() {

  //set the resolution to 10 bits (0-1024)
  analogReadResolution(10);
  pinMode(SENSOR_PIN, INPUT);
  pinMode(LED_PIN, OUTPUT);
  pinMode(ALARM_PIN, OUTPUT);
  pinMode(RESET_BUTTON_PIN, INPUT);
  pinMode(WIFI_STATUS_PIN, OUTPUT);

  WiFi.mode(WIFI_STA);  // explicitly set mode, esp defaults to STA+AP

  Serial.begin(115200);           // set baud rate for serial port
  while (!Serial) { delay(10); }  // wait for serial communication to begin

  // Create the queue which will have <queueElementSize> number of elements, each of size `message_t` and pass the address to <queueHandle>.
  queueHandle = xQueueCreate(queueElementSize, sizeof(message_t));

  // Check if the queue was successfully created
  if (queueHandle == NULL) {
    Serial.println("Fatal error; queue could not be created.");
    while (1) delay(1000);  // At this point as is not possible to continue
  }

  // --------------------------------------------------------------------------------------
  // Begin email configuration
  // --------------------------------------------------------------------------------------

  /*  Set the network reconnection option */
  MailClient.networkReconnect(true);
  /** Enable the debug via Serial port
    * 0 for no debugging
    * 1 for basic level debugging
    *
    * Debug port can be changed via ESP_MAIL_DEFAULT_DEBUG_PORT in ESP_Mail_FS.h
    */
  smtp.debug(1);

  /* Set the callback function to get the sending results */
  smtp.callback(smtpCallback);

  // --------------------------------------------------------------------------------------
  // End email configuration
  // --------------------------------------------------------------------------------------


  /* 
    Set up two tasks to run independently. One for reading the sensor, and one for processing the sensor values.
    There are two tasks because we don't want to block reading the sensor if something goes wrong with the processing.
    */

  xTaskCreatePinnedToCore(
    taskProcessSensor, "Task process sensor value"  // A name for the task just for humans
    ,
    4096  // The stack size can be checked by calling `uxHighWaterMark = uxTaskGetStackHighWaterMark(NULL);`
    ,
    NULL  // No parameter is used
    ,
    2  // Priority, with 3 (configMAX_PRIORITIES - 1) being the highest, and 0 being the lowest.
    ,
    &taskProcessSensorHandler  // Task handle is not used in this program
    ,
    1);

  xTaskCreatePinnedToCore(
    taskReadSensor, "Task Read sensor value", 2048  // Stack size
    ,
    NULL  // No parameter is used
    ,
    1  // Priority
    ,
    &taskReadSensorHandler  // Task handle is not used in this program
    ,
    0);

  // Now the task scheduler, which takes over control of scheduling individual tasks, is automatically started.

  //2DO - verify that the sensor is being read when the WiFi manager is in AP mode

  //Note: reset settings - wipe credentials for testing
  //wm.resetSettings();

  // read config values from EEPROM
  EEPROM.begin(256);
  EEPROM.get(0, wifiParams);

  // if they have not been set before then initialize to \0
  if (isAlphaNumeric(wifiParams.gmail_account[0])) {
    wifiParams.gmail_account[100] = '\0';
  } else {
    forcePortalRun = true;
    for (int i; i <= 100; i++) {
      wifiParams.gmail_account[i] = '\0';
    }
  }

  if (isAlphaNumeric(wifiParams.gmail_application_password[0])) {
    wifiParams.gmail_application_password[32] = '\0';
  } else {
    forcePortalRun = true;
    for (int i; i <= 32; i++) {
      wifiParams.gmail_application_password[i] = '\0';
    }
  }

  Serial.print("setup wifiParams.gmail_account: ");
  Serial.println(wifiParams.gmail_account);
  Serial.print("setup wifiParams.gmail_application_password: ");
  Serial.println(wifiParams.gmail_application_password);

  // set the values to whatever was saved in EEPROM
  // name, prompt, default, length
  param_gmail_account.setValue(wifiParams.gmail_account, 101);
  param_gmail_application_password.setValue(wifiParams.gmail_application_password, 33);

  wm.addParameter(&param_gmail_account);
  wm.addParameter(&param_gmail_application_password);

  // wait 10 mintues for credentials before reboot
  wm.setConfigPortalTimeout(60);  //600

  wm.setConfigPortalBlocking(false);
  wm.setSaveParamsCallback(saveParamsCallback);  // register a callback function with WiFi manager to save custom parameters to flash memory
  wm.setClass("invert");                         // set dark theme (because it looks cooler)
  wm.setScanDispPerc(true);                      // display signal strength as percentage

  // automatically connect using saved credentials if they exist; if connection fails it starts an access point with the specified name
  if (wm.autoConnect("Sensor Monitor")) {
    Serial.println("Info: Connected to WiFi.");
  } else {
    Serial.println("Info: Not connected to WiFi; running configportal access point.");
  }
}


// --------------------------------------------------------------------------------------
// Main loop
// --------------------------------------------------------------------------------------

void loop() {

  // do this on the first call to loop()
  if (first_call_to_loop_ind == 1) {
    Serial.print("Info: loop running on core: ");
    Serial.println(xPortGetCoreID());
    first_call_to_loop_ind = 0;
  }

  //doWiFiManager();// non-blocking call to WiFi manager
  wm.process();

  // set current and previous WiFi status
  prevWLStatusString = WLStatusString;
  WLStatusString = wm.getWLStatusString();

  // if WiFi connetion is first connected, or lost and re-connected do this
  /* DEBUG
  if(Serial.available()>0){
    char input_chr=Serial.read();
    Serial.print("prevWLStatusString:");     Serial.println(prevWLStatusString);
    Serial.print("WLStatusString:");     Serial.println(WLStatusString);
    Serial.print("portalRunning:");    Serial.println(portalRunning);
    Serial.println("forceServerConnect"); Serial.println(forceServerConnect);
    Serial.println("forceServerDisconnect"); Serial.println(forceServerDisconnect);
    Serial.println("forcePortalRun"); Serial.println(forcePortalRun);
    Serial.println("forcePortalStop"); Serial.println(forcePortalStop);

    if(input_chr=='c'){
      forceServerConnect=true;
    }
    if(input_chr=='d'){
      forceServerDisconnect=true;
    }
    if(input_chr=='p'){
      forcePortalRun=true;
    }
    if(input_chr=='s'){
      forcePortalStop=true;
    }
    if(input_chr=='r'){
      wm.resetSettings();
      ESP.restart();
    }
    
  }
  DEBUG */

  // If the reset button has been pressed for 2 seconds then clear the WiFi settings and reboot ESP. This will force the captive portal to start.
  if (analogRead(RESET_BUTTON_PIN) > 1000) {
    Serial.println("Info: RESET BUTTON PRESSED");

    vTaskSuspend(taskReadSensorHandler);
    vTaskSuspend(taskProcessSensorHandler);

    // turn on all LEDs as a signal that the button has been pressed
    Serial.println("Info: Turn ON LED");

    digitalWrite(ALARM_PIN, HIGH);
    digitalWrite(LED_PIN, HIGH);
    digitalWrite(WIFI_STATUS_PIN, HIGH);

    //sleep(1000);

    Serial.println("Info: Turn OFF LED");

    digitalWrite(ALARM_PIN, LOW);
    digitalWrite(LED_PIN, LOW);
    digitalWrite(WIFI_STATUS_PIN, LOW);

    Serial.println("Info: Disconnect WiFi");
    wm.disconnect();
    Serial.println("Info: Reset WiFi");
    wm.resetSettings();
    Serial.println("Info: Restart ESP32");
    ESP.restart();
  }

  if (forceServerConnect || (WLStatusString == "WL_CONNECTED" && prevWLStatusString != "WL_CONNECTED")) {
    forceServerConnect = false;

    Serial.print("prevWLStatusString:");
    Serial.println(prevWLStatusString);

    // print IP address
    Serial.print("Info: IP Address: ");
    Serial.println(WiFi.localIP());

    // set mDNS to reference web server by name instead of IP (most routers will defailt to <dns_name>.local)
    if (!MDNS.begin("sumppumpmonitor")) {
      Serial.println("Error: Unable to start mDNS");
      return;
    } else {
      Serial.println("Info: Access on local network as http://sumppumpmonitor.local/");
    }

    // setup server
    MDNS.addService("http", "tcp", 80);

    /**
     * This will set configured ntp servers and constant TimeZone/daylightOffset
     * should be OK if your time zone does not need to adjust daylightOffset twice a year,
     * in such a case time adjustment won't be handled automatically.
    */
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer1, ntpServer2);
    printLocalTime();
    //wm.disconnect() 2DO - disconnect WiFi when not trying to send signal
    //wm.erase();

    // Set default page for Web Server
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
      request->send(200, "text/html", HtmlContent);
    });

    /**
    * This is not needed at this time, but I'm leaving it in for future use. If we need to collect user input for some reason. 
    *

    // Send a GET request to <IP>/get?message=<message>
    server.on("/get", HTTP_GET, [] (AsyncWebServerRequest *request) {
        String message;
        if (request->hasParam(PARAM_MESSAGE)) {
            message = request->getParam(PARAM_MESSAGE)->value();
        } else {
            message = "No message sent";
        }
        request->send(200, "text/plain", "Hello, GET: " + message);
    });
    

    // Send a POST request to <IP>/post with a form field message set to <message>
    server.on("/post", HTTP_POST, [](AsyncWebServerRequest *request){
        String message;
        if (request->hasParam(PARAM_MESSAGE, true)) {
            message = request->getParam(PARAM_MESSAGE, true)->value();
        } else {
            message = "No message sent";
        }
        request->send(200, "text/plain", "Hello, POST: " + message);
    });
    */


    server.onNotFound(notFound);
    server.begin();  // start the Web Server
  }
  // do this if WiFi connection is lost
  else if (forceServerDisconnect || (WLStatusString != "WL_CONNECTED" && prevWLStatusString == "WL_CONNECTED")) {
    forceServerDisconnect = false;
    server.end();  // end server
    MDNS.end();    // end mDNS service
  } else if (WLStatusString == "WL_IDLE_STATUS") {
    Serial.println("Stuck in WL_IDLE_STATUS so restarting ESP.");
    delay(1000);
    ESP.restart();
  } else if (wm.getConfigPortalActive() == false && WLStatusString == "WL_DISCONNECTED") {
    Serial.println("Stuck in WL_DISCONNECTED so restarting ESP.");
    delay(1000);
    ESP.restart();
  }




  // send status email
  if (WLStatusString == "WL_CONNECTED" && sendEmailFlag != 0) {
    sendEmailFlag = 0;

    // delay 1 second to allow for the queue to be processed so that the HTML is updated; this is a temp workaround
    delay(1000);

    if (sendEmailType == 1) {
      //Serial.println("SENDING ON EMAIL ******************************");
      sendEmail(sendEmailType);
    } else if (sendEmailType == 2) {
      //Serial.println("SENDING OFF EMAIL ******************************");
      sendEmail(sendEmailType);
    }
  }

  // send INFO email every ? days
  unsigned long currentMillis = millis();
  // detect rollover in millis() and calculate the offset
  if (currentMillis < emailSummaryPrevMillis) {
    emailSummaryPrevMillisRollover = emailSummaryPrevMillisRollover + (ULONG_MAX - emailSummaryPrevMillis) + 1;
  }
  emailSummaryPrevMillis = currentMillis;

  // get the new elapsed time
  unsigned long elapsedTime = currentMillis - emailSummaryPrevMillisRollover;

  // if elapsed time is greater than ? days then send email
  if (elapsedTime >= emailSummaryInterval) {
    emailSummaryPrevMillisRollover = currentMillis;
    //Serial.println("SENDING INFO EMAIL ******************************");
    sendEmail(3);
  }


  // turn ON the alarm for 5 seconds every 10 seconds
  if (raiseAlarm == 1 && currentMillis % 10000 < 5000) {
    digitalWrite(ALARM_PIN, HIGH);
  } else {
    digitalWrite(ALARM_PIN, LOW);
  }

  // blink yellow LED when WiFi is NOT connected
  if (WLStatusString == "WL_CONNECTED") {
    digitalWrite(WIFI_STATUS_PIN, HIGH);
  } else {
    digitalWrite(WIFI_STATUS_PIN, LOW);
  }
}

// Task to process sensor values
void taskProcessSensor(void *pvParameters) {
  bool debug_mode = false;  // flag to print extra information to the log

  message_t message;
  message_t prevMessage;
  message_t messagePlaceholder;

  // variables to keep track of sensor statistics
  long prevStatusDuration = 0;
  long cntOnStatusDuration = 0;
  long totOnStatusDuration = 0;
  int maxOnStatusDuration = 0;
  int minOnStatusDuration = 0;
  int avgOnStatusDuration = 0;
  long cntOffStatusDuration = 0;
  long totOffStatusDuration = 0;
  int maxOffStatusDuration = 0;
  int minOffStatusDuration = 0;
  int avgOffStatusDuration = 0;

  String pStatus = "...";
  String pStatusTime = "";
  String pInitialTime = "...";
  String pTable = "";


  Serial.print("Info: taskProcessSensor running on core: ");
  Serial.println(xPortGetCoreID());

  for (;;) {  // A Task shall never return or exit.

    // One approach would be to poll the function (uxQueueMessagesWaiting(QueueHandle) and call delay if nothing is waiting.
    // The other approach is to use infinite time to wait defined by constant `portMAX_DELAY`:
    if (queueHandle != NULL) {  // Sanity check just to make sure the queue actually exists
      int ret = xQueueReceive(queueHandle, &message, portMAX_DELAY);
      if (ret == pdPASS) {
        // The message was successfully received

        Serial.print("Received sensor reading: ");
        Serial.print(message.sensorStatus);
        Serial.println(&message.timeInfo, " Date: %B %d %Y %H:%M:%S");

        // If the linked list is full, then remove the oldest element.
        Serial.println("Task watermark: " + String(uxTaskGetStackHighWaterMark(NULL)) + " bytes");
        Serial.println("Free memory: " + String(esp_get_free_heap_size()) + " bytes");

        // only add to the history table if the status has changed from the previous status
        if (message.sensorStatus != prevMessage.sensorStatus || sensorHist.size() == 0) {

          // collect statistics
          if (sensorHist.size() > 0) {
            prevStatusDuration = round(difftime(mktime(&message.timeInfo), mktime(&prevMessage.timeInfo)));
            Serial.print("prevStatusDuration: ");
            Serial.println(prevStatusDuration);

            // There is a bug where the prevStatusDuration reading is abnormal (large negative value); discard those values from the statistics.
            if (prevStatusDuration < 0) {
              Serial.println("WARNING: Abnormal value detected.");
            } else if (prevMessage.sensorStatus == '1') {
              cntOnStatusDuration++;
              totOnStatusDuration += prevStatusDuration;
              avgOnStatusDuration = int(totOnStatusDuration / cntOnStatusDuration);
              if (prevStatusDuration >= maxOnStatusDuration) {
                maxOnStatusDuration = prevStatusDuration;
              }
              if (prevStatusDuration <= minOnStatusDuration || minOnStatusDuration == 0) {
                minOnStatusDuration = prevStatusDuration;
              }
            } else if (prevMessage.sensorStatus == '0') {
              cntOffStatusDuration++;
              totOffStatusDuration += prevStatusDuration;
              avgOffStatusDuration = int(totOffStatusDuration / cntOffStatusDuration);
              if (prevStatusDuration >= maxOffStatusDuration) {
                maxOffStatusDuration = prevStatusDuration;
              }
              if (prevStatusDuration <= minOffStatusDuration || minOffStatusDuration == 0) {
                minOffStatusDuration = prevStatusDuration;
              }
            }
          }  // collect statistics


          if (sensorHist.size() >= sensorHistSize) {
            sensorHist.shift();
            if (debug_mode) Serial.println("Info: Removed oldest element from history.");
            if (debug_mode) Serial.println("Free memory: " + String(esp_get_free_heap_size()) + " bytes");
          }

          // Add latest element
          sensorHist.add(message);
          if (debug_mode) Serial.println("Info: Added new element to history.");
        }

        // Print the history
        if (debug_mode) Serial.println("Debug: History... ");
        pTable = "";
        for (int i = 0; i < sensorHist.size(); i++) {
          messagePlaceholder = sensorHist.get(i);
          if (debug_mode) Serial.print("Status: ");
          if (debug_mode) Serial.print(messagePlaceholder.sensorStatus);
          if (debug_mode) Serial.println(&messagePlaceholder.timeInfo, " Date: %F %T");

          //build HTML table content
          strftime(stringBuffer, sizeof(stringBuffer), "%F %T", &messagePlaceholder.timeInfo);
          if (messagePlaceholder.sensorStatus == '1') {
            pTable = "<tr><td>" + String(stringBuffer) + "</td><td>On</td></tr>" + pTable;
          } else {
            pTable = "<tr><td>" + String(stringBuffer) + "</td><td>Off</td></tr>" + pTable;
          }
        }
        if (debug_mode) Serial.println(pTable);

        //refresh HTML content
        if (message.sensorStatus == '1') {
          pStatus = "On";
        } else {
          pStatus = "Off";
        }

        // get time of reading
        strftime(stringBuffer, sizeof(stringBuffer), "%F %T", &message.timeInfo);
        pStatusTime = String(stringBuffer);

        strftime(stringBuffer, sizeof(stringBuffer), "%F %T", &initialTimeInfo);
        pInitialTime = String(stringBuffer);
        if (debug_mode) Serial.println(pInitialTime);

        GenerateHtml(pStatus, pStatusTime, pInitialTime, pTable, cntOnStatusDuration, totOnStatusDuration, maxOnStatusDuration, minOnStatusDuration, avgOnStatusDuration, cntOffStatusDuration, totOffStatusDuration, maxOffStatusDuration, minOffStatusDuration, avgOffStatusDuration);

        // copy message to prevMessage
        // only if the status has changed from the previous status; otherwise it's a continuation of the same status
        if (message.sensorStatus != prevMessage.sensorStatus || (prevMessage.sensorStatus != '0' && prevMessage.sensorStatus != '1'))
          memcpy((void *)&prevMessage, (void *)&message, sizeof(message));

      } else if (ret == pdFALSE) {
        Serial.println("Error: Unable to receive data from the queue.");
      }
    }  // Sanity check

    delay(10);

  }  // Infinite loop
}

// Task to read sensor values
void taskReadSensor(void *pvParameters) {
  message_t message;                  // current message
  message_t prevMessage;              // previous message used to detect changes
  message_t firstMessage;             // first message for a new status, used to keep track of status duration
  message_t prevNotificationMessage;  // prevous time a notificaion was sent

  int randomNumber;             // random number
  char sensorReading;           // sensor value
  int emailSentFlag = 0;        //flag to track if an email has been sent (attempted to send)
  long tmpStatusDuration = 0;   //local variable to calculate status duration
  long tmpStatusDuration2 = 0;  //local variable to calculate status duration
  int forceQueueFlag = 0;       //flag to force sending a status to the queue even if the state hasn't changed.
  int analogVolts;              //voltage reading from sensor

  Serial.print("Info: taskReadSensor running on core: ");
  Serial.println(xPortGetCoreID());

  for (;;) {

    // display the core this task is running on
    // Serial.print("TaskReadSensor running on core: ");
    // Serial.println(xPortGetCoreID());

    /*
    // generate aa random sensor reading for the purpose of testing
    randomNumber=random(0,100);
    if(randomNumber<40){
      sensorReading = '1';
    }else{
      sensorReading = '0';
    }
    */

    // wait 1 second and get average of 10 sensor readings
    analogVolts = analogReadMilliVolts(SENSOR_PIN);
    for (int i = 0; i < 9; i++) {
      delay(100);
      analogVolts += analogReadMilliVolts(SENSOR_PIN);
    }
    analogVolts = analogVolts / 10;

    Serial.print("Sensor reading: ");
    Serial.println(analogVolts);
    if (analogVolts > SENSOR_LOW_THRESHOLD) {
      sensorReading = '1';
      digitalWrite(LED_PIN, HIGH);
    } else {
      sensorReading = '0';
      digitalWrite(LED_PIN, LOW);
    }

    // save sensor reading and date to message variable
    message.sensorStatus = sensorReading;

    if (initialTimeInfoSet == 0) {
      if (!getLocalTime(&initialTimeInfo)) {
        Serial.println("Warning: Unable to get time.");
        continue;
      }
      initialTimeInfoSet = 1;
    }
    if (!getLocalTime(&message.timeInfo)) {
      Serial.println("Warning: Unable to get time.");
      continue;
    }

    Serial.print("Sensor reading: ");
    Serial.print(sensorReading);
    Serial.println(&message.timeInfo, " Date: %F %T");
    Serial.print("WLStatusString:");
    Serial.println(WLStatusString);
    Serial.print("WiFi.getMode():");
    Serial.println(WiFi.getMode());



    // run some task once every timerDelay
    if ((millis() - lastTime) > timerDelay) {
      lastTime = millis();
      // force this entry to the queue
      forceQueueFlag = 1;
    }

    // if the sensor status remains unchanged
    if (message.sensorStatus == prevMessage.sensorStatus && sensorHist.size() > 0) {
      //how many seconds between the prev notification message and the current message
      tmpStatusDuration = round(difftime(mktime(&message.timeInfo), mktime(&prevNotificationMessage.timeInfo)));

      // The prevNotificationMessage should never be more recent than the first message, unless we're debugging. This check can be removed when the final code is released.
      tmpStatusDuration2 = round(difftime(mktime(&prevNotificationMessage.timeInfo), mktime(&firstMessage.timeInfo)));

      // flag to re-send email if it has been ON/OFF for too long and not currently already sending an email
      if (((message.sensorStatus == '1' && tmpStatusDuration > sensorOnRepeatLimit) || (message.sensorStatus == '0' && tmpStatusDuration > sensorOffRepeatLimit)) && emailSentFlag == 1 && tmpStatusDuration2 > 0 && sendEmailFlag == 0) {
        Serial.println("RESEND EMAIL ********************");
        Serial.println(&message.timeInfo, " Date: %F %T");
        Serial.println(&prevNotificationMessage.timeInfo, " Date: %F %T");
        Serial.println(&firstMessage.timeInfo, " Date: %F %T");
        Serial.print("tmpStatusDuration ");
        Serial.println(tmpStatusDuration);
        Serial.print("tmpStatusDuration2 ");
        Serial.println(tmpStatusDuration2);
        Serial.print("sensorOnRepeatLimit ");
        Serial.println(sensorOnRepeatLimit);

        // reset flag to allow for re-sending the email
        emailSentFlag = 0;
      }
    }

    // track how long a status has not changed; if too long then send email
    if (message.sensorStatus == prevMessage.sensorStatus && emailSentFlag == 0 && sensorHist.size() > 0) {
      //how many seconds between the first message and the current message
      tmpStatusDuration = round(difftime(mktime(&message.timeInfo), mktime(&firstMessage.timeInfo)));
      Serial.print("tmpStatusDuration: ");
      Serial.println(tmpStatusDuration);

      Serial.print("message.sensorStatus:");
      Serial.println(message.sensorStatus);

      Serial.print("sensorOnLimit:");
      Serial.println(sensorOnLimit);

      Serial.print("sensorOffLimit:");
      Serial.println(sensorOffLimit);

      Serial.print("sendEmailFlag:");
      Serial.println(sendEmailFlag);

      // send email if it has been ON for too long
      if (message.sensorStatus == '1' && tmpStatusDuration > sensorOnLimit && sendEmailFlag == 0) {
        curStatusDuration = tmpStatusDuration;
        Serial.print("curStatusDuration: ");
        Serial.println(curStatusDuration);
        // set flag to send email
        sendEmailFlag = 1;
        sendEmailType = 1;  // ON email

        // set flag to mark that email has been sent
        emailSentFlag = 1;

        // mark when the notification is being sent so that we can re-send it periodically
        memcpy(&prevNotificationMessage, &message, sizeof(message));

        // turn ON alarm
        raiseAlarm = 1;

        // force this entry to the queue
        forceQueueFlag = 1;
      }
      // send email if it has been OFF for too long
      else if (message.sensorStatus == '0' && tmpStatusDuration > sensorOffLimit && sendEmailFlag == 0) {
        curStatusDuration = tmpStatusDuration;
        Serial.print("curStatusDuration: ");
        Serial.println(curStatusDuration);
        // set flag to send email
        sendEmailFlag = 1;
        sendEmailType = 2;  // OFF email

        // set flag to mark that email has been sent
        emailSentFlag = 1;

        // mark when the notification is being sent so that we can re-send it periodically
        memcpy(&prevNotificationMessage, &message, sizeof(message));

        // turn ON alarm
        raiseAlarm = 1;

        // force this entry to the queue
        forceQueueFlag = 1;
      }
    }

    // Send the message to the process task;
    // check if the queue exists AND if there is any free space in the queue.
    Serial.println("Info: Number of elements in queue is: " + String(uxQueueMessagesWaiting(queueHandle)));
    if (queueHandle != NULL && uxQueueSpacesAvailable(queueHandle) > 0) {
      // do this if there was a change detected between the current reading and the previous reading
      if (message.sensorStatus != prevMessage.sensorStatus || forceQueueFlag == 1) {
        // The message needs to be passed as pointer to void.
        // The last parameter states how many milliseconds should wait (keep trying to send) if is not possible to send right away.
        // When the wait parameter is 0 it will not wait and if the send is not possible the function will return errQUEUE_FULL
        // Note: The items are queued by copy, not by reference, so free the buffer after use (if needed).
        int ret = xQueueSend(queueHandle, (void *)&message, 0);
        Serial.println("Info: Message sent to queue.");
        if (ret == pdTRUE) {
          // The message was successfully sent.
        } else if (ret == errQUEUE_FULL) {
          // Since we are checking uxQueueSpacesAvailable this should not occur.
          Serial.println("Warning: Unable to send data into the queue.");
        }  // Queue send check

        forceQueueFlag = 0;  //reset flag

      }  // sensor status changed
    }    // Queue sanity check
    else {
      Serial.println("Warning: Unable to send data into the queue because it is full. Number of elements is: " + String(uxQueueMessagesWaiting(queueHandle)));
    }

    // reset email sent flag when the status changes
    if (message.sensorStatus != prevMessage.sensorStatus) {
      // reset flag to allow for sending emails
      emailSentFlag = 0;
      // copy message to firstMessage
      memcpy(&firstMessage, &message, sizeof(message));

      // turn OFF alarm (if it was ON)
      raiseAlarm = 0;
    }

    // copy message to prevMessage
    memcpy((void *)&prevMessage, (void *)&message, sizeof(message));

  }  // Infinite loop
}


void sendEmail(int pSendEmailType) {

  /* Declare the Session_Config for user defined session credentials */
  Session_Config config;
  String msg_subject;
  String htmlMsg;

  /*
  // automatically connect using saved credentials if they exist; if connection fails it starts an access point with the specified name
  if(wm.autoConnect("Sensor Monitor")){
      Serial.println("Info: Connected to WiFi.");
  }
  else {
    Serial.println("Info: Not connected to WiFi; running configportal access point.");
  }
  */

  /* Set the session config */
  config.server.host_name = SMTP_HOST;
  config.server.port = SMTP_PORT;
  //config.login.email = AUTHOR_EMAIL;
  //config.login.password = AUTHOR_PASSWORD;
  config.login.email = String(wifiParams.gmail_account) + "@gmail.com";
  config.login.password = wifiParams.gmail_application_password;


  /** Assign your host name or you public IPv4 or IPv6 only
  * as this is the part of EHLO/HELO command to identify the client system
  * to prevent connection rejection.
  * If host name or public IP is not available, ignore this or
  * use loopback address "127.0.0.1".
  *
  * Assign any text to this option may cause the connection rejection.
  */
  config.login.user_domain = F("127.0.0.1");

  /*
  Set the NTP config time
  For times east of the Prime Meridian use 0-12
  For times west of the Prime Meridian add 12 to the offset.
  Ex. American/Denver GMT would be -6. 6 + 12 = 18
  See https://en.wikipedia.org/wiki/Time_zone for a list of the GMT/UTC timezone offsets
  */
  config.time.ntp_server = F("pool.ntp.org,time.nist.gov");
  config.time.gmt_offset = -9;
  config.time.day_light_offset = 1;

  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer1, ntpServer2);

  /* Declare the message class */
  SMTP_Message message;

  /* Set the message headers */
  message.sender.name = F("Sump Pump Monitor");
  //message.sender.email = AUTHOR_EMAIL;
  message.sender.email = String(wifiParams.gmail_account) + "@gmail.com";

  if (pSendEmailType == 1) {
    msg_subject = "ALERT: sump pump ON for " + String(curStatusDuration) + " seconds.";
    htmlMsg = "<p>The sump pump has been running for over <span style=\"color:#ff0000;\">" + String(round(sensorOnLimit / 60)) + "</span> minutes.</p>";
  }
  if (pSendEmailType == 2) {
    msg_subject = "ALERT: sump pump OFF for " + String(curStatusDuration) + " seconds.";
    htmlMsg = "<p>The sump pump has been OFF for over <span style=\"color:#ff0000;\">" + String(round(sensorOffLimit / 60)) + "</span> minutes.</p>";
  }
  if (pSendEmailType == 3) {
    msg_subject = "INFO: summary of sump pump usage";
    htmlMsg = "<p>Here is an updated summary of recent sump pump usage.</p>";
  }

  //message.subject = F(msg_subject);
  message.subject = msg_subject;
  message.addRecipient(F("recipient"), String(wifiParams.gmail_account) + "@gmail.com");

  //message.html.content = htmlMsg;
  message.html.content = HtmlContent;

  /** The html text message character set e.g.
   * us-ascii
   * utf-8
   * utf-7
   * The default value is utf-8
   */
  message.html.charSet = F("us-ascii");

  /** The content transfer encoding e.g.
   * enc_7bit or "7bit" (not encoded)
   * enc_qp or "quoted-printable" (encoded)
   * enc_base64 or "base64" (encoded)
   * enc_binary or "binary" (not encoded)
   * enc_8bit or "8bit" (not encoded)
   * The default value is "7bit"
   */
  message.html.transfer_encoding = Content_Transfer_Encoding::enc_7bit;

  /** The message priority
   * esp_mail_smtp_priority_high or 1
   * esp_mail_smtp_priority_normal or 3
   * esp_mail_smtp_priority_low or 5
   * The default value is esp_mail_smtp_priority_low
   */
  message.priority = esp_mail_smtp_priority::esp_mail_smtp_priority_normal;

  /** The Delivery Status Notifications e.g.
   * esp_mail_smtp_notify_never
   * esp_mail_smtp_notify_success
   * esp_mail_smtp_notify_failure
   * esp_mail_smtp_notify_delay
   * The default value is esp_mail_smtp_notify_never
   */
  // message.response.notify = esp_mail_smtp_notify_success | esp_mail_smtp_notify_failure | esp_mail_smtp_notify_delay;

  /* Set the custom message header */
  message.addHeader(F("Message-ID: <data.integration.specialist@gmail.com>"));

  /* Set the TCP response read timeout in seconds */
  // smtp.setTCPTimeout(10);

  /* Connect to the server */
  if (!smtp.connect(&config)) {
    MailClient.printf("Connection error, Status Code: %d, Error Code: %d, Reason: %s", smtp.statusCode(), smtp.errorCode(), smtp.errorReason().c_str());
    return;
  }

  if (smtp.isAuthenticated())
    Serial.println("\nSuccessfully logged in.");
  else
    Serial.println("\nConnected with no Auth.");

  /* Start sending Email and close the session */
  if (!MailClient.sendMail(&smtp, &message))
    MailClient.printf("Error, Status Code: %d, Error Code: %d, Reason: %s", smtp.statusCode(), smtp.errorCode(), smtp.errorReason().c_str());
  //2do add light that shows last email failed sending

  // to clear sending result log
  smtp.sendingResult.clear();

  MailClient.printf("Free Heap: %d\n", MailClient.getFreeHeap());

  // disconnect from WiFi when email is sent
  // WiFi_Disconnect();
}



/* Callback function to get the Email sending status */
void smtpCallback(SMTP_Status status) {
  /* Print the current status */
  Serial.println(status.info());

  /* Print the sending result */
  if (status.success()) {
    // MailClient.printf used in the examples is for format printing via debug Serial port
    // that works for all supported Arduino platform SDKs e.g. SAMD, ESP32 and ESP8266.
    // In ESP8266 and ESP32, you can use Serial.printf directly.

    Serial.println("----------------");
    MailClient.printf("Message sent success: %d\n", status.completedCount());
    MailClient.printf("Message sent failed: %d\n", status.failedCount());
    Serial.println("----------------\n");

    for (size_t i = 0; i < smtp.sendingResult.size(); i++) {
      /* Get the result item */
      SMTP_Result result = smtp.sendingResult.getItem(i);

      // In case, ESP32, ESP8266 and SAMD device, the timestamp get from result.timestamp should be valid if
      // your device time was synched with NTP server.
      // Other devices may show invalid timestamp as the device time was not set i.e. it will show Jan 1, 1970.
      // You can call smtp.setSystemTime(xxx) to set device time manually. Where xxx is timestamp (seconds since Jan 1, 1970)

      MailClient.printf("Message No: %d\n", i + 1);
      MailClient.printf("Status: %s\n", result.completed ? "success" : "failed");
      MailClient.printf("DEBUG: Date/Time: %s\n", MailClient.Time.getDateTimeString(result.timestamp, "%F %T").c_str());
      MailClient.printf("Recipient: %s\n", result.recipients.c_str());
      MailClient.printf("Subject: %s\n", result.subject.c_str());
    }
    Serial.println("----------------\n");

    // You need to clear sending result as the memory usage will grow up.
    smtp.sendingResult.clear();
  } else {
  }
}

/* Function to generate HTML content */
void GenerateHtml(String &pStatus, String &pStatusTime, String &pInitialTime, String &pTable, long cntOnStatusDuration, long totOnStatusDuration, int maxOnStatusDuration, int minOnStatusDuration, int avgOnStatusDuration, long cntOffStatusDuration, long totOffStatusDuration, int maxOffStatusDuration, int minOffStatusDuration, int avgOffStatusDuration) {
  HtmlContent = "<!DOCTYPE html>\
    <html>\
    <head> \
      <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\
      <title>Sump Pump Monitor</title>\
      <meta http-equiv=\"refresh\" content=\"15\">\
      <style>\
        body {background-color: #121212; color: #ffffff; font-family: Arial, sans-serif; margin: 0; padding: 0; }\
        .container {max-width: 800px; margin: 0 auto; padding: 20px;}\
        .status {font-size: 24px;margin-bottom: 10px;}\
        .statusTime {font-size: 12px;margin-bottom: 10px;}\
        table {width: 100%;border-collapse: collapse; margin-top: 20px;}\
        th, td { padding: 10px; text-align: left; border-bottom: 1px solid #444; }\
        th { background-color: #333; color: #fff; }\
      </style>\
    </head>\
    <body>\
      <div class=\"container\">\
        <div class=\"status\" id=\"currentStatus\">Current status: "
                + pStatus + "</div><div class=\"statusTime\" id=\"currentStatusTime\">As of " + pStatusTime + "</div><table id=\"statusAggr\"><thead><tr><th>Statistic</th><th>Value</th></tr></thead><tbody><tr><td>Initial date</td><td>" + pInitialTime + "<tr><td>Avg time On</td><td>" + String(avgOnStatusDuration) + " seconds</td></tr>" + "<tr><td>Max time On</td><td>" + String(maxOnStatusDuration) + " seconds</td></tr>" + "<tr><td>Min time On</td><td>" + String(minOnStatusDuration) + " seconds</td></tr>" + "<tr><td>Total time On</td><td>" + String(totOnStatusDuration) + " seconds</td></tr>" + "<tr><td>Count times On</td><td>" + String(cntOnStatusDuration) + "</td></tr>" + "<tr><td>Avg time Off</td><td>" + String(avgOffStatusDuration) + " seconds</td></tr>" + "<tr><td>Max time Off</td><td>" + String(maxOffStatusDuration) + " seconds</td></tr>" + "<tr><td>Min time Off</td><td>" + String(minOffStatusDuration) + " seconds</td></tr>" + "<tr><td>Total time Off</td><td>" + String(totOffStatusDuration) + " seconds</td></tr>" + "<tr><td>Count times Off</td><td>" + String(cntOffStatusDuration) + "</td></tr>" + "</td></tr></tbody></table><div class=\"status\" id=\"currentStatus\"><p>History</p></div><table id=\"statusHistory\"><thead><tr><th>Date</th><th>Status</th></tr></thead><tbody>" + pTable + "</tbody></table></div></body></html>";
}
