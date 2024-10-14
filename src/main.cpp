/*
Gate Counter by Greg Liebig gliebig@sheboyganlights.org
Initial Build 12/5/2023 12:15 pm
Changed time format YYYY-MM-DD hh:mm:ss 12/13/23


Version 0.5 Added second Reflective Sensor
10/10/24 




Purpose: suppliments Car Counter to improve traffic control and determine park capacity
Counts vehicles as they exit the park
Connects to WiFi and updates RTC on Boot
Uses an Optocoupler to read burried vehicle sensor for Ghost Controls Gate operating at 12V
DOIT DevKit V1 ESP32 with built-in WiFi & Bluetooth
SPI Pins
D5 - CS
D18 - CLK
D19 - MISO
D23 - MOSI

*/
#include <Arduino.h>
#include <Wire.h>
#include <PubSubClient.h>
//#include <ArduinoJson.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "RTClib.h"
#include "NTPClient.h"
#include <WiFiClientSecure.h>
#include <WiFiMulti.h>
#include "secrets.h"
#include "time.h"
//#include "FS.h"
#include "SD.h"
#include "SPI.h"
#if defined(ESP8266)
  #include <ESP8266WiFi.h>
  #include <ESPAsyncTCP.h>
#elif defined(ESP32)
  #include <WiFi.h>
  #include <AsyncTCP.h>
#endif

#include <ESPAsyncWebServer.h>
#include <ElegantOTAPro.h>

// ******************** VARIBLES *******************
#define magSensorPin 17 // Pin for Magnotometer Sensor
#define beamSensorPin 16  //Pin for Reflective Sensor
#define PIN_SPI_CS 5 // The ESP32 pin GPIO5
#define MQTT_KEEPALIVE 30
#define FWVersion "24.10.12" // Firmware Version
#define OTA_Title "Gate Counter" // OTA Title
// **************************************************

AsyncWebServer server(80);

unsigned long ota_progress_millis = 0;

void onOTAStart() {
  // Log when OTA has started
  Serial.println("OTA update started!");
  // <Add your own code here>
}

void onOTAProgress(size_t current, size_t final) {
  // Log every 1 second
  if (millis() - ota_progress_millis > 1000) {
    ota_progress_millis = millis();
    Serial.printf("OTA Progress Current: %u bytes, Final: %u bytes\n", current, final);
  }
}

void onOTAEnd(bool success) {
  // Log when OTA has finished
  if (success) {
    Serial.println("OTA update finished successfully!");
  } else {
    Serial.println("There was an error during OTA update!");
  }
  // <Add your own code here>
}

// Inser Hive MQTT Cert Below


//#include <DS3231.h>
RTC_DS3231 rtc;
int line1 =0;
int line2 =9;
int line3 = 20;
int line4 = 30;
int line5 = 42;
int line6 = 50;
int line7 = 53;

//Create Multiple WIFI Object

WiFiMulti wifiMulti;
WiFiClientSecure espGateCounter;
PubSubClient mqtt_client(espGateCounter);

unsigned long lastMsg = 0;
#define MSG_BUFFER_SIZE (500)
char msg[MSG_BUFFER_SIZE];
int value = 0;

char mqtt_server[] = mqtt_Server;
char mqtt_username[] = mqtt_UserName;
char mqtt_password[] = mqtt_Password;
const int mqtt_port = mqtt_Port;

// MQTT TOPIC DEFINITIONS
#define THIS_MQTT_CLIENT "espGateCounter" // Look at line 90 and set variable for WiFi Client secure & PubSubCLient 12/23/23

// Puplishing Topics 
#define MQTT_PUB_TOPIC0  "msb/traffic/exit/hello"
#define MQTT_PUB_TOPIC1  "msb/traffic/exit/temp"
#define MQTT_PUB_TOPIC2  "msb/traffic/exit/time"
#define MQTT_PUB_TOPIC3  "msb/traffic/exit/count"
#define MQTT_PUB_TOPIC4  "msb/traffic/exit/inpark"
#define MQTT_PUB_TOPIC5  "msb/traffic/exit/timeout"
#define MQTT_PUB_TOPIC6  "msb/traffic/exit/beamSensorState"
// Subscribing Topics (to reset values)
#define MQTT_SUB_TOPIC0  "msb/traffic/enter/count"
#define MQTT_SUB_TOPIC1  "msb/traffic/exit/resetcount"


//const uint32_t connectTimeoutMs = 10000;
uint16_t connectTimeOutPerAP=5000;
const char* ampm ="AM";
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = -21600;
const int   daylightOffset_sec = 3600;
int16_t temp;

char buf2[25] = "YYYY-MM-DD hh:mm:ss";

// ******** RESET COUNTS ON REBOOT *******
int currentDay = 0;
int currentHour = 0;
int currentMin = 0;
int totalDailyCars = 0;
int carCounterCars =0;
int sensorBounceCount=0;
int sensorBounceRemainder;
bool sensorBounceFlag;

bool carPresentFlag = 0;

bool nocarTimerFlag = 0;
unsigned long nocarTimerMillis =0;
bool magSensorState = 0;
bool beamSensorState = 0;
bool last_beamSensorState = 1;
unsigned long whileMillis; // used for debugging
unsigned long lastwhileMillis = 0;
unsigned long beamSensorStateLowMillis; // used for debugging
unsigned long last_beamSensorStateLowMillis = 0;
unsigned long beamSensorStateHighMillis;
unsigned long last_beamSensorStateHighMillis;
unsigned long bounceTimerMillis;

//########### Bounce Times ######################################################################################
unsigned long ignoreBounceTimeMillis = 1000;  // Maximum time to ingnore a beam state change while car in detection zone
unsigned long nocarTimeoutMillis = 900; // Time required for High Pin to stay low to reset car in detection zone
unsigned long carpassingTimoutMillis = 6000; // Time delay to allow car to pass before checking for HIGN pin

//unsigned long highMillis = 0; //Grab the time when the vehicle sensor is high
unsigned long previousMillis; // Last time sensor pin changed state
unsigned long currentMillis; // Comparrison time holder
unsigned long carDetectedMillis;  // Grab the ime when sensor 1st trips
unsigned long lastcarDetectedMillis;  // Grab the ime when sensor 1st trips


unsigned long wifi_lastReconnectAttemptMillis;
unsigned long wifi_connectioncheckMillis = 5000; // check for connection every 5 sec
unsigned long mqtt_lastReconnectAttemptMillis;
unsigned long mqtt_connectionCheckMillis = 5000;
unsigned long nowwifi;
unsigned long nowmqtt;






File myFile; //used to write files to SD Card
File myFile2;

char days[7][4] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
char months[12][4] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

// var ************* DISPLAY SIZE ************
Adafruit_SSD1306 display = Adafruit_SSD1306(128, 64, &Wire, -1);

/*
unsigned long ota_progress_millis = 0;

void onOTAStart() {
  // Log when OTA has started
  Serial.println("OTA update started!");
  // <Add your own code here>
}

void onOTAProgress(size_t current, size_t final) {
  // Log every 1 second
  if (millis() - ota_progress_millis > 1000) {
    ota_progress_millis = millis();
    Serial.printf("OTA Progress Current: %u bytes, Final: %u bytes\n", current, final);
  }
}

void onOTAEnd(bool success) {
  // Log when OTA has finished
  if (success) {
    Serial.println("OTA update finished successfully!");
  } else {
    Serial.println("There was an error during OTA update!");
  }
  // <Add your own code here>
}
*/



void setup_wifi() {
    Serial.println("Connecting to WiFi");
    display.println("Connecting to WiFi..");
    display.display();
    while(wifiMulti.run(connectTimeOutPerAP) != WL_CONNECTED) {
      //Serial.print(".");
    }
    Serial.println("Connected to the WiFi network");
    display.clearDisplay();
    display.setTextColor(WHITE);
    display.setTextSize(1);
    display.display();
   // print the SSID of the network you're attached to:
    display.setCursor(0, line1);
    display.print("SSID: ");
    display.println(WiFi.SSID());

    Serial.print("SSID: ");
    Serial.println(WiFi.SSID());

    // print your board's IP address:
    IPAddress ip = WiFi.localIP();
    Serial.print("IP: ");
    Serial.println(ip);
    display.setCursor(0, line2);
    display.print("IP: ");
    display.println(ip);

    // print the received signal strength:
    long rssi = WiFi.RSSI();
    Serial.print("signal strength (RSSI):");
    Serial.print(rssi);
    Serial.println(" dBm");
    display.setCursor(0, line3);
    display.print("signal: ");
    display.print(rssi);
    display.println(" dBm");
    display.display();
 
  // Elegant OTA
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/plain", "Hi! This is The Gate Counter.");
  });

  // You can also enable authentication by uncommenting the below line.
  // ElegantOTA.setAuth("admin", "password");

  ElegantOTA.setID(THIS_MQTT_CLIENT);  // Set Hardware ID
  ElegantOTA.setFWVersion(FWVersion);   // Set Firmware Version
  ElegantOTA.setTitle(OTA_Title);  // Set OTA Webpage Title

  ElegantOTA.begin(&server);    // Start ElegantOTA

  // ElegantOTA callbacks
  ElegantOTA.onStart(onOTAStart);
  ElegantOTA.onProgress(onOTAProgress);
  ElegantOTA.onEnd(onOTAEnd);

  server.begin();
  Serial.println("HTTP server started");

  delay(5000);
}


void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  payload[length] = '\0';
 
  if (strcmp(topic, MQTT_SUB_TOPIC0) == 0) {
     carCounterCars = atoi((char *)payload);
//     Serial.println(" Car Counter Updated");
    }
  
  if (strcmp(topic, MQTT_SUB_TOPIC1) == 0){
    totalDailyCars = atoi((char *)payload);
//    Serial.println(" Gate Counter Updated");
  }
  //  Serial.println(carCountCars);
  Serial.println();
}


void reconnect() {
  // Loop until we’re reconnected
  while (!mqtt_client.connected()) {
    Serial.print("Attempting MQTT connection… ");
    String clientId = THIS_MQTT_CLIENT;
    // Attempt to connect
    if (mqtt_client.connect(clientId.c_str(), mqtt_username, mqtt_password)) {
      Serial.println("connected!");
      Serial.println("Waiting for Car");
      // Once connected, publish an announcement…
      mqtt_client.publish(MQTT_PUB_TOPIC0, "Hello from Gate Counter!");
      // … and resubscribe
      mqtt_client.subscribe(MQTT_PUB_TOPIC0);
    } else {
      Serial.print("failed, rc = ");
      Serial.print(mqtt_client.state());
      Serial.println(" try again in 5 seconds");
    }
  }
  mqtt_client.subscribe(MQTT_SUB_TOPIC0);
  mqtt_client.subscribe(MQTT_SUB_TOPIC1);
}

void SetLocalTime() {
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)){
    Serial.println("Failed to obtain time. Using Compiled Date");
    return;
  }
  //Following used for Debugging and can be commented out
  Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S");
  Serial.print("Day of week: ");
  Serial.println(&timeinfo, "%A");
  Serial.print("Month: ");
  Serial.println(&timeinfo, "%B");
  Serial.print("Day of Month: ");
  Serial.println(&timeinfo, "%d");
  Serial.print("Year: ");
  Serial.println(&timeinfo, "%Y");
  Serial.print("Hour: ");
  Serial.println(&timeinfo, "%H");
  Serial.print("Hour (12 hour format): ");
  Serial.println(&timeinfo, "%I");
  Serial.print("Minute: ");
  Serial.println(&timeinfo, "%M");
  Serial.print("Second: ");
  Serial.println(&timeinfo, "%S");

  Serial.println("Time variables");
  char timeHour[3];
  strftime(timeHour,3, "%H", &timeinfo);
  Serial.println(timeHour);
  char timeWeekDay[10];
  strftime(timeWeekDay,10, "%A", &timeinfo);
  Serial.println(timeWeekDay);
  Serial.println();

  // Convert NTP time string to set RTC
  char timeStringBuff[50]; //50 chars should be enough
  strftime(timeStringBuff, sizeof(timeStringBuff), "%Y-%m-%d %H:%M:%S", &timeinfo);
  Serial.println(timeStringBuff);
  rtc.adjust(DateTime(timeStringBuff));
}


void setup() {
  Serial.begin(115200);
  //Initialize Display
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);

  display.clearDisplay();
  display.setTextColor(WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.display();

  //Initialize SD Card
  if (!SD.begin(PIN_SPI_CS)) {
    Serial.println(F("SD CARD FAILED, OR NOT PRESENT!"));
    while (1); // stop the program
    display.clearDisplay();
    display.setTextSize(2);
    display.setTextColor(WHITE);
    display.setCursor(0,line1);
    display.println("Check SD Card");
    display.display();
  }

  Serial.println(F("SD CARD INITIALIZED."));
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(WHITE);
    display.setCursor(0,line1);
    display.println("SD Card Ready");
    display.display();
 
  if (!SD.exists("/GateCount.csv")) {
    Serial.println(F("GateCount.csv doesn't exist. Creating GateCount.csv file..."));
    // create a new file by opening a new file and immediately close it
    myFile = SD.open("/GateCount.csv", FILE_WRITE);
    myFile.close();
      // recheck if file is created or not & write Header
  if (SD.exists("/GateCount.csv")){
    Serial.println(F("GateCount.csv exists on SD Card."));
    myFile = SD.open("/GateCount.csv", FILE_APPEND);
    myFile.println("Date Time,Pass Timer,NoCar Timer,Bounces,Car#,Cars In Park,Temp,Last Car Millis, This Car Millis,Bounce Flag,Millis");
    myFile.close();
    Serial.println(F("Header Written to GateCount.csv"));
  }else{
    Serial.println(F("GateCount.csv doesn't exist on SD Card."));
  }
  }


  if (!SD.exists("/SensorBounces.csv")) {
    Serial.println(F("SensorBounces.csv doesn't exist. Creating SensorBounces.csv file..."));
    // create a new file by opening a new file and immediately close it
    myFile2 = SD.open("/SensorBounces.csv", FILE_WRITE);
    myFile2.close();
   // recheck if file is created or not & write Header
  if (SD.exists("/SensorBounces.csv")){
    Serial.println(F("SensorBounces.csv exists on SD Card."));
    myFile2 = SD.open("/SensorBounces.csv", FILE_APPEND);
    //("DateTime\t\t\tPassing Time\tLast High\tDiff\tLow Millis\tLast Low\tDiff\tBounce #\tCurent State\tCar#" )
    myFile2.println("Time,Pass Timer,Last High,Diff,No Car Timer,Low Millis,Last Low,Diff,Bounce#,Curent State,Car#,Millis");
    myFile2.close();
    Serial.println(F("Header Written to SensorBounces.csv"));
  }else{
    Serial.println(F("SensorBounces.csv doesn't exist on SD Card."));
  }
 
  }



  WiFi.mode(WIFI_STA); 
  wifiMulti.addAP(secret_ssid_AP_1,secret_pass_AP_1);
  wifiMulti.addAP(secret_ssid_AP_2,secret_pass_AP_2);
  wifiMulti.addAP(secret_ssid_AP_3,secret_pass_AP_3);
  wifiMulti.addAP(secret_ssid_AP_4,secret_pass_AP_4);
  wifiMulti.addAP(secret_ssid_AP_5,secret_pass_AP_5);
  
  // WiFi.scanNetworks will return the number of networks found
  int n = WiFi.scanNetworks();
  Serial.println("scan done");
  if (n == 0) {
      Serial.println("no networks found");
  } 
  else {
    Serial.print(n);
    Serial.println(" networks found");
    for (int i = 0; i < n; ++i) {
      // Print SSID and RSSI for each network found
      Serial.print(i + 1);
      Serial.print(": ");
      Serial.print(WiFi.SSID(i));
      Serial.print(" (");
      Serial.print(WiFi.RSSI(i));
      Serial.print(")");
      Serial.println((WiFi.encryptionType(i) == WIFI_AUTH_OPEN)?" ":"*");
      delay(10);
    }
  }
  
  setup_wifi();
//  espGateCounter.setCACert(root_ca);
  mqtt_client.setServer(mqtt_server, mqtt_port);
  mqtt_client.setCallback(callback);

  //If RTC not present, stop and check battery
  if (! rtc.begin()) {
    Serial.println("Could not find RTC! Check circuit.");
    display.clearDisplay();
    display.setTextSize(2);
    display.setTextColor(WHITE);
    display.setCursor(0,line1);
    display.println("Clock DEAD");
    display.display();
    while (1);
  }

  // Get NTP time from Time Server 
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  SetLocalTime();
  
  //Set Input Pin
  pinMode(magSensorPin, INPUT_PULLDOWN);
   pinMode(beamSensorPin, INPUT_PULLDOWN);
 
  display.clearDisplay();
  display.setTextColor(WHITE);
  display.setTextSize(2);
  display.setCursor(0, line4);
  display.print("GATE Count");

  Serial.println  ("Initializing Gate Counter");
    Serial.print("Temperature: ");
    temp=((rtc.getTemperature()*9/5)+32);
    Serial.print(temp);
    Serial.println(" F");
  display.display();
  delay(3000);
}

void loop() {
  ElegantOTA.loop();

    // non-blocking WiFi and MQTT Connectivity Checks
    if (wifiMulti.run() == WL_CONNECTED) {
      // Check for MQTT connection only if wifi is connected
      if (!mqtt_client.connected()){
        nowmqtt=millis();
        if(nowmqtt - mqtt_lastReconnectAttemptMillis > mqtt_connectionCheckMillis){
          mqtt_lastReconnectAttemptMillis = nowmqtt;
          Serial.println("Attempting MQTT Connection");
          reconnect();
        }
          mqtt_lastReconnectAttemptMillis =0;
      } else {
        //keep MQTT client connected when WiFi is connected
        mqtt_client.loop();
      }
    } else {
        // Reconnect WiFi if lost, non blocking
        nowwifi=millis();
          if ((nowwifi - wifi_lastReconnectAttemptMillis) > wifi_connectioncheckMillis){
            setup_wifi();
          }
        wifi_lastReconnectAttemptMillis = 0;
    }


      
      DateTime now = rtc.now();
      temp=((rtc.getTemperature()*9/5)+32);
      //Reset Gate Counter at 5:00:00 pm
        if ((now.hour() == 17) && (now.minute() == 0) && (now.second() == 0)){
             totalDailyCars = 0;
         }
      display.clearDisplay();
      display.setTextSize(1);
      display.setCursor(0, line1);
      //  display Day of Week
      display.print(days[now.dayOfTheWeek()]);

      //  Display Date
      display.print(" ");         
      display.print(months[now.month(), DEC +1]);
      display.print(" ");
      display.print(now.day(), DEC);
      display.print(", ");
      display.println(now.year(), DEC);
      
      // Convert 24 hour clock to 12 hours
      currentHour = now.hour();

      if (currentHour - 12 > 0) {
          ampm ="PM";
          currentHour = now.hour() - 12;
      }else{
          currentHour = now.hour();
          ampm = "AM";
      }

      //Display Time
      //add leading 0 to Hours & display Hours
      display.setTextSize(1);

      if (currentHour < 10){
        display.setCursor(0, line2);
        display.print("0");
        display.println(currentHour, DEC);
      }else{
        display.setCursor(0, line2);
        display.println(currentHour, DEC);
      }

      display.setCursor(14, line2);
      display.println(":");
 
      //Add leading 0 To Mintes & display Minutes 
      //  display.setTextSize(1);
      if (now.minute() < 10) {
        display.setCursor(20, line2);
        display.print("0");
        display.println(now.minute(), DEC);
      }else{
        display.setCursor(21, line2);
        display.println(now.minute(), DEC);
      }

      display.setCursor(34, line2);
      display.println(":");

      //Add leading 0 To Seconds & display Seconds
      //  display.setTextSize(1);
      if (now.second() < 10){
        display.setCursor(41, line2);
        display.print("0");
        display.println(now.second(), DEC);
      }else{
        display.setCursor(41, line2);
        display.println(now.second(), DEC);   
      }

      // Display AM-PM
      display.setCursor(56, line2);
      display.println(ampm); 

      // Display Temp
      // display.setTextSize(1);
      display.setCursor(73, line2);
      display.print("Temp: " );
      //display.setCursor(70, 10);
      display.println(temp, 0);

      // Display Gate Count
      display.setTextSize(1);
      display.setCursor(0, line3);
      display.print("Exiting: ");
      display.setTextSize(2); 
      
      display.setCursor(50, line3);           
      display.println(totalDailyCars);
      display.setTextSize(1);
      display.setCursor(0, line5);
      display.print("In Park: ");
      display.setTextSize(2); 
      display.setCursor(50, line5);
      display.println(carCounterCars-totalDailyCars);


      display.display();
      magSensorState=digitalRead(magSensorPin);
      beamSensorState=digitalRead(beamSensorPin);
 // @@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@     
      // Count Cars Exiting
      // Sensing Vehicle  
      // Detector HIGH when vehicle sensed, Normally LOW
      // Both Sensors need to be active to start sensing vehicle Magnotometer senses vehicle not people
      // Then Beam confirms vehicle is present and then count car after vehicle passes
      // IMPORTANT: Magnotometer will bounce as a single vehicle passes. 
      // ignore if beam sensor bounces for x millis 
      if ((magSensorState == HIGH) && (beamSensorState == HIGH)) {
          lastwhileMillis = 0;
          last_beamSensorStateLowMillis=0;  // Reset beamSensor Timer
          nocarTimerMillis=0;  
          sensorBounceCount = 0; //Reset bounce counter
          carPresentFlag = 1; // when both detectors are high, set flag car is in detection zone. Only watch Beam Sensor
          carDetectedMillis = millis(); // Start timer when car entered detection zone
          bounceTimerMillis = 0; // Reset bounceTimer to 0 for bounce check
//          beamSensorStateLowMillis = millis()-carDetectedMillis;
//          lastSensorState=HIGH;

// Used for debugging
          DateTime now = rtc.now();
          char buf3[] = "YYYY-MM-DD hh:mm:ss"; //time of day when detector was tripped
          Serial.print("Car Triggered Detector at = ");
          Serial.print(carDetectedMillis);
          Serial.print(", Car Number Being Counted = ");         
          Serial.println (totalDailyCars+1) ;  //add 1 to total daily cars so car being detected is synced
          Serial.println("DateTime\t\tWhile\tLHigh\tDiff\tnoCar\tLow Millis\tLast LOW\tDiff\tBounce #\tCurent State\tCar#\tMillis" );  

          // When both Sensors are tripped, car is in the detection zone.
          // figure out when car clears detection zone & beam sensor remains lOW for period of time
          // Then Reset Car Present Flag to 0
          while (carPresentFlag == 1) {
             beamSensorState = digitalRead(beamSensorPin); // Beam Sensor is now priority. Ignore magSensor until car clears detection zone
             currentMillis = millis();
             whileMillis=millis()-carDetectedMillis; //   While car in detection zone, Record relative time while car is passing         
                       // Beam sensor may bounce while vehicle is in detection zone from high to low to high
                       // This loop is used to ignore those bounces
                       // if beam detector state changes from HIGH to LOW for more than ignoreBounceTimeMillis car cleared sensor & increment count
                       // If it remains HIGH car is in detection zone
                       // Added publishing state changes of beamSensor to MQTT 10/13/24
                       if ((beamSensorState != last_beamSensorState)  && (beamSensorState==HIGH)) {
                                          lastwhileMillis=whileMillis; 
                                          mqtt_client.publish(MQTT_PUB_TOPIC6, String(beamSensorState).c_str());
                        //                  nocarTimerMillis = millis();       
                       }
                       // If beamSensorState bounces low need to start timer to determine how long it remains low bounceTimerMillis < ignoreBounceTimeMillis
                       if ((beamSensorState != last_beamSensorState)  && (beamSensorState==LOW)) {
                          sensorBounceCount ++;  //count the state changes
                          nocarTimerMillis = millis();  // start timer for no car in detection zone
                          bounceTimerMillis = millis(); // start timer for beamSensor bounce 
                          mqtt_client.publish(MQTT_PUB_TOPIC6, String(beamSensorState).c_str());
                       //}
                       
                       //Record Bounce
                          DateTime now = rtc.now();
                          char buf2[] = "YYYY-MM-DD hh:mm:ss";
                          //Count number of Bounces and check each 4 bounces

                          //start a timer to time how long senstor remains low when it switches states
                          beamSensorStateLowMillis=millis()-carDetectedMillis; //

                          

                          //Debugging Code Can be removed  **************************************************************************
                          Serial.print(now.toString(buf2));
                          Serial.print(" \t\t ");
                          Serial.print(whileMillis);
                          Serial.print(" \t ");
                          Serial.print(lastwhileMillis);
                          Serial.print(" \t ");
                          Serial.print(whileMillis-lastwhileMillis);
                          Serial.print(" \t ");
                          Serial.print(millis()-nocarTimerMillis);  
                          Serial.print(" \t ");
                          Serial.print(beamSensorStateLowMillis);                        
                          Serial.print(" \t\t ");   
                          Serial.print(last_beamSensorStateLowMillis);
                          Serial.print(" \t\t ");   
                          Serial.print(beamSensorStateLowMillis-last_beamSensorStateLowMillis);
                          Serial.print(" \t\t ");   
                          Serial.print(sensorBounceCount);
                          Serial.print(" \t\t ");              
                          Serial.print(beamSensorState);
                          //Serial.print(" \t\t ");
                          //Serial.print(last_beamSensorState);
                          Serial.print(" \t\t ");
                          Serial.print(totalDailyCars+1);
                          Serial.print(" \t\t ");
                          Serial.print(millis());
                          Serial.println();
                         
                         //T("DateTime\t\t\tPassing Time\tLast High\tDiff\tLow Millis\tLast Low\tDiff\tBounce #\tCurent State\tCar#" )
                          myFile2 = SD.open("/SensorBounces.csv", FILE_APPEND);
                          if (myFile2) {
                              myFile2.print(now.toString(buf2));
                              myFile2.print(", "); 
                              myFile2.print(whileMillis);
                              myFile2.print(", "); 
                              myFile2.print (lastwhileMillis) ; 
                              myFile2.print(", ");
                              myFile2.print(whileMillis-lastwhileMillis);
                              myFile2.print(", ");
                              myFile2.print(millis()-nocarTimerMillis);
                              myFile2.print(", ");
                              myFile2.print(beamSensorStateLowMillis);
                              myFile2.print(", ");
                              myFile2.print(last_beamSensorStateLowMillis);
                              myFile2.print(", ");
                              myFile2.print(beamSensorStateLowMillis-last_beamSensorStateLowMillis);
                              myFile2.print(" , ");              
                              myFile2.print(sensorBounceCount);
                              myFile2.print(" , ");
                              myFile2.print(beamSensorState);
                              myFile2.print(" , ");
                              myFile2.print(totalDailyCars+1); //Prints this Car millis
                              myFile2.print(" , ");
                              myFile2.print(lastcarDetectedMillis); //Prints Last car millis
                              myFile2.print(" , ");
                              myFile2.print(carDetectedMillis); //Prints car number being detected
                              myFile2.print(" , ");
                              myFile2.print(millis()); //Prints current millis for debugging
                              myFile2.println();
                              myFile2.close();
                              //Serial.println(F(" Bounce Log Recorded SD Card."));
                          } else {
                              Serial.print(F("SD Card: Issue encountered while attempting to open the file GateCount.csv"));
                          }
                       
                           // end of debugging code ********************************************************************************* 
                           //Detector State is bouncing
                       } // end of if detector state is bouncing check

                      // Dectector is HIGH CHECK CAR HAS CLEARED AND BREAK LOOP #################################################

                      //force count & reset if there is an undetectable car present 12/25/23
                      // This section may be removed with new beam sensor 10/13/24                 
                            // Check added 12/21/23 to ensure no car is present for x millis
                            if (beamSensorState==LOW)  {
                                         //lastwhileMillis=whileMillis;  
                              
                              // If no car is present and state does not change, then car has passed
                              if (((millis() - nocarTimerMillis) >= nocarTimeoutMillis) ) { 
                                nocarTimerFlag = 0;
                              } 
                              //Resets if Loop sticks after 10 seconds and does not record a car.
                              if (millis() - carDetectedMillis > 10000) {
                                 Serial.println("Timeout! No Car Counted");
                        mqtt_client.publish(MQTT_PUB_TOPIC5, String(totalDailyCars+1).c_str());
                                 carPresentFlag=0;
                                 break;
                              }

                            } else {
                              //nocarTimerMillis = millis();   // Start or Reset Timer when pin goes high
                              nocarTimerFlag = 1;  // change state to active
                              }



             //allow enough time for a car to pass and then make sure sensor remains low 10/13/24
             //Conditions that myst be met for a car to be clear and count the car ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
             //Main Reset car passing timer
             
               // beamSensor is Low & no car Present OR if Bounce times out then record car
               if (((beamSensorState == LOW) && (nocarTimerFlag == 0)) || (beamSensorStateLowMillis-last_beamSensorStateLowMillis > ignoreBounceTimeMillis) ) {
                  Serial.print(now.toString(buf3));
                  Serial.print(", Millis NoCarTimer = ");
                  Serial.print(currentMillis-nocarTimerMillis);
                  Serial.print(", Total Millis to pass = ");
                  Serial.println(currentMillis-carDetectedMillis);
                  //Serial.print(", ");
                  //Serial.print(String("DateTime::TIMESTAMP_FULL:\t")+now.timestamp(DateTime::TIMESTAMP_FULL));
                  //Serial.print(",1,"); 
                  totalDailyCars ++;     

                  // open file for writing Car Data
                  //"Date Time,Pass Timer,NoCar Timer,TotalExitCars,CarsInPark,Temp"
                  myFile = SD.open("/GateCount.csv", FILE_APPEND);
                  if (myFile) {
                      myFile.print(now.toString(buf3));
                      myFile.print(", ");
                      myFile.print (currentMillis-carDetectedMillis) ; 
                      myFile.print(", ");
                      myFile.print (currentMillis-nocarTimerMillis) ; 
                      myFile.print(", "); 
                      myFile.print (sensorBounceCount) ; 
                      myFile.print(", ");                       
                      myFile.print (totalDailyCars) ; 
                      myFile.print(", ");
                      myFile.print(carCounterCars-totalDailyCars);
                      myFile.print(", ");
                      myFile.print(temp);
                              myFile.print(" , ");
                              myFile.print(lastcarDetectedMillis); //Prints car number being detected
                              myFile.print(" , ");
                              myFile.print(carDetectedMillis); //Prints car number being detected
                      myFile.print(", ");
                      myFile.print(sensorBounceFlag);
                      myFile.print(", ");
                      myFile.println(millis());
                      myFile.close();
                      
                      Serial.print(F("Car Saved to SD Card. Car Number = "));
                      Serial.print(totalDailyCars);
                      Serial.print(F(" Cars in Park = "));
                      Serial.println(carCounterCars-totalDailyCars);  
                        mqtt_client.publish(MQTT_PUB_TOPIC1, String(temp).c_str());
                        mqtt_client.publish(MQTT_PUB_TOPIC2, now.toString(buf3));
                        mqtt_client.publish(MQTT_PUB_TOPIC3, String(totalDailyCars).c_str());
                        mqtt_client.publish(MQTT_PUB_TOPIC4, String(carCounterCars-totalDailyCars).c_str());

                        //snprintf (msg, MSG_BUFFER_SIZE, "Car #%ld,", totalDailyCars);
                        //Serial.print("Publish message: ");
                        //Serial.println(msg);
                        //mqtt_client.publish("msbGateCount", msg);
                      //}
                  } else {
                      Serial.print(F("SD Card: Issue encountered while attempting to open the file GateCount.csv"));
                  }
                  carPresentFlag = 0;
                  sensorBounceFlag = 0;
                  whileMillis = 0;
                  lastcarDetectedMillis=carDetectedMillis;
              }  // end of car passed check

             last_beamSensorState=beamSensorState;
             last_beamSensorStateLowMillis=beamSensorStateLowMillis;
             last_beamSensorStateHighMillis=beamSensorStateHighMillis;


             //sensorBounceCount =0;
            
           } // end of while loop
      } // Start looking for next HIGH on Vehicle sensor

      //loop forever looking for car and update time and counts
}