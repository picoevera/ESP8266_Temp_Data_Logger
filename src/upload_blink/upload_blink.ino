#include "DHT.h"

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


#define ESP8266_LED  ESP8266_12_GPIO5 
#define DHTPIN ESP8266_12_GPIO12
#define DHTTYPE DHT22

// DHT sensor instance
DHT dht(DHTPIN, DHTTYPE, 15);

void setup() 
{
  pinMode(ESP8266_LED, OUTPUT);
  Serial.begin(115200);
  Serial.println();
  Serial.println("Starting");

  dht.begin();
}

void loop() 
{
  Serial.println("ON");
  digitalWrite(ESP8266_LED, HIGH);
  delay(500);
  Serial.println("OFF");
  
  digitalWrite(ESP8266_LED, LOW);
  delay(500);
  int humidity_data = (int)dht.readHumidity();
  int temperature_data = (int)dht.readTemperature();

  Serial.println("temp");
  Serial.println(temperature_data);
  Serial.println("humidity");
  Serial.println(humidity_data);
  
 delay(5000);
}
