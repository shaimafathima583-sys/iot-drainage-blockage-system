#include <WiFi.h>
#include <Firebase_ESP_Client.h>

// WiFi
const char* ssid = "Nphn";
const char* pwd  = "";

// Drain identity
#define DRAIN_ID "DRAIN_001"

// Firebase
#define API_KEY "YOUR_API_KEY"
#define DATABASE_URL ""

FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

// Pins
#define TRIG_PIN 5
#define ECHO_PIN 21
#define MQ2_PIN 34
#define IR_PIN 23
#define FLOAT_PIN 19
#define VIB_PIN 4
#define IN1 26
#define IN2 27
#define ENA 25

// Sensor values
float waterLevelCM = 0;
int gasValue = 0;
int vibCountWindow = 0;
int dailyVibrationCount = 0;

// States
int blockageScore = 0;
int healthScore = 100;
String drainState = "Normal";
bool motorActive = false;

// Event counters
int dailyIRCount = 0;
int dailyOverflowCount = 0;
int monthlyOverflowCount = 0;

// Fault flags
bool ultrasonicFault = false;
bool vibrationFault = false;
bool irFault = false;

// Tracking variables
int lastIRState = LOW;
int lastFloatState = LOW;
unsigned long vibWindowStart = 0;
unsigned long lastVibrationTime = 0;
unsigned long lastDayReset = 0;
unsigned long motorRunTimeToday = 0;
unsigned long motorStartTime = 0;

const unsigned long VIB_WINDOW_MS = 10000;
const unsigned long DAY_MS = 86400000;

float totalWaterLevel = 0;
int readingCount = 0;

void setup() {

  Serial.begin(115200);

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(MQ2_PIN, INPUT);
  pinMode(IR_PIN, INPUT);
  pinMode(FLOAT_PIN, INPUT);
  pinMode(VIB_PIN, INPUT);

  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  pinMode(ENA, OUTPUT);

  digitalWrite(IN1, LOW);
  digitalWrite(IN2, LOW);
  digitalWrite(ENA, LOW);

  WiFi.begin(ssid, pwd);
  while (WiFi.status() != WL_CONNECTED) delay(500);

  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;

  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  vibWindowStart = millis();
  lastDayReset = millis();
}

void loop() {

  readSensors();
  trackIR();
  trackOverflow();
  computeVibrationWindow();
  detectFaults();
  computeBlockageScore();
  classifyDrainState();
  controlMotor();
  updateDailyAnalytics();
  handleDashboardCommands();
  logEvents();
  sendLiveData();

  delay(2000);
}

void readSensors() {

  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  long duration = pulseIn(ECHO_PIN, HIGH, 30000);
  waterLevelCM = duration * 0.034 / 2;

  gasValue = analogRead(MQ2_PIN);

  totalWaterLevel += waterLevelCM;
  readingCount++;
}

void trackIR() {

  int currentIR = digitalRead(IR_PIN);

  if (currentIR == HIGH && lastIRState == LOW)
    dailyIRCount++;

  lastIRState = currentIR;
}

void trackOverflow() {

  int currentFloat = digitalRead(FLOAT_PIN);

  if (currentFloat == HIGH && lastFloatState == LOW) {
    dailyOverflowCount++;
    monthlyOverflowCount++;
  }

  lastFloatState = currentFloat;
}

void computeVibrationWindow() {

  if (digitalRead(VIB_PIN) == HIGH) {
    vibCountWindow++;
    dailyVibrationCount++;
    lastVibrationTime = millis();
  }

  if (millis() - vibWindowStart >= VIB_WINDOW_MS) {
    vibCountWindow = 0;
    vibWindowStart = millis();
  }
}

void detectFaults() {

  ultrasonicFault = (waterLevelCM <= 0 || waterLevelCM > 400);

  vibrationFault = (millis() - lastVibrationTime > 60000);

  static int stableIR = 0;
  static int lastIR = -1;
  int currentIR = digitalRead(IR_PIN);

  if (currentIR == lastIR)
    stableIR++;
  else
    stableIR = 0;

  irFault = (stableIR > 50);
  lastIR = currentIR;
}

void computeBlockageScore() {

  blockageScore = 0;

  if (waterLevelCM < 10) blockageScore += 40;
  if (gasValue > 2000) blockageScore += 20;
  if (vibCountWindow > 15) blockageScore += 20;
  if (digitalRead(FLOAT_PIN) == HIGH) blockageScore += 40;

  if (blockageScore > 100) blockageScore = 100;

  healthScore = 100 - blockageScore;
}

void classifyDrainState() {

  if (blockageScore >= 70)
    drainState = "Severe";
  else if (blockageScore >= 40)
    drainState = "Partial";
  else
    drainState = "Normal";
}

void controlMotor() {

  if (drainState == "Severe") {

    if (!motorActive)
      motorStartTime = millis();

    digitalWrite(IN1, HIGH);
    digitalWrite(IN2, LOW);
    digitalWrite(ENA, HIGH);
    motorActive = true;

  } else {

    if (motorActive)
      motorRunTimeToday += millis() - motorStartTime;

    digitalWrite(ENA, LOW);
    motorActive = false;
  }
}

void updateDailyAnalytics() {

  if (millis() - lastDayReset >= DAY_MS) {

    dailyIRCount = 0;
    dailyOverflowCount = 0;
    dailyVibrationCount = 0;
    motorRunTimeToday = 0;
    totalWaterLevel = 0;
    readingCount = 0;

    lastDayReset = millis();
  }
}

void handleDashboardCommands() {

  String path = "/drains/" + String(DRAIN_ID) + "/commands/motor";

  if (Firebase.RTDB.getString(&fbdo, path)) {

    String cmd = fbdo.stringData();

    if (cmd == "ON") {
      digitalWrite(ENA, HIGH);
      motorActive = true;
    }

    if (cmd == "OFF") {
      digitalWrite(ENA, LOW);
      motorActive = false;
    }
  }
}

void logEvents() {

  if (drainState == "Severe") {

    String path = "/drains/" + String(DRAIN_ID) + "/events/latest";
    Firebase.RTDB.setString(&fbdo, path, "Severe blockage detected");
  }
}

void sendLiveData() {

  String base = "/drains/" + String(DRAIN_ID) + "/";

  Firebase.RTDB.setFloat(&fbdo, base + "live/waterLevel", waterLevelCM);
  Firebase.RTDB.setInt(&fbdo, base + "live/gasValue", gasValue);
  Firebase.RTDB.setInt(&fbdo, base + "live/blockageScore", blockageScore);
  Firebase.RTDB.setInt(&fbdo, base + "live/healthScore", healthScore);
  Firebase.RTDB.setString(&fbdo, base + "live/state", drainState);
  Firebase.RTDB.setBool(&fbdo, base + "live/motorActive", motorActive);
  Firebase.RTDB.setInt(&fbdo, base + "live/rssi", WiFi.RSSI());

  Firebase.RTDB.setBool(&fbdo, base + "faults/ultrasonic", ultrasonicFault);
  Firebase.RTDB.setBool(&fbdo, base + "faults/vibration", vibrationFault);
  Firebase.RTDB.setBool(&fbdo, base + "faults/ir", irFault);

  Firebase.RTDB.setInt(&fbdo, base + "daily/irCount", dailyIRCount);
  Firebase.RTDB.setInt(&fbdo, base + "daily/overflowCount", dailyOverflowCount);
  Firebase.RTDB.setInt(&fbdo, base + "daily/motorRunTime_sec", motorRunTimeToday / 1000);

  Firebase.RTDB.setInt(&fbdo, base + "monthly/overflowCount", monthlyOverflowCount);
}