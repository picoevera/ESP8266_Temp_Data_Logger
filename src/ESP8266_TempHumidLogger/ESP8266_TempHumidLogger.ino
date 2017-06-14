#include <Adafruit_Sensor.h>
#include <DHT.h>
#include <DHT_U.h>
#include <ESP8266WiFi.h>
#include <EEPROM.h>
#include <ESP8266WebServer.h>
extern "C"{
#include "Ext_Structs4EEPROM.h"
}

// DEBUG
#define DEBUG

#ifdef DEBUG
 #define DEBUG_PRINT(x)         Serial.print (x)
 #define DEBUG_PRINTDEC(x)      Serial.print (x, DEC)
 #define DEBUG_PRINTHEX(x)      Serial.print (x, HEX)
 #define DEBUG_PRINTLN(x)       Serial.println (x)
 #define DEBUG_PRINTLNDEC(x)    Serial.println (x, DEC)
 #define DEBUG_PRINTLNHEX(x)    Serial.println (x, HEX)

#else
 #define DEBUG_PRINT(x)
 #define DEBUG_PRINTDEC(x)
 #define DEBUG_PRINTHEX(x)
 #define DEBUG_PRINTLN(x) 
 #define DEBUG_PRINTLNDEC(x)
 #define DEBUG_PRINTLNHEX(x)
#endif


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
//
#define EXT_BUTT_INT  ESP8266_12_GPIO13  

// DHT sensor instance
DHT dht(DHTPIN, DHTTYPE, 15);

// SM states
#define NUM_STATES  6
#define Init  0
#define SetTime       1
#define Idle    2
#define NextAcquisition 3
#define DataTx 4
#define Diagnostic 5

char clientState;
char prevClientState4Diag;

// Diagnostic
// Synthoms
#define E_OK                0
#define TO_WIFI_CONN        1
#define TXDATA_CLIENT_FAILED  2
// Error Types
#define ERR_GET_TIME      0
#define ERR_DATA_TX       1
#define ERR_IS_FULL       2
// Allowed retries
#define N_ALLOWED_RETRY  3
#define N_RETRY_SET_INIT_TIME  N_ALLOWED_RETRY
#define N_RETRY_DATA_TX   N_ALLOWED_RETRY
unsigned char remainingRetry4DataTX;
unsigned char remainingRetry4SetInitTime;
// Flags affecting the SM
boolean errorIsPresent = 0;
char getTimeIsPending = 1; // SM has not been passed though "GetTime" state yet

// Wifi
#define RETRY_DELAY_WIFI_CONN  1000
#define TO_WIFI_CONN_VAL        10000

const char* ssid     = "XXXXX";
const char* password = "XXXXXX";

float tempSamplesArray[NUM_SAMPLES]={0};
float HumidSamplesArray[NUM_SAMPLES];

// Time
unsigned long timeNow = 0;
unsigned long timeLast = 0;
// counters
unsigned long Time2NextAcquisition = 0;
unsigned long Time2NextTx = 0;
#define DELAY_IDLE   10000
#define DATA_TX_TIMER_ELAPSED  300 // 36000 // -> every 10 hours
#define NEXT_ACQUIS_TIMER_ELAPSED 150 // 300 // -> around every 5 mins 
#define DELAY_BETWEEN_SUBSAMPLES     1000

union __data2write data2write;

union __DiagLog2write log2write;


int Delay2NextSample = DELAY2NEXT_SAMPLE;
void reinitializeTime()
{
  data2write.blk2write.timestamp.currSeconds = 0;
  data2write.blk2write.timestamp.currMinutes = 0;
  data2write.blk2write.timestamp.currHours = 0;
  data2write.blk2write.timestamp.currDays = 1;
  data2write.blk2write.timestamp.currMonth = 1;
  data2write.blk2write.timestamp.currYear = 16;
}

// <-----  EEPROM Methods ------>
#define EE_SIZE 512
#define POS_DIAG_FLAG 0
#define POS_LOG_START_BYTE 1
#define POS_LOG_N_ENTRIES  3
#define POS_DATA_START_BYTE 5
#define POS_DATA_N_ENTRIES  7

// Byte 0-> flag ErrorIsPresent 
// Bytes 1-2 StartByte of the Diag Log
// Bytes 3-4 Number of entries in Diag Log
// Bytes 5-6 StartByte of the Data
// Bytes 7-8 Number of entries in Data
// Byte 10 - 205 Data
// Byte 210 - 306 Log Diagnostic
#define FIRST_BYTE4DATA   10
#define FIRST_BYTE4LOG    210
#define SIZE_BYTES4DATA   200
#define SIZE_BYTES4LOG    100
unsigned short int DataEmptyPos; // Position of the first empty byte for data
unsigned short int LogEmptyPos;  // Position of the first empty byte for log
#define DATA_OVERWRITTEN   1
unsigned short int nDataEntriesInEEPROM = 0; // number of entries(data2write) in EEPROM 
unsigned short int nLogEntriesInEEPROM = 0; // number of entries(data2write) in EEPROM 

void resetDataCounters()
{
    DataEmptyPos = FIRST_BYTE4DATA; 
    nDataEntriesInEEPROM = 0;
}

void resetLogCounters()
{
    LogEmptyPos = FIRST_BYTE4LOG; 
    nLogEntriesInEEPROM = 0;
}

void EE_Setup() { 
  EEPROM.begin(EE_SIZE); 
}

void EE_EraseAll() {
  int i;

  for (i = 0; i < EE_SIZE; i++) {
    yield();
    EEPROM.write(i, 0);
  }
  EEPROM.commit();
  // reset counters
  resetDataCounters();
  resetLogCounters();

}

char EE_Data_CheckIfIsFull(unsigned short int len)
{
  char ret_val = E_OK;
  
  // checking for empty space 
  if(SIZE_BYTES4DATA - (DataEmptyPos-FIRST_BYTE4DATA) < len)
  {
      // overwrite the oldest data
      DataEmptyPos = FIRST_BYTE4DATA;  
      ret_val = DATA_OVERWRITTEN;
  }
  // Update the number of entries
  if(nDataEntriesInEEPROM <  (SIZE_BYTES4DATA/SIZE_STRUCT))
  {
    nDataEntriesInEEPROM++;
  }    

  return ret_val;
}

char EE_Log_CheckIfIsFull(unsigned short int len)
{
  char ret_val = E_OK;
  
  // checking for empty space 
  if(SIZE_BYTES4LOG - (LogEmptyPos-FIRST_BYTE4LOG) < len)
  {
      // overwrite the oldest data
      LogEmptyPos = FIRST_BYTE4LOG;  
      ret_val = DATA_OVERWRITTEN;
  }
  // Update the number of entries
  if(nLogEntriesInEEPROM <  (SIZE_BYTES4LOG/LOG_SIZE_STRUCT))
  {
    nLogEntriesInEEPROM++;
  }    

  return ret_val;
}

void EE_StoreData(byte *data, unsigned short int len, unsigned short int *startByte ) {
  unsigned short int i;
  
  for (i = 0; i < len; i++) {
    yield();
    EEPROM.write(i + *startByte, data[i]);
  }
  EEPROM.commit();
  // update first empty pos
  *startByte += len;
}

void EE_LoadData(short int startByte, short int nByte2Read, byte* data) {
  short int i;

  for (i = 0; i < nByte2Read; i++) {
    yield();
    data[i] = EEPROM.read(i+startByte);
  }
}

void resetEEcounters()
{
  // reset counters
  resetDataCounters();
  resetLogCounters();  
}

void setErrorIsPresent(boolean val)
{
   byte byte2write = 0;
   
   // retrieve the current byte in EE
   byte2write = EEPROM.read(POS_DIAG_FLAG);
   if(val)
   {
      // set the bit
      byte2write |= 0x01;
   }
   else
   {
      // reset the bit
      byte2write &= ~0x01;
      // reset the counter??
      // byte2write = 0;
    }
   EEPROM.write(POS_DIAG_FLAG, byte2write);
}

// at the setup, retrieving the DIAG_FLAG. If error is present => increment the counter till 127
boolean getErrAndSetNextCycle()
{
   boolean errIsPresent = 0;
   byte byte2write = 0;
   unsigned char tmpCounter = 0;
   // retrieve the current byte in EE
   byte2write = EEPROM.read(POS_DIAG_FLAG);
   // check the LSB value
   if( (byte2write&0x01)>0)
   {
      errIsPresent = 1;
      // update the counter
      tmpCounter = byte2write>>1;
      tmpCounter++;
      if(tmpCounter <128)
      {
          byte2write = (byte2write&0x01)|tmpCounter<<1;
      }
      // Write the byte
      EEPROM.write(POS_DIAG_FLAG, byte2write);
      // Retrieving from NVM the counters
      // get the first empty byte for the Diag log
      LogEmptyPos = getLogStartByte();  // Position of the first empty byte for log
      // get the current number of entries(log2write) in EEPROM
      nLogEntriesInEEPROM = getLogNentry();
      // get the first empty byte for the Data
      DataEmptyPos = getDataStartByte();
      // get the current number of entries(data2write) in EEPROM
      nDataEntriesInEEPROM = getDataNentry();
      // DEBUG
      DEBUG_PRINTLN("getErrAndSetNextCycle");
      DEBUG_PRINTLN("byte2write - LogEmptyPos - nLogEntriesInEEPROM");
      DEBUG_PRINTHEX(byte2write);
      DEBUG_PRINT("  ");
      DEBUG_PRINTDEC(LogEmptyPos);
      DEBUG_PRINT("  ");
      DEBUG_PRINTLNDEC(nLogEntriesInEEPROM);
      // DUMP
      DEBUG_PRINTLN("DUMP ");
      byte data;
      for(int i_item = 0;i_item<15;i_item++)
      {
        data = EEPROM.read(i_item);
        DEBUG_PRINTLNHEX(data);
      }
      
   }
   return(errIsPresent);      
}

// LOG
void setLogStartByte(unsigned short int value)
{
  unsigned short int startByte = POS_LOG_START_BYTE;
  EE_StoreData((byte*)&value, sizeof(value), &startByte);
}
void setLogNentry(unsigned short int value)
{
  unsigned short int startByte = POS_LOG_N_ENTRIES;
  EE_StoreData((byte*)&value, sizeof(value), &startByte);  
}

unsigned short int getLogStartByte()
{
  unsigned short int LogStartByte;  
  EE_LoadData(POS_LOG_START_BYTE, sizeof(unsigned short int), (byte*)&LogStartByte);
  return LogStartByte;
}
unsigned short int getLogNentry()
{
  unsigned short int LogNentry;  
  EE_LoadData(POS_LOG_N_ENTRIES, sizeof(unsigned short int), (byte*)&LogNentry);
  return LogNentry;
}
// DATA
void setDataStartByte(unsigned short int value)
{
  unsigned short int startByte = POS_DATA_START_BYTE;
  EE_StoreData((byte*)&value, sizeof(value), &startByte);
}
void setDataNentry(unsigned short int value)
{
  unsigned short int startByte = POS_DATA_N_ENTRIES;
  EE_StoreData((byte*)&value, sizeof(value), &startByte);  
}

unsigned short int getDataStartByte()
{
  unsigned short int DataStartByte;  
  EE_LoadData(POS_DATA_START_BYTE, sizeof(unsigned short int), (byte*)&DataStartByte);
  return DataStartByte;
}
unsigned short int getDataNentry()
{
  unsigned short int DataNentry;  
  EE_LoadData(POS_DATA_N_ENTRIES, sizeof(unsigned short int), (byte*)&DataNentry);
  return DataNentry;
}


// <-----  END EEPROM ---->

void InitFunc()
{
 
  int i;
  for(i=0;i<NUM_SAMPLES;i++)
  {
    tempSamplesArray[i] = 0;
    HumidSamplesArray[i] = 0;
  }

  //  
  timeNow = 0;
  timeLast = 0;
  // reset the counters
  Time2NextAcquisition = 0;
  Time2NextTx = 0;
  // 
  reinitializeTime();
  data2write.blk2write.currTempDec=0;
  data2write.blk2write.currHumidity=0;
  data2write.blk2write.currHumidityDec=0;
  //
  prevClientState4Diag = 0;
  remainingRetry4DataTX = N_RETRY_DATA_TX;
  remainingRetry4SetInitTime= N_RETRY_SET_INIT_TIME;

  DEBUG_PRINT("The time is:           ");
  DEBUG_PRINTDEC(data2write.blk2write.timestamp.currDays);
  DEBUG_PRINT(":");
  DEBUG_PRINTDEC(data2write.blk2write.timestamp.currHours);
  DEBUG_PRINT(":");
  DEBUG_PRINTDEC(data2write.blk2write.timestamp.currMinutes);
  DEBUG_PRINT(":"); 
  DEBUG_PRINTLNDEC(data2write.blk2write.timestamp.currSeconds); 
}

void handlingLed(int nBlinks, int timeDuration)
{
   char i_Blink;
    boolean led_stat = 0;
   for(i_Blink =0; i_Blink < nBlinks;i_Blink++)
   {
      digitalWrite(ESP8266_LED, led_stat); 
      led_stat = ~led_stat;
      delay(timeDuration);    
   }  
}

void handlingTime(unsigned long delay_ms)
{
  if(errorIsPresent)
  {
       handlingLed(delay_ms/1000, 1000); 
  }
  else
  {
    delay(delay_ms);
  }  
  // updating the counters  
  Time2NextAcquisition += delay_ms/1000;
  Time2NextTx += delay_ms/1000;
}

volatile boolean inInt = false;

void btnINT()
{
    if(!inInt)  
    {
      inInt = true;      
    }
}

void setup() {
  // put your setup code here, to run once:
  pinMode(ESP8266_LED, OUTPUT);
  Serial.begin(115200);
  
  DEBUG_PRINTLN("Starting");

  dht.begin();
  
  delay(10);
  // EEPROM setup
  EE_Setup();
  // Set State Machine
  clientState = Init;
  //
  attachInterrupt(digitalPinToInterrupt(EXT_BUTT_INT), btnINT, FALLING);

  // Debug Setup
  digitalWrite(ESP8266_LED, HIGH);
  delay(1000);
}

void addEntryInDiagLog(char ret_val, char nRetry, char errType)
{
    byte byte2write = 0;
    unsigned char tmpVal=0;
    // set error flag
    if(errorIsPresent == 0)
    {
        // set the flag once
        errorIsPresent = 1;
        setErrorIsPresent(errorIsPresent);
    }
    // updating the timestamp
    UpdatingSWtimer(&log2write.diagLog.timestamp);
    // Write Err on Log
    EE_Log_CheckIfIsFull(LOG_SIZE_STRUCT);
    // prepare the byte to write
    switch(errType)
    {
      case ERR_GET_TIME:
      //set bits 2
      byte2write |= 0x04;
      break;
      case ERR_DATA_TX:
      //set bits 3
      byte2write |= 0x08;
      break;
      case ERR_IS_FULL:
      // set bits 2 and 3
      byte2write |= 0x0C;
      break;
      default:
      break;  
    }
    // set the number of retries - the first 2 bits of the byte to write
    tmpVal = nRetry;
    if(tmpVal>N_ALLOWED_RETRY)
      tmpVal = N_ALLOWED_RETRY;
    byte2write = (byte2write&0x03)|tmpVal;         
    // set the synthom - the most significant 4 bits
    tmpVal = ret_val;
    byte2write = (byte2write&0x0F)|(tmpVal<<4);         
    // byte2write 0-1 = number of retries, 2 = error on retrieving Time, 3= error on TX the data, 2-3 = error EE is FULL, overwritten, 4-7 = synthoms
    log2write.diagLog.curr_diag_synthom = byte2write;
    // debug
    DEBUG_PRINTLN("addEntryInDiagLog");
    DEBUG_PRINT("LogEmptyPos before ");
    DEBUG_PRINTLNDEC(LogEmptyPos);
    DEBUG_PRINT("nLogEntriesInEEPROM before ");
    DEBUG_PRINTLNDEC(nLogEntriesInEEPROM);
    
    EE_StoreData(log2write.array2write, LOG_SIZE_STRUCT, &LogEmptyPos);
    DEBUG_PRINT("LogEmptyPos after ");
    DEBUG_PRINTLNDEC(LogEmptyPos);
    DEBUG_PRINT("nLogEntriesInEEPROM before ");
    DEBUG_PRINTLNDEC(nLogEntriesInEEPROM);
    // update the counters
    setLogStartByte(LogEmptyPos);
    setLogNentry(nLogEntriesInEEPROM);
}

void Nblinks4ClientState(char clientState)
{
  handlingLed(clientState + 1, 250);
}

void printTimeNow()
{
  DEBUG_PRINTLNDEC(millis()); 
}

void samplesAcquisition()
{
    printTimeNow();
    DEBUG_PRINTLN("  samplesAcquisition");  
    // 8 samples every half min
    boolean Led_Val = 1;
    digitalWrite(ESP8266_LED, Led_Val);
    for(int i=0;i<NUM_SAMPLES;i++)
    {  
      // read the sample
      tempSamplesArray[i] = dht.readTemperature();
      HumidSamplesArray[i] = dht.readHumidity();
      // delay
      handlingTime(DELAY_BETWEEN_SUBSAMPLES);
      //LED-> to remove??
      Led_Val = ~Led_Val;
      digitalWrite(ESP8266_LED, Led_Val);
    } 
}
void closeConnWifi()
{
  WiFi.disconnect();
}

int openConn2WiFi()
{
  int TOcounter = 0;
  boolean TO_flag = 0;
   String response2client;
  
  // Set WiFi to station mode and disconnect from an AP if it was previously connected
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  WiFi.begin(ssid, password);
  handlingTime(100);
    // create the connection
    while (WiFi.status() != WL_CONNECTED && !TO_flag) {
      digitalWrite(ESP8266_LED, HIGH);
       delay(RETRY_DELAY_WIFI_CONN);
      // ESP.wdtFeed();
      digitalWrite(ESP8266_LED, LOW);
      
      //delay_driveLED(RETRY_DELAY_WIFI_CONN, currState);
      DEBUG_PRINT(".");
      TOcounter += RETRY_DELAY_WIFI_CONN;
      if(TOcounter > TO_WIFI_CONN_VAL)
      {
          TO_flag = 1;
      }
    }
    // handle timeout of Wifi connection
    if(TO_flag)
    {
      DEBUG_PRINTLN("WiFi connection - Timeout");  
      return TO_WIFI_CONN;  
    }
    // debug
    DEBUG_PRINTLN("");
    DEBUG_PRINTLN("WiFi connected");  
    DEBUG_PRINTLN("IP address: ");
    DEBUG_PRINTLN(WiFi.localIP());    
    return E_OK; 
}

int connectClient2NIST(WiFiClient *p_client)
{
  const int httpPort = 13;
  // https://www.hackster.io/rayburne/nist-date-amp-time-with-esp8266-and-oled-display-e8b9a9
  const char* host = "time.nist.gov"; // Round-robin DAYTIME protocol

  if (!p_client->connect(host, httpPort)) 
  {
    return TXDATA_CLIENT_FAILED;
  }
  return E_OK;    
}

int getTime()
{
  int ret_val;
  DEBUG_PRINTLN("  getTime ");
  ret_val= openConn2WiFi();
  if(ret_val == E_OK)
  {
     // Use WiFiClient class to create TCP connections
     WiFiClient client2NIST;
      

      if(connectClient2NIST(&client2NIST) != E_OK)
      {
        DEBUG_PRINTLN("connection failed");
        closeConnWifi();
        return TXDATA_CLIENT_FAILED;
      } 
      // This will send the request to the server
      client2NIST.print("HEAD / HTTP/1.1\r\nAccept: */*\r\nUser-Agent: Mozilla/4.0 (compatible; ESP8266 NodeMcu Lua;)\r\n\r\n");

      delay(100); // TO REMOVE
      // Read all the lines of the reply from server and print them to Serial
      // expected line is like : Date: Thu, 01 Jan 2015 22:00:14 GMT
      char buffer[12];
      String TimeDate = "";
      DEBUG_PRINTLN(">  Listening...<");
    
      while(client2NIST.available())
      {
        String line = client2NIST.readStringUntil('\r');
    
        if (line.indexOf("Date") != -1)
        {
          DEBUG_PRINT("=====>");
        } else
        {
          DEBUG_PRINT(line);
          // date starts at pos 7
          TimeDate = line.substring(7);
          DEBUG_PRINTLN(TimeDate);
          // Date
          TimeDate = line.substring(7, 15);
          TimeDate.toCharArray(buffer, 10);
          DEBUG_PRINTLN("UTC Date/Time:");

          DEBUG_PRINTLN("currDays");
          DEBUG_PRINTLN(line.substring(13, 15));
          DEBUG_PRINTLN((line.substring(13, 15)).toInt());
          DEBUG_PRINTLN("currMonth");
          DEBUG_PRINTLN(line.substring(10, 12));
          DEBUG_PRINTLN(line.substring(10, 12));
          DEBUG_PRINTLN("currYear");
          DEBUG_PRINTLN(line.substring(7, 9));
          DEBUG_PRINTLN((line.substring(7, 9)).toInt());
          data2write.blk2write.timestamp.currDays = (line.substring(13, 15)).toInt();            
          data2write.blk2write.timestamp.currMonth = (line.substring(10, 12)).toInt();        
          data2write.blk2write.timestamp.currYear = (line.substring(7, 9)).toInt();
          // time starts at pos 16
          TimeDate = line.substring(16, 24);
          TimeDate.toCharArray(buffer, 10);
          // Date: Thu, 01 Jan 2015 22:00:14 GMT
          // 57751 16-12-29 14:11:31 00 1 0  87.2 UTC(NIST) * 
          DEBUG_PRINTLN("currHours");
          DEBUG_PRINTLN(line.substring(16, 18));
          DEBUG_PRINTLN((line.substring(16, 18)).toInt());
          DEBUG_PRINTLN("currMinutes");
          DEBUG_PRINTLN(line.substring(19, 21));
          DEBUG_PRINTLN((line.substring(19, 21)).toInt());
          DEBUG_PRINTLN("currSeconds");
          DEBUG_PRINTLN(line.substring(22, 24));
          DEBUG_PRINTLN((line.substring(22, 24)).toInt());
         
          data2write.blk2write.timestamp.currHours = (line.substring(16, 18)).toInt();
          data2write.blk2write.timestamp.currMinutes = (line.substring(19, 21)).toInt();
          data2write.blk2write.timestamp.currSeconds = (line.substring(22, 24)).toInt();        
        }
      }
  DEBUG_PRINTLN();
  DEBUG_PRINTLN("closing connection");
  // !! copy date in structure

     closeConnWifi();
     return E_OK;
      
  }
  closeConnWifi();
  return(ret_val);
   
}

unsigned char getNumDaysCurrMonth(unsigned char month, unsigned char year)
{
     unsigned char numDays4Month[] = {0,31,28,31,30,30,31,31,30,31,30,31};
     if(year%4 == 0 && month == 2)
     {
        return (29);
     } 
     return(numDays4Month[month]);
}

  void UpdatingSWtimer(struct _timestampStr  *currTimeStamp)
  {
    unsigned long diff_secs = 0;  
    unsigned long diff_mins = 0;
    unsigned long n_hours = 0; 
    unsigned long n_mins = 0;
    unsigned long n_days = 0;
    unsigned long nDays2EndOfMonth = 0;
    timeNow = millis(); // the number of milliseconds that have passed since boot
  
    diff_secs = (timeNow - timeLast);//the number of seconds that have passed since the last time 60 seconds was reached.
    DEBUG_PRINT("diff_secs ");
    DEBUG_PRINTLNDEC(diff_secs);
    //
    timeLast = timeNow;
    //
    diff_mins = diff_secs/60;
    currTimeStamp->currSeconds += diff_secs - (diff_mins * 60);
    if(currTimeStamp->currSeconds > 60)
    {
        currTimeStamp->currSeconds = currTimeStamp->currSeconds - 60;
        // 
        currTimeStamp->currMinutes += 1;
    }
  
    
    if(diff_mins > 60)
    {
        n_hours = diff_mins/60;
        n_mins = diff_mins - (n_hours*60);
        // 
        currTimeStamp->currMinutes += n_mins;
        //
    }
    else
    {
        currTimeStamp->currMinutes += diff_mins;
    }
  
    //
    if(currTimeStamp->currMinutes > 60)
    {
        currTimeStamp->currMinutes = currTimeStamp->currMinutes - 60;
        // 
        currTimeStamp->currHours += 1;
      
    }
  
    if(n_hours > 24)
    {
        n_days = n_hours/24;
       
        currTimeStamp->currHours += n_hours - (n_days *24);
        //
    }
    else
    {
        currTimeStamp->currHours += n_hours;
    }

    if(currTimeStamp->currHours > 24)
    {
       currTimeStamp->currHours = currTimeStamp->currHours - 24;
       //
       n_days += 1; 
    }
    
    while(n_days > 0)  
    {      
         nDays2EndOfMonth = getNumDaysCurrMonth(currTimeStamp->currMonth, currTimeStamp->currYear)- currTimeStamp->currDays;
        if(nDays2EndOfMonth <= n_days)
        {
            currTimeStamp->currDays = 0;
            // decrease the counter of days
            n_days -= nDays2EndOfMonth;
            // increase the month
            currTimeStamp->currMonth += 1;
            if(currTimeStamp->currMonth > 12)
            {
                currTimeStamp->currMonth = 1;
                //
                currTimeStamp->currYear += 1;
            }
        }
        else
        {
           currTimeStamp->currDays += n_days;
           n_days = 0; 
        }
    }
      
    DEBUG_PRINT("The time is:           ");
  DEBUG_PRINTDEC(currTimeStamp->currYear);
  DEBUG_PRINT(":");
  DEBUG_PRINTDEC(currTimeStamp->currMonth);
  DEBUG_PRINT(":");
  DEBUG_PRINTDEC(currTimeStamp->currDays);
  DEBUG_PRINT(":");
  DEBUG_PRINTDEC(currTimeStamp->currHours);
  DEBUG_PRINT(":");
  DEBUG_PRINTDEC(currTimeStamp->currMinutes);
  DEBUG_PRINT(":"); 
  DEBUG_PRINTLNDEC(currTimeStamp->currSeconds);   
  }


// average
void calcAverageAndAppend()
{
    printTimeNow();
    DEBUG_PRINTLN("  calcAverageAndAppend");  
    int index;
    double tempVal,humVal;
    char ret_val;
    tempVal = humVal = 0;
    // temperature
    for(index=0;index<NUM_SAMPLES;index++)
    {
      tempVal +=  tempSamplesArray[index];  
      humVal += HumidSamplesArray[index];
    }
    // calculating the average
    tempVal = tempVal/NUM_SAMPLES;
    humVal = humVal/NUM_SAMPLES;
    // retriving the integer part
    // temperature   
    data2write.blk2write.currTemp = (unsigned char)((int)tempVal);
    // humidity
    data2write.blk2write.currHumidity = (unsigned char)((int)humVal);
    // retriving the fractional part  
    // temperature
    tempVal = tempVal - data2write.blk2write.currTemp;
    data2write.blk2write.currTempDec = (unsigned char)((int)(tempVal*100));
    // humidity
    humVal = humVal - data2write.blk2write.currHumidity;
    data2write.blk2write.currHumidityDec = (unsigned char)((int)(humVal*100));
    // WRITE TO EEPROM
    if((ret_val = EE_Data_CheckIfIsFull(SIZE_STRUCT)) != E_OK)
    {
        addEntryInDiagLog(ret_val, 1, ERR_IS_FULL);  
        //
        setErrorIsPresent(errorIsPresent);                
    }
    // debug only 
    DEBUG_PRINTLN("EE_StoreData - ARRAY ");
    for(index=0;index<SIZE_STRUCT;index++)
    {
        DEBUG_PRINTLNHEX(data2write.array2write[index]);
    }
    DEBUG_PRINTLN(" EE_StoreData - STRUCT  ");
    DEBUG_PRINTDEC(data2write.blk2write.timestamp.currYear);
    DEBUG_PRINT(":");
    DEBUG_PRINTDEC(data2write.blk2write.timestamp.currMonth);
    DEBUG_PRINT(":");
    DEBUG_PRINTDEC(data2write.blk2write.timestamp.currDays);
    DEBUG_PRINT(":");
    DEBUG_PRINTDEC(data2write.blk2write.timestamp.currHours);
    DEBUG_PRINT(":");
    DEBUG_PRINTDEC(data2write.blk2write.timestamp.currMinutes);
    DEBUG_PRINT(":"); 
    DEBUG_PRINTLNDEC(data2write.blk2write.timestamp.currSeconds); 
    DEBUG_PRINTDEC(data2write.blk2write.currTemp);
    DEBUG_PRINT(",");
    DEBUG_PRINTLNDEC(data2write.blk2write.currTempDec);
    DEBUG_PRINTDEC(data2write.blk2write.currHumidity);
    DEBUG_PRINT(",");
    DEBUG_PRINTLNDEC(data2write.blk2write.currHumidityDec);
    DEBUG_PRINTLN("DataEmptyPos -  nDataEntriesInEEPROM ");
    EE_StoreData(data2write.array2write, SIZE_STRUCT, &DataEmptyPos);
    DEBUG_PRINTLNDEC(DataEmptyPos);
    DEBUG_PRINTLNDEC(nDataEntriesInEEPROM);
}

int TxData2IOT()
{
    const int httpPort = 80;
    const char* host = "api.pushingbox.com"; 

    printTimeNow();
    DEBUG_PRINTLN("  errDataTX E_OK");
    // Blinking LED -> trying the connection
    int ret_val;
    
    // Open WiFi
    ret_val= openConn2WiFi();
    if(ret_val == E_OK)
    {    
       // Open Client
       // Use WiFiClient class to create TCP connections
       WiFiClient client2IOT;
  
        if (!client2IOT.connect(host, httpPort)) {
          Serial.println("connection failed");
          closeConnWifi();
          return TXDATA_CLIENT_FAILED;
        } 


    
      String getmsg;
      String currTemp;
      String currHum;
      union __data2write sample2write;
      union __DiagLog2write currLog2write;
      // Read data from EEPROM
      int i_item;
      // DUMP for DEBUG
      DEBUG_PRINTLN("DUMP ");
      byte data;
      for(i_item = 0;i_item<25;i_item++)
      {
        data = EEPROM.read(i_item);
        DEBUG_PRINTLNHEX(data);
      }
      /////////////////
      
      for(i_item = 0;i_item<nDataEntriesInEEPROM;i_item++)
      {
        EE_LoadData(FIRST_BYTE4DATA + (i_item*SIZE_STRUCT), SIZE_STRUCT, sample2write.array2write);
            // debug only 
          DEBUG_PRINTLN("EE_StoreData - ARRAY ");
          for(int index=0;index<SIZE_STRUCT;index++)
          {
              DEBUG_PRINTLNHEX(sample2write.array2write[index]);
          }
        
        // This will send the request to the server
  
        //http://api.pushingbox.com/pushingbox?devid=v6F177361DA38A29&humidityData=33&celData=44&fehrData=111&hicData=22&hifData=77
  
        currTemp = String(sample2write.blk2write.currTemp) + "," + String(sample2write.blk2write.currTempDec);
        currHum = String(sample2write.blk2write.currHumidity) + "," + String(sample2write.blk2write.currHumidityDec);
        getmsg = "";
        getmsg =  "GET /pushingbox?devid=v6F177361DA38A29&humidityData=";
        getmsg += String(sample2write.blk2write.timestamp.currDays)+"/"+ String(sample2write.blk2write.timestamp.currMonth)+ "/" + String(sample2write.blk2write.timestamp.currYear);
        getmsg += "&celData=" + String(000000000000000000);
        getmsg += "&fehrData=" + String(sample2write.blk2write.timestamp.currHours)+":"+ String(sample2write.blk2write.timestamp.currMinutes) +":"+String(sample2write.blk2write.timestamp.currSeconds);
        getmsg += "&hicData=" + currTemp;
        getmsg += "&hifData=" + currHum + " HTTP/1.1";
        // TX Data      
        client2IOT.println(getmsg.c_str());
        client2IOT.println("Host: api.pushingbox.com");
        client2IOT.println("Connection: close");
        client2IOT.println();
        DEBUG_PRINTLN(getmsg.c_str());
        DEBUG_PRINTLN("Host: api.pushingbox.com");      
        handlingTime(100);    
     }

    for(i_item = 0;i_item<nLogEntriesInEEPROM;i_item++)
    {
      EE_LoadData(FIRST_BYTE4LOG+(i_item*LOG_SIZE_STRUCT), LOG_SIZE_STRUCT, currLog2write.array2write);
      
      // This will send the request to the server

      //http://api.pushingbox.com/pushingbox?devid=v6F177361DA38A29&humidityData=33&celData=44&fehrData=111&hicData=22&hifData=77
        getmsg = " LOG  ";

        getmsg += String(currLog2write.diagLog.timestamp.currDays)+"/"+ String(currLog2write.diagLog.timestamp.currMonth)+ "/" + String(currLog2write.diagLog.timestamp.currYear);
        getmsg += "&celData=" + String(000000000000000000);
        getmsg += "&fehrData=" + String(currLog2write.diagLog.timestamp.currHours)+":"+ String(currLog2write.diagLog.timestamp.currMinutes) +":"+String(currLog2write.diagLog.timestamp.currSeconds);
        getmsg += "&hicData=" + currLog2write.diagLog.curr_diag_synthom;
        // TX Data      
        client2IOT.println(getmsg.c_str());
        client2IOT.println("Host: api.pushingbox.com");
        client2IOT.println("Connection: close");
        client2IOT.println();
        DEBUG_PRINTLN(getmsg.c_str());
        DEBUG_PRINTLN("Host: api.pushingbox.com");      
        handlingTime(100);    
    }
    // Empty the EEPROM
    EE_EraseAll();
    closeConnWifi();

    return E_OK;
  }
  closeConnWifi();
  return(ret_val);    
}


void loop() 
{
  static int ret_val;
  if(inInt)
  {
    DEBUG_PRINTLN("ExtButtonDebouncing");
    inInt = false;
    // disabling interrupt
    noInterrupts();
    if (digitalRead(EXT_BUTT_INT) == LOW){
        delay(20);
        if (digitalRead(EXT_BUTT_INT) == LOW){
            DEBUG_PRINTLN("button_state = PRESSED");
                
            clientState = DataTx;
         }
    }
    // re-enabling interrupt
    interrupts();
  }
  
  switch(clientState)
  {
    case Init:
    DEBUG_PRINTLN("InitTime");
    // Blinks the LED
    Nblinks4ClientState(clientState);
    // reinitialization structures and counters  
    InitFunc();
    // Handling the SM flags
    getTimeIsPending = 1;  
    errorIsPresent = getErrAndSetNextCycle();
    DEBUG_PRINT("setup - errorIsPresent");
    DEBUG_PRINTLNDEC(errorIsPresent);
    if(errorIsPresent == 0)
    {
        // reset the EE
        EE_EraseAll();
        // 
        clientState = SetTime;        
    }
    else
    {
        clientState = DataTx;
    }
    break;
    case SetTime:
      DEBUG_PRINTLN("SetTime");    
      // Blinks the LED
      Nblinks4ClientState(clientState);
      // disabling interrupt
      noInterrupts();
      
      // set the flag
      getTimeIsPending = 0;
      // try connecting to Time 
      ret_val = getTime();
      if(ret_val == E_OK)
      {
        // 
        clientState = NextAcquisition;
      }
      else
      {
        addEntryInDiagLog(ret_val, remainingRetry4SetInitTime, ERR_GET_TIME);
        //
        prevClientState4Diag = SetTime;
        //
        clientState = Diagnostic;
      }
      interrupts();
    break;
    case Idle:
      DEBUG_PRINTLN("Idle");    
      // Blinks the LED
      Nblinks4ClientState(clientState);
      handlingTime(DELAY_IDLE);
      if(Time2NextTx > DATA_TX_TIMER_ELAPSED)
      {
          // TX data to cloud
          clientState = DataTx;
          // reset the counter
          Time2NextTx = 0;
      }
      if(Time2NextAcquisition > NEXT_ACQUIS_TIMER_ELAPSED)
      {
          // next acquisition
          clientState = NextAcquisition;
          // reset the counter
          Time2NextAcquisition = 0;           
      }
    break;
    case NextAcquisition:
      DEBUG_PRINTLN("NextAcquisition");    
      // Blinks the LED
      Nblinks4ClientState(clientState);
      // implementing the strategy
      samplesAcquisition();
      //
      UpdatingSWtimer(&(data2write.blk2write.timestamp));
      // average
      calcAverageAndAppend();
      // next 
      clientState = Idle;
    break;
    case DataTx:
      DEBUG_PRINTLN("DataTx");    
      // Blinks the LED
      Nblinks4ClientState(clientState);
      // disabling interrupt
      noInterrupts();

      // search for AP_Master
      ret_val = TxData2IOT();
      if(ret_val == E_OK)
      {
        clientState = Idle;
      } 
      else 
      {
        addEntryInDiagLog(ret_val, remainingRetry4DataTX, ERR_DATA_TX);
        //
        prevClientState4Diag = DataTx;
        clientState = Diagnostic;
      }
      interrupts();
    break;
    case Diagnostic:
      DEBUG_PRINTLN("Diagnostic");    
      // Blinks the LED
      Nblinks4ClientState(clientState);    
      // Copy CODE in EEPROM 
      DEBUG_PRINTLN("Diag");
      switch(prevClientState4Diag)
      {
          case SetTime:
          remainingRetry4SetInitTime--;
          if(remainingRetry4SetInitTime > 0)
          {
              clientState = SetTime;
              //        
          }
          else
          {
              clientState = Idle;
              // fast blink for 3 seconds
              handlingLed(30, 100);
              remainingRetry4SetInitTime = N_RETRY_SET_INIT_TIME;
              //
//              errorIsPresent = 1;
//              setErrorIsPresent(errorIsPresent);
              // to remove
          }
          break;
          case DataTx:
          remainingRetry4DataTX--;
          if(remainingRetry4DataTX > 0)
          {
              clientState = DataTx;
          }
          else
          {
              // fast blink for 3 seconds
              handlingLed(30, 100);
              if(getTimeIsPending)
              {
                  clientState = SetTime; 
              }
              else
              {
                clientState = Idle;
              }
              // reset the timer
              remainingRetry4DataTX = N_RETRY_DATA_TX;
              // Store in NVM the StartByte and the N_Entries of Data
              setDataStartByte(DataEmptyPos);
              setDataNentry(nDataEntriesInEEPROM);
              //
              errorIsPresent = 1;
              setErrorIsPresent(errorIsPresent);
          }
        break;
        default:
            clientState = Idle;
        break;  
    }
    break;
    default:
    break;
  }
  delay(100);
}

