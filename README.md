# esp32-ct-sensor-monitor

CT sensor clamp sump pump monitor using ESP32

The objective of this project is to monitor a sump pump. Living in an area with a high water table, it's crucial for me to ensure my sump pump functions correctly. While the primary focus is on the sump pump, this system can also be adapted to monitor other appliances, such as a deep freezer.

A current transformer sensor detects the current in the cable powering the sump pump. If the sump pump is active, the sensor registers a HIGH reading; otherwise, it reads LOW. An ESP32 microcontroller sends email notifications if the sump pump remains on or off for extended periods. Additionally, the ESP32 microcontroller operates a small web server, offering real-time statistics on sump pump usage.

A significant advantage of this setup is its independence from external servers like Home Assistant; it only needs a Wi-Fi connection. However, a drawback is the lack of an external server to detect and notify in case of device failure.

Hardware used in this project:
- ESP32 microcontroller
- USB-C cable
- Power supply (USB charger)
- AC Current Sensor (Split Core Current Transformer)
- female headphone jack (AUX) adapter
- diode x4
- capacitor
- LED x3
- resistors x3
- buzzer
- breadboard
- wires to connect components
- plastic box/case
- AC Line Splitter

* All electronic components were purchased from AliExpress for a total of about $20 USD

Software used in this project:
- Arduino IDE 2.1.2
Required libraries:
- AsyncTCP 1.1.4
- ESPAsyncTCP 1.2.4
- ESPAsyncWebSrv 1.2.6
- ESP_Mail_Client 3.4.6
- LinkedList 1.3.3
    * I modified the name of the class because there was another library with a class that has the same name.
      * LinkedList.h
- WiFiManager 2.0.16-rc.2
    * I added a line to the code to reboot the ESP32 if it is unable to connect after hitting the re-try limit.
      * WiFiManager.cpp
      * WiFiManager.h

Finished product attached to wall in utility room:

![esp32-3jpg](https://github.com/m1i1k/esp32-ct-sensor-monitor/assets/41442342/5395bcee-59c1-42b9-94e4-00821c8582bd)

![esp32-final](https://github.com/m1i1k/esp32-ct-sensor-monitor/assets/41442342/51f954e0-9a7b-46af-90c4-b55ec9695ccb)

The CT clamp is attached to the outlet powering the sump pump. 

Warning: For safety, this should be enclosed in an electric box. I just have it exposed like this temporarily.

Note: The CT clamps only works when it is sensing the hot/live wire. If both the hot and neutral wires are inside the clamp then it will not give a reading.

![ESP32-ctclamp](https://github.com/m1i1k/esp32-ct-sensor-monitor/assets/41442342/c730da7e-ef25-4493-9c2e-4aa24c80de8d)

A proper solution would be to use an AC line splitter.

![esp-AClinesplitter_](https://github.com/m1i1k/esp32-ct-sensor-monitor/assets/41442342/4b08906d-db6f-4e78-bb63-ce0a82da00bd)

This is what the user interface looks like:

![Screenshot_20231004-150325](https://github.com/m1i1k/esp32-ct-sensor-monitor/assets/41442342/10c0ab20-61c1-4b71-8044-265483c92eac)


# Connecting the CT sensor to the ESP 32

![esp32-pin jpg](https://github.com/m1i1k/esp32-ct-sensor-monitor/assets/41442342/563f13cd-90d6-40bf-8863-5e853e8903ae)

A push button is read by pin 35. This button is used to reset the WiFi credentials. Doing so will force the captive portal to start where the user can reconfigure the WiFi credentials as well as the google account that is used for email notification.
```
#define RESET_BUTTON_PIN 35        //pin to reset WiFi credentials and enter captive portal mode
pinMode(RESET_BUTTON_PIN, INPUT);
```
LED indicatar pins are set to pin 0, 4, and 15. 
```
#define LED_PIN 0                  //pin to display LED (green LED) when current is detected in CT clamp
#define ALARM_PIN 4                //pin to raise alarm (red LED and buzzer) when the pump has been running abnormally.
#define WIFI_STATUS_PIN 15         //pin to show when WiFi is connected (yellow LED)

pinMode(LED_PIN, OUTPUT);
pinMode(ALARM_PIN, OUTPUT);  
pinMode(WIFI_STATUS_PIN, OUTPUT);
```
The sensor voltage is read by analog pin 34. 
```
#define SENSOR_PIN 34              //pin to read sensor value
pinMode(SENSOR_PIN, INPUT);
```
This diagram shows how the sensor pin is wired with the CT clamp. 

![esp32-ctclamp-connection](https://github.com/m1i1k/esp32-ct-sensor-monitor/assets/41442342/b4badb9e-d87b-494a-ac7f-119ddde331e4)


# Overview of the source code

The program initiates by verifying the presence of a WiFi connection through the WiFi Manager. If absent, it launches an access point alongside a captive portal. Users can then connect and input both their WiFi and Gmail credentials.

Upon specifying the WiFi details, the connection commences. Should the connection drop, the WiFi Manager attempts reconnection. A yellow LED signifies a successful WiFi connection. If issues arise with the connection, users can press the device's reset button, resetting the WiFi settings and reinitiating the captive portal.

Simultaneously, the program initiates two continuous tasks: taskReadSensor() and taskProcessSensor(). The former monitors the sensor, logging status changes to a queue. The latter processes these values, updating the web and email content accordingly.

The primary loop() function oversees flag variables, manages email notifications, operates the web server, and monitors WiFi connectivity.

Note: This application mandates a Gmail account and an application password, utilizing the Gmail API for email notifications.

Here is a high-level description of the logic in the code:

```
/* Setup:
- Configure ESP pins for I/O
- Initialize message queue for inter-process communication
- Initialize email client library (Gmail)
- Launch task: read sensor values
- Launch task: process sensor values
- Initialize WiFi
- Initialize WiFi Manager
- Retrieve Gmail credentials from EEPROM (set via WiFi Manager portal)
*/
void setup()



/* Main Loop:
- If WiFi connected, set up web server and mDNS for URL-based access
- Monitor event flags for email alerts and alarms
- Check for reset button press
*/
void loop()



/* Task: Process Sensor Values:
- Continuously check message queue for updates
- On message receipt, update statistics and web/email content
*/
void taskProcessSensor()



/* Task: Read Sensor Values:
- Continuously monitor sensor voltage
- Send message on state change (HIGH to LOW, LOW to HIGH) or after a time interval
- Flag for email alert and alarm if sensor state persists beyond threshold
*/
void taskReadSensor()



/* Function: Send Email using Gmail */
void sendEmail()



/* Callback: Report Email Send Status */
void smtpCallback()



/* Function: Generate HTML Content */
void GenerateHtml()
```

# Wishlist

- Help Wanted: I have concerns regarding the sensor's wiring. I believe a resistor should connect the sensor's DC output to GND immediately after the capacitor. However, introducing a resistor causes the sensor's DC output voltage to drop to 0V. I tried 10 Ohm and 10k Ohm. Email me at m1i1k at yahoo dot com.
- While an inductor could be placed right after the bridge rectifier's DC output for further signal smoothing, it's not essential for my application. Precise current measurement isn't my objective.
- Ideally, the Wi-Fi should activate solely for sending notification emails and remain off otherwise. Regrettably, I couldn't achieve this functionality.
- There is no permanent storage of statistics. It would be nice to add an SD card to save the statistics in case of power outage.

# Final thoughts

If I were to revisit this project, I'd consider deploying a master server in the cloud, perhaps using Firebase or Home Assistant. This device would then focus solely on reading the sensor and relaying messages to the server. Such an approach would enhance reliability during power outages and allow monitoring of multiple devices via a unified web interface. For scaling to numerous devices, I'd adopt a hub-and-spoke model, where each device is a basic chip with an RF transmitter, communicating with a central HUB connected to LAN or WiFi. For now, this device fulfills its purpose, alerting me to any issues with my sump pump. Overall, I'm pleased with the outcome.
