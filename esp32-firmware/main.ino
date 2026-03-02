#include <WiFi.h>
#include <Firebase_ESP_Client.h>

const char* ssid = "Nphn";
const char* pwd = "";

#define DATABASE_URL "https://your-project.firebaseio.com/"

FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

// Sensor pins
#define TRIG_PIN 5
#define ECHO_PIN 21
#define MQ2_PIN 34
#define IR_PIN 23
#define FLOAT_PIN 19
#define VIB_PIN 4
#define IN1 26
#define IN1 27
#define ENA 25

//Global variables
float waterLevelCM; //Ultra value (water level)
int irWasteCount = 0; //IR waste 
int vibCount = 0; //Abnormal vibrations
int gasValue; // Gas values 
bool isOverflowing = false; //Float switch checks the overflow of water
bool motorActive = false; //Motor ON/OFF
int healthScore = 100; //Overall health score


void setup() 
{
  Serial.begin(115200);
  WiFi.begin(ssid, pwd);
  
  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;

  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true)

  initPins();
  initWiFi();
  initFirebase();
  loadNodeMeta();

}

void loop() {
Firebase.RTDB.setInt(&fbdo, "/drains/DRAIN_001/live/ultrasonic", random(10, 50));
  delay(1000);

  // Read sensor values function
  readSensors();
  // check sesnor faults
  detectFaultySensors();
  //Abnormal vibration counts 
  computeVibrationWindow();
  //Health Score
  computeBlockageScore();
  //drain status
  classifyDrainState();
  //Control Motor
  controlMotor();
  //Send live data to firebase 
  sendLiveToFirebase();
  // Log any events
  logEventsIfAny();
  //Dashboard commands 
  handleDashboardCommands();  

  delay(200); //update every 2 seconds 
}


// Read all sensor values
void readSensors(){
  //Ultrasonic 
  digitalWrite(TRIG_PIN,LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN,HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN,LOW);
  delayMicroseconds(2);

  waterLevelCM = pulseIN(ECHO_PIN,HIGH)*0.034/2;

  //MQ2 Sensor
  gasValue = analogRead(MQ2_PIN);

  //IR Object detection
  irWasteCount = digitalRead (IR_PIN);

  //Float switch 
  isOverflowin = digitalRead(FLOAT_PIN);

}

//States the overall health of the drain state
void classifyDrainState(){
  if (healthScore >= 80)
  Serial.println("Drain State healtheir");
else if (healthScore >= 50)
  Serial.println("Dran State Warning");
else 
Serial.println("Drain State critical");

}

//Motor control
void controlMotor(){
  if (healthScore < 50){
    digitalWrite(IN1,HIGH);
    digitalWrite(IN2,LOW);
    digitalWrite(ENA,HIGH);
    motorActive= true;
  }else {
    digitalWrite(IN1,LOW);
    digitalWrite(IN2,LOW);
    digitalWrite(ENA,LOW);
    motorActive= true;
    motorActive=false;
  }

}


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






