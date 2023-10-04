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
- WiFiManager 2.0.16-rc.2

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

![esp32-ctclamp-connection](https://github.com/m1i1k/esp32-ct-sensor-monitor/assets/41442342/b4badb9e-d87b-494a-ac7f-119ddde331e4)

2DO

# Overview of the source code

2DO

# Wishlist

- Help Wanted: I have concerns regarding the sensor's wiring. I believe a resistor should connect the sensor's DC output to GND immediately after the capacitor. However, introducing a resistor causes the sensor's DC output voltage to drop to 0V. I tried 10 Ohm and 10k Ohm.
- While an inductor could be placed right after the bridge rectifier's DC output for further signal smoothing, it's not essential for my application. Precise current measurement isn't my objective.
- Ideally, the Wi-Fi should activate solely for sending notification emails and remain off otherwise. Regrettably, I couldn't achieve this functionality.
- There is no permanent storage of statistics. It would be nice to add an SD card to save the statistics in case of power outage.

# Final thoughts

2DO
