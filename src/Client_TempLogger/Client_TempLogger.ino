#include "DHT.h"
#include <ESP8266WiFi.h>
#include <EEPROM.h>
#include <ESP8266WebServer.h>

#define ESP8266_12_GPIO4 4 // Led in NodeMCU at pin GPIO16 (D0).
#define ESP8266_12_GPIO5 5
#define ESP8266_12_GPIO12 12
#define ESP8266_12_GPIO13 13
#define ESP8266_12_GPIO14 14
#define ESP8266_12_GPIO16 16
// Special ports -> TO BE CONFIRMED
#define ESP8266_12_GPIO15 15
#define ESP8266_12_GPIO0 0
#define ESP8266_12_GPIO2 2
//

#define NUM_SAMPLES 8

#define ESP8266_LED  ESP8266_12_GPIO5 
#define DHTPIN ESP8266_12_GPIO12
#define DHTTYPE DHT22

// DHT sensor instance
DHT dht(DHTPIN, DHTTYPE, 15);

// SM states
#define NUM_STATES  8
#define InitTime  0
#define AP4InitTime       1
#define BlindTime 2
#define Idle    3
#define NextAcquisition 4
#define AP 5
#define DataTx 6
#define Diagnostic 7

char clientState;
//EEPROM
int currPos = 0;

// Wifi
const char* ssid     = "AndroidAP_MARCO";
const char* password = "";

const char* host = "www.google.com";

unsigned long long Delay2NextState[NUM_STATES] = {10,1000,1000,1000,1000,1000,1000,1000}; // expressed in millisec
float tempSamplesArray[NUM_SAMPLES]={0};
float HumidSamplesArray[NUM_SAMPLES];
int currIndexSamples = 0;


// Time
unsigned long timeNow = 0;
unsigned long timeLast = 0;
unsigned long lastAcquisition = 0;
unsigned long lastTx = 0;

struct _time_data
{
  char currSeconds;
  char currMinutes;
  char currHours ;
  char currDays;
  char currTemp;
  char currHumidity;
};

union __data2write
{
    struct _time_data blk2write;
    char array2write[8];
};

union __data2write data2write;

// 
ESP8266WebServer server(80);

/* Just a little test message.  Go to http://192.168.4.1 in a web browser
 * connected to this access point to see it.
 */
void handleRoot() {
  server.send(200, "text/html", "<h1>You are connected</h1>");
}



void reinit()
{
  // 
  int i;
  for(i=0;i<NUM_SAMPLES;i++)
  {
    tempSamplesArray[i] = 0;
    HumidSamplesArray[i] = 0;
  }
  currIndexSamples = 0;
  
  timeNow = 0;
  timeLast = 0;
  data2write.blk2write.currSeconds = 0;
  data2write.blk2write.currMinutes = 0;
  data2write.blk2write.currHours = 0;
  data2write.blk2write.currDays = 0;
  data2write.blk2write.currTemp=0;
  data2write.blk2write.currHumidity=0;
}

int wifi_client()
{
  // Wifi scan
  return 0;
}

int SetWiFiAP()
{
// WiFi.mode(WIFI_AP);
//
//  // Do a little work to get a unique-ish name. Append the
//  // last two bytes of the MAC (HEX'd) to "Thing-":
//  // uint8_t mac[WL_MAC_ADDR_LENGTH];
//  // WiFi.softAPmacAddress(mac);
//  // String macID = String(mac[WL_MAC_ADDR_LENGTH - 2], HEX) +
//                 // String(mac[WL_MAC_ADDR_LENGTH - 1], HEX);
//  // macID.toUpperCase();
//  String AP_Name = "ESP8266 Thing ";
//  String WiFiAPPSK = "";
//
//  // char AP_NameChar[AP_NameString.length() + 1];
//  // memset(AP_NameChar, 0, AP_NameString.length() + 1);
//
//  // for (int i=0; i<AP_NameString.length(); i++)
//    // AP_NameChar[i] = AP_NameString.charAt(i);
//
//  WiFi.softAP(AP_Name, WiFiAPPSK);
// IPAddress myIP = WiFi.softAPIP();
//  Serial.print("AP IP address: ");
//  Serial.println(myIP);
//  server.on("/", handleRoot);
//  server.begin();
//  Serial.println("HTTP server started");
//
//server.handleClient();  
return 0;
  
}

int samplesAcquisition()
{
  // 8 samples every half min
  for(int i=0;i<NUM_SAMPLES;i++)
  {  
    // read the sample
    tempSamplesArray[i] = dht.readTemperature();
    HumidSamplesArray[i] = dht.readHumidity();
    // delay
    handlingTime(BlindTime);
    // 
  } 
}

int calcAverageAndAppend()
{
  int index;
  double tempVal,humVal;
  // temperature
  for(index=0;index<NUM_SAMPLES;index++)
  {
    tempVal +=  tempSamplesArray[index];  
    humVal += HumidSamplesArray[index];
  }
  
  data2write.blk2write.currTemp = tempVal/NUM_SAMPLES;
  // humidity
  data2write.blk2write.currHumidity = humVal/NUM_SAMPLES;

  for(index = 0; index < sizeof(data2write.blk2write);index++)
  {
       EEPROM.write(currPos, data2write.array2write[index]);
       currPos += index;
  }
  EEPROM.commit();
}

void UpdatingSWtimer()
{
  timeNow = millis()/1000; // the number of milliseconds that have passed since boot
  data2write.blk2write.currSeconds = timeNow - timeLast;//the number of seconds that have passed since the last time 60 seconds was reached.
  
  if (data2write.blk2write.currSeconds == 60) {
    timeLast = timeNow;
    data2write.blk2write.currMinutes += 1;
  }

  //if one minute has passed, start counting milliseconds from zero again and add one minute to the clock.

  if (data2write.blk2write.currMinutes == 60){ 
    data2write.blk2write.currMinutes = 0;
    data2write.blk2write.currHours += 1;
  }

  // if one hour has passed, start counting minutes from zero and add one hour to the clock

  if (data2write.blk2write.currHours == 24){
    data2write.blk2write.currHours = 0;
    data2write.blk2write.currDays = data2write.blk2write.currDays + 1;
    }

    //if 24 hours have passed , add one day

  // if (hours ==(24 - startingHour) && correctedToday == 0){
    // delay(dailyErrorFast*1000);
    // seconds = seconds + dailyErrorBehind;
    // correctedToday = 1;
  // }

  //every time 24 hours have passed since the initial starting time and it has not been reset this day before, add milliseconds or delay the progran with some milliseconds. 
  //Change these varialbes according to the error of your board. 
  // The only way to find out how far off your boards internal clock is, is by uploading this sketch at exactly the same time as the real time, letting it run for a few days 
  // and then determine how many seconds slow/fast your boards internal clock is on a daily average. (24 hours).

  // if (hours == 24 - startingHour + 2) { 
    // correctedToday = 0;
  // }

  //let the sketch know that a new day has started for what concerns correction, if this line was not here the arduiono
  // would continue to correct for an entire hour that is 24 - startingHour. 



  Serial.print("The time is:           ");
  Serial.print(data2write.blk2write.currDays);
  Serial.print(":");
  Serial.print(data2write.blk2write.currHours);
  Serial.print(":");
  Serial.print(data2write.blk2write.currMinutes);
  Serial.print(":"); 
  Serial.println(data2write.blk2write.currSeconds); 
}

int reinitializeTime()
{
  data2write.blk2write.currSeconds = 0;
  data2write.blk2write.currMinutes = 0;
  data2write.blk2write.currHours = 0;
  data2write.blk2write.currDays = 0;
}

int handlingTime(int currState)
{
  unsigned long delay_ms = Delay2NextState[currState];
  delay(delay_ms);
  UpdatingSWtimer();
}
  
void setup() 
{
  pinMode(ESP8266_LED, OUTPUT);
  Serial.begin(115200);
  Serial.println();
  Serial.println("Starting");

  dht.begin();
  
  delay(10);
  
   // We start by connecting to a WiFi network

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
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP()); 

  EEPROM.begin(512);
  // state machine
  clientState = 0;
}

void loop() 
{
  switch(clientState)
  {
    case InitTime:
    // reinitialization structures and counters  
    reinit();
    clientState = AP4InitTime;
    break;
    case AP4InitTime:
    // Open the communication
    SetWiFiAP();
//    server.begin();
//    // waiting for a request from the clientMaster
//      // Check if a client has connected
//    WiFiClient client = server.available();
//    if (!client) {
//      return;
//    }
//    
//    // Read the first line of the request
//    String req = client.readStringUntil('\r');
//    Serial.println(req);
//    client.flush();
//
//    // Send the response to the client
//    client.print(s);
//    delay(1);
//    Serial.println("Client disonnected");
    
    // client -> time -> requestNextComm= NO
    // if connection is OK
    reinitializeTime();
    
    clientState = BlindTime;
    
    // else
    clientState = Diagnostic;
    break;
    case BlindTime:
    // 
    samplesAcquisition();
    // calc average -> add in array
    calcAverageAndAppend();
    // next state
    clientState = Idle;
    break;
    case Idle:
       timeNow = millis()/1000; // the number of milliseconds that have passed since boot
       if(timeNow - lastTx >= Delay2NextState[AP])
       {
           // if Time to Communicate
           clientState = AP;
       }
       else
       {    
          // else
          handlingTime(Idle);
          clientState = NextAcquisition;
          // 
       }
    break;
    case NextAcquisition:
    // implementing the strategy
    samplesAcquisition();
    // average
    calcAverageAndAppend();
    // next 
    clientState = Idle;
    break;
    case AP:
    // Open the communication
    SetWiFiAP();
    server.begin();
    // waiting for client request -> repetition
    
    // If Time Out
    clientState = Diagnostic;
    // else
    clientState = DataTx;
    break;
    case DataTx:
    // search for AP_Master
    // If channel is OK
    // TX data
    clientState = Idle;
    // else
    clientState = Diagnostic;
    break;
    case Diagnostic:
    // if MAN = reinit 
    clientState = InitTime;
    // If MAN = TimeOut_Client
    
    // If MAN = 
    
    break;
  }
 
}
