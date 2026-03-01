#include <WiFi.h>

const char* ssid = "Nphn";
const char* pwd = "";

void setup() 
{
  Serial.begin(115200);
  WiFi.begin(ssid, pwd);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("WiFi connected");
}

void loop() {}