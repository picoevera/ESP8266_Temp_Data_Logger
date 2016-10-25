#include "DHT.h"
#include <ESP8266WiFi.h>
#include <EEPROM.h>
#include <ESP8266WebServer.h>

#define ESP8266_CLIENT_ID  0

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
#define NUM_STATES  7
#define InitTime  0
#define AP4InitTime       1
#define Idle    2
#define NextAcquisition 3
#define AP 4
#define DataTx 5
#define Diagnostic 6

char clientState;
//EEPROM
int currPos = 0;

// Diagnostic
int curr_diag_synthom;
#define E_OK                0
#define TO_WIFI_CONN        1
#define TXDATA_CLIENT_FAILED  2

// Wifi
#define RETRY_DELAY_WIFI_CONN  500
#define TO_WIFI_CONN_VAL        2000

const char* ssid     = "AndroidAP_MARCO";
const char* password = "";

const char* host = "192.168.4.1";

unsigned long long Delay2NextState[NUM_STATES] = {10,1000,1000,1000,1000,1000,1000}; // expressed in millisec
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




void debugPrint(String str) {
  if(isDebug)
    serialDebug->print(str);
}

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

int SetWiFiAP(int currState)
{
 // https://learn.sparkfun.com/tutorials/esp8266-thing-hookup-guide/example-sketch-ap-web-server
//  
  WiFiClient client;
 WiFi.mode(WIFI_AP);
    int TOcounter = 0;
    boolean TO_flag = 0;
    const int httpPort_AP = 2357;
    String response2client;
   ESP8266WebServer server(httpPort_AP); 
   
  String AP_Name = "ESP8266_Client_" + ESP8266_CLIENT_ID;
  String WiFiAPPSK = "";
  // NECESSARIO CONVERTIRE DA STRING TO CHAR??????
  // char AP_NameChar[AP_NameString.length() + 1];
  // memset(AP_NameChar, 0, AP_NameString.length() + 1);

  // for (int i=0; i<AP_NameString.length(); i++)
    // AP_NameChar[i] = AP_NameString.charAt(i);

  WiFi.softAP(AP_Name, WiFiAPPSK);
 IPAddress myIP = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(myIP);
  server.on("/", handleRoot); //????
  server.begin();
  Serial.println("HTTP server started");

  // Check if a client has connected
  while((client = server.available()) == 0 && !TO_flag)
  {
      delay(RETRY_DELAY_CLIENT_TO_AP);
      Serial.print(".");
      TOcounter += RETRY_DELAY_CLIENT_TO_AP;
      if(TOcounter > TO_CLIENT_TO_AP_VAL)
      {
          TO_flag = 1;
      }
  }
  if(TO_flag)
    return TO_CLIENT_TO_AP;
    
  // Read the first line of the request
  String req = client.readStringUntil('\r');
  Serial.println(req);
  client.flush();

   if (req.indexOf("Master_AP") != -1)
   {
      if(currState == AP4InitTime)
      {
          // retrieve the current date and the current time -> init the timing
          //curr_time=year_month_days_hours_mins_seconds
          if(req.indexOf("curr_time") != -1)
          {
              data2write.blk2write.currSeconds = 0; // parsing the string for seconds   
              data2write.blk2write.currMinutes = 0;
              data2write.blk2write.currHours = 0;
              data2write.blk2write.currDays = 0;
              response2client = "NO_DATA_TO_TX";
          }  
       }
       if(currState == AP)
       {
           response2client = "DATA_TO_TX";
       }  
   }
  // Prepare the response. Start with the common header:
  String s = "HTTP/1.1 200 OK\r\n";
  s += "Content-Type: text/html\r\n\r\n";
  s += "<!DOCTYPE HTML>\r\n<html>\r\n";
  s += response2client;
  s += "</html>\n";

  // Send the response to the client
  client.print(s);
  delay(1);
  Serial.println("Client disonnected");

  // The client will actually be disconnected 
  // when the function returns and 'client' object is detroyed 
  return E_OK;
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


// http://www.rudiswiki.de/wiki9/WiFiFTPServer
int client4TxData()
{
    // variables
    int TOcounter = 0;
    boolean TO_flag = 0;
    const int httpPort_TxData = 2356;
    // searcf for the Master_AP 
    WiFi.begin(ssid, password);
    // create the connection
    while (WiFi.status() != WL_CONNECTED && !TO_flag) {
      delay(RETRY_DELAY_WIFI_CONN);
      Serial.print(".");
      TOcounter += RETRY_DELAY_WIFI_CONN;
      if(TOcounter > TO_WIFI_CONN_VAL)
      {
          TO_flag = 1;
      }
    }
    // handle timeout of Wifi connection
    if(TO_flag)
    {
      return TO_WIFI_CONN;  
    }
    // debug
    Serial.println("");
    Serial.println("WiFi connected");  
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP());    
     
    //
  // Use WiFiClient class to create TCP connections
  WiFiClient client;
  
  if (!client.connect(host, httpPort_TxData)) {
    Serial.println("connection failed");
    return TXDATA_CLIENT_FAILED;
  }
  
  // We now create a URI for the request
  String url = "/update.json";
  
  String content = String("api_key=") + api_key + "&" + "field1=" + ADC_value;
  int content_length = content.length();
           
  Serial.print("Requesting URL: ");
  Serial.println(url);
    
  client.print(String("POST ") + url + " HTTP/1.1" + "\r\n" +
  "Host: " + String(host) + "\r\n"
  "Content-Length: " + content_length + "\r\n\r\n" + 
  content + "\r\n\r\n"
  );
  
  unsigned long timeout = millis();
  while (client.available() == 0) {
    if (millis() - timeout > 5000) {
      Serial.println(">>> Client Timeout !");
      client.stop();
      return TXDATA_CLIENT_TO;
    }
  }
  
  // Read all the lines of the reply from server and print them to Serial
  while(client.available()){
    String line = client.readStringUntil('\r');
    Serial.print(line);
  }
  
  Serial.println();
  Serial.println("closing connection");
    
}


void setup() 
{
  pinMode(ESP8266_LED, OUTPUT);
  Serial.begin(115200);
  Serial.println();
  Serial.println("Starting");

  dht.begin();
  
  delay(10);
  
 
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
    int ret_val = SetWiFiAP();
    if(ret_val == E_OK)
    {
      clientState = NextAcquisition;
    }
    else
    {
      // mem the synthom
      curr_diag_synthom = ret_val;
      clientState = Diagnostic;
    }
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
