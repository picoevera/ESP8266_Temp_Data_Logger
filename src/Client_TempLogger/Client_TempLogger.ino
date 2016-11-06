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
#define DELAY2NEXT_SAMPLE 10

#define ESP8266_LED  ESP8266_12_GPIO5 
#define DHTPIN ESP8266_12_GPIO12
#define DHTTYPE DHT22

// DHT sensor instance
DHT dht(DHTPIN, DHTTYPE, 15);

// SM states
#define NUM_STATES  6
#define InitTime  0
#define SetInitTime       1
#define Idle    2
#define NextAcquisition 3
#define DataTx 4
#define Diagnostic 5

char clientState;
char isDebug = 1;
//EEPROM
int currPos = 0;
int nItemsEEPROM = 0;

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

unsigned long long Delay2NextState[NUM_STATES] = {10,1000,1000,1000,1000,1000}; // expressed in millisec
float tempSamplesArray[NUM_SAMPLES]={0};
float HumidSamplesArray[NUM_SAMPLES];
int currIndexSamples = 0;


// Time
unsigned long timeNow = 0;
unsigned long timeLast = 0;
unsigned long lastAcquisition = 0;
unsigned long lastTx = 0;

#define SIZE_STRUCT 10
struct _time_data
{
  char currSeconds;
  char currMinutes;
  char currHours ;
  char currDays;
  char currTemp;
  char currTempDec;
  char currHumidity;
  char currHumidityDec;
};

union __data2write
{
    struct _time_data blk2write;
    char array2write[SIZE_STRUCT];
};

union __data2write data2write;

int Delay2NextSample = DELAY2NEXT_SAMPLE;



//void debugPrint(String str) {
//  if(isDebug)
//    Serial.print(str);
//}

/* Just a little test message.  Go to http://192.168.4.1 in a web browser
 * connected to this access point to see it.
 */

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

  currPos = 0;
  nItemsEEPROM = 0;
  
  timeNow = 0;
  timeLast = 0;
  data2write.blk2write.currSeconds = 0;
  data2write.blk2write.currMinutes = 0;
  data2write.blk2write.currHours = 0;
  data2write.blk2write.currDays = 0;
  data2write.blk2write.currTemp=0;
  data2write.blk2write.currTempDec=0;
  data2write.blk2write.currHumidity=0;
  data2write.blk2write.currHumidityDec=0;
}


int samplesAcquisition()
{
  // 8 samples every half min
  boolean Led_Val = 1;
  digitalWrite(ESP8266_LED, Led_Val);
  for(int i=0;i<NUM_SAMPLES;i++)
  {  
    // read the sample
    tempSamplesArray[i] = dht.readTemperature();
    HumidSamplesArray[i] = dht.readHumidity();
    // delay
    handlingTime(Delay2NextSample,Delay2NextState[Delay2NextSample]);
    //LED
    Led_Val = ~Led_Val;
    digitalWrite(ESP8266_LED, Led_Val);
     
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
       currPos ++;
  }
  nItemsEEPROM ++;
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

int handlingTime(int currState, unsigned long delay_ms)
{
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
  
 
  EEPROM.begin(512);
  // state machine
  clientState = 0;
}

int closeConnWifi()
{
  WiFi.disconnect();
}

void delay_driveLED(int curr_delay, int currState)
{
  //if(currState == 
  handlingTime(0, curr_delay);
}


int openConn2WiFi(int currState)
{
  const char* ssid     = "AndroidAP_MARCO";
  const char* password = "";
  int TOcounter = 0;
  boolean TO_flag = 0;
  const int httpPort_AP = 2357;
   String response2client;
  
  // Set WiFi to station mode and disconnect from an AP if it was previously connected
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  WiFi.begin(ssid, password);
  handlingTime(0,100);

    // create the connection
    while (WiFi.status() != WL_CONNECTED && !TO_flag) {
      
      delay_driveLED(RETRY_DELAY_WIFI_CONN, currState);
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
    return E_OK; 
}

int connectClient2NIST(WiFiClient *p_client)
{
  const int httpPort = 13;

  if (!p_client->connect(host, httpPort)) {
    Serial.println("connection failed");
    return TXDATA_CLIENT_FAILED;
  }
      
}

int getTime(int currState)
{
  // https://www.hackster.io/rayburne/nist-date-amp-time-with-esp8266-and-oled-display-e8b9a9
  const char* host = "time.nist.gov"; // Round-robin DAYTIME protocol
  int ret_val;

  ret_val= openConn2WiFi(currState);
  if(ret_val == E_OK)
  {
     // Use WiFiClient class to create TCP connections
     WiFiClient client2NIST;
      const int httpPort = 13;

      if (!client2NIST.connect(host, httpPort)) {
        Serial.println("connection failed");
        closeConnWifi();
        return TXDATA_CLIENT_FAILED;
      } 
      // This will send the request to the server
      client2NIST.print("HEAD / HTTP/1.1\r\nAccept: */*\r\nUser-Agent: Mozilla/4.0 (compatible; ESP8266 NodeMcu Lua;)\r\n\r\n");

      handlingTime(0,100);
    
      // Read all the lines of the reply from server and print them to Serial
      // expected line is like : Date: Thu, 01 Jan 2015 22:00:14 GMT
      char buffer[12];
      String TimeDate = "";
      Serial.print(">  Listening...<");
    
      while(client2NIST.available())
      {
        String line = client2NIST.readStringUntil('\r');
    
        if (line.indexOf("Date") != -1)
        {
          Serial.print("=====>");
        } else
        {
          Serial.print(line);
          // date starts at pos 7
          TimeDate = line.substring(7);
          Serial.println(TimeDate);
          // time starts at pos 14
          TimeDate = line.substring(7, 15);
          TimeDate.toCharArray(buffer, 10);
          Serial.println("UTC Date/Time:");
          TimeDate = line.substring(16, 24);
          TimeDate.toCharArray(buffer, 10);

          
        }
      }
  Serial.println();
  Serial.println("closing connection");
  // !! copy date in structure

     closeConnWifi();
     return E_OK;
      
  }
  closeConnWifi();
  return(ret_val);
   
}

void setEmptyEEPROM()
{
    // Empty the EEPROM  
}


int TxData2IOT(int currState)
{
    // Blinking LED -> trying the connection
  int ret_val;
    
    // Open WiFi
  ret_val= openConn2WiFi(currState);
  if(ret_val == E_OK)
  {    
    // Open Client
     // Use WiFiClient class to create TCP connections
     WiFiClient client2IOT;
      const int httpPort = 80;
      const char* host = "api.pushingbox.com"; 

      if (!client2IOT.connect(host, httpPort)) {
        Serial.println("connection failed");
        closeConnWifi();
        return TXDATA_CLIENT_FAILED;
      } 
      String getmsg;
      String currTemp;
      String currHum;
      union __data2write sample2write;
      // Read data from EEPROM
      int i_item, i_byte;
      for(i_item = 0;i_item<nItemsEEPROM;i_item++)
      {
        for(i_byte=0; i_byte<SIZE_STRUCT; i_byte++)
        {
          sample2write.array2write[i_byte] = (char)EEPROM.read(i_byte + (i_item*SIZE_STRUCT));
        }
        
      // This will send the request to the server

      //http://api.pushingbox.com/pushingbox?devid=v6F177361DA38A29&humidityData=33&celData=44&fehrData=111&hicData=22&hifData=77

      currTemp = String(sample2write.blk2write.currTemp) + "," + String(sample2write.blk2write.currTempDec);
      currHum = String(sample2write.blk2write.currHumidity) + "," + String(sample2write.blk2write.currHumidityDec);
      getmsg = "";
      getmsg =  "GET /pushingbox?devid=v6F177361DA38A29&humidityData=";
      getmsg += String(sample2write.blk2write.currSeconds);
      getmsg += "&celData=" + String(sample2write.blk2write.currMinutes);
      getmsg += "&fehrData=" + String(sample2write.blk2write.currHours);
      getmsg += "&hicData=" + currTemp;
      getmsg += "&hifData=" + currHum + " HTTP/1.1";
      //sprintf(postmsg,"GET /pushingbox?devid=v6F177361DA38A29&humidityData=%d&celData=%d&fehrData=%d&hicData=%f&hifData=%f HTTP/1.1",data2write.blk2write.currSeconds, data2write.blk2write.currMinutes, data2write.blk2write.currHours, String.toFloat(currTemp),String.toFloat(currHum)); // NOTE** In this line of code you can see where the temperature value is inserted into the wed address. It follows 'status=' Change that value to whatever you want to post.
      client2IOT.println(getmsg.c_str());
      client2IOT.println("Host: api.pushingbox.com");
      client2IOT.println("Connection: close");
      client2IOT.println();
      Serial.println(getmsg.c_str());
      Serial.println("Host: api.pushingbox.com");      
      handlingTime(0,100);

      
      
      }
    


    // TX Data 
    
    // Empty EEPROM
    setEmptyEEPROM();

    closeConnWifi();

    return E_OK;
  }
  closeConnWifi();
  return(ret_val);    
}


void loop() 
{
  static int ret_val;
  switch(clientState)
  {
    case InitTime:
    // reinitialization structures and counters  
    reinit();
    clientState = SetInitTime;
    break;
    case SetInitTime:
    // switch on the led
    digitalWrite(ESP8266_LED, HIGH);
    // try connecting to Time 
    ret_val = getTime(clientState);
    if(ret_val == E_OK)
    {
      // switch off the LED
      digitalWrite(ESP8266_LED, LOW);
      // 
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
       if(timeNow - lastTx >= Delay2NextState[DataTx])
       {
           // if Time to Communicate
           clientState = DataTx;
       }
       else
       {    
          // else
          handlingTime(Idle,Delay2NextState[Idle]);
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
    case DataTx:
    // search for AP_Master
    ret_val = TxData2IOT(clientState);
    if(ret_val == E_OK)
    {
      clientState = Idle;
    } 
    else 
    {
      // mem the synthom
      curr_diag_synthom = ret_val;
      clientState = Diagnostic;
    }
    break;
    case Diagnostic:
    // Copy CODE in EEPROM 
    Serial.println("Diag");
    // if MAN = reinit 
    clientState = InitTime;
    // If MAN = TimeOut_Client
    
    // If MAN = 
    
    break;
    default:
    break;
  }
}
