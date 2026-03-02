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

//Main Loop Functions

float readUltrasonic() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  long duration = pulseIn(ECHO_PIN, HIGH, 30000);
  return duration * 0.034 / 2;
}
// Read all sensor values


//Count vibration hits in time window


// Check if any sensor is not working
void detectFaultySensors() {

  ultrasonicFault = (waterLevel <= 0 || waterLevel > 400);

  if (vibrationCount == 0) {
    if (millis() - lastVibrationTime > 60000)
      vibrationFault = true;
  } else {
    vibrationFault = false;
    lastVibrationTime = millis();
  }

  static int stableCount = 0;
  static int lastIR = -1;

  if (irValue == lastIR)
    stableCount++;
  else
    stableCount = 0;

  irFault = (stableCount > 50);
  lastIR = irValue;

  Firebase.RTDB.setBool(&fbdo, "/" + drainID + "/faults/ultrasonic", ultrasonicFault);
  Firebase.RTDB.setBool(&fbdo, "/" + drainID + "/faults/vibration", vibrationFault);
  Firebase.RTDB.setBool(&fbdo, "/" + drainID + "/faults/ir", irFault);
}

//Calculate blockage severity score
void computeBlockageScore() {
  blockageScore = 0;

  if (waterLevel < 10) blockageScore += 40;
  if (irValue == HIGH) blockageScore += 30;
  if (vibrationCount < 5) blockageScore += 30;
}

//Decide state (Normal / Partial / Severe)


//Turn motor ON/OFF based on condition


//Send live data to Firebase
void sendLiveToFirebase() {

  Firebase.RTDB.setFloat(&fbdo, "/" + drainID + "/live/waterLevel", waterLevel);
  Firebase.RTDB.setInt(&fbdo, "/" + drainID + "/live/vibrationCount", vibrationCount);
  Firebase.RTDB.setInt(&fbdo, "/" + drainID + "/live/blockageScore", blockageScore);
  Firebase.RTDB.setString(&fbdo, "/" + drainID + "/live/state", drainState);
}

//Save events (overflow, blockage, gas alert)
void logEventsIfAny() {

  if (drainState == "Severe") {
    Firebase.RTDB.setString(&fbdo, "/" + drainID + "/events/latest", "Severe Blockage Detected");
  }
}

// Update daily analytics data
void updateDailyStats() {

  totalWaterLevel += waterLevel;
  readingCount++;

  float avgLevel = totalWaterLevel / readingCount;

  Firebase.RTDB.setInt(&fbdo, "/" + drainID + "/daily/totalBlockages", totalBlockagesToday);
  Firebase.RTDB.setInt(&fbdo, "/" + drainID + "/daily/severeBlockages", severeBlockagesToday);
  Firebase.RTDB.setFloat(&fbdo, "/" + drainID + "/daily/avgWaterLevel", avgLevel);
  Firebase.RTDB.setFloat(&fbdo, "/" + drainID + "/daily/motorRunTime_sec", motorRunTimeToday / 1000);

  if (millis() - lastDayReset > 86400000) {
    totalBlockagesToday = 0;
    severeBlockagesToday = 0;
    motorRunTimeToday = 0;
    totalWaterLevel = 0;
    readingCount = 0;
    lastDayReset = millis();
  }
}

//Receive control commands from dashboard
void handleDashboardCommands() {
  if (Firebase.RTDB.getString(&fbdo, "/" + drainID + "/commands/motor")) {

    String cmd = fbdo.stringData();

    if (cmd == "ON")
      digitalWrite(MOTOR_PIN, HIGH);
    else if (cmd == "OFF")
      digitalWrite(MOTOR_PIN, LOW);
  }
}

//SETUP
void setup() {
  Serial.begin(115200);

  initPins();
  initWiFi();
  initFirebase();
  loadNodeMeta();
}

// LOOP
void loop() {

  readSensors();
  computeVibrationWindow();
  detectFaultySensors();
  computeBlockageScore();
  classifyDrainState();
  controlMotor();
  sendLiveToFirebase();
  logEventsIfAny();
  updateDailyStats();
  handleDashboardCommands();

  delay(5000);
}




