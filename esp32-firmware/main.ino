#include <WiFi.h>
#include <Firebase_ESP_Client.h>

const char* ssid = "Nphn";
const char* pwd = "";

#define DATABASE_URL "https://your-project.firebaseio.com/"

FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

void setup() 
{
  Serial.begin(115200);
  WiFi.begin(ssid, pwd);
  
  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;

  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true)

}

void loop() {
Firebase.RTDB.setInt(&fbdo, "/drains/DRAIN_001/live/ultrasonic", random(10, 50));
  delay(1000);
}