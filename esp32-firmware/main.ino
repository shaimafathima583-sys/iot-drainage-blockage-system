#include <WiFi.h>
#include <Firebase_ESP_Client.h>

// WiFi
const char* ssid = "Nphn";
const char* pwd  = "Twinkle12259";

// Drain identity
#define DRAIN_ID "DRAIN_001"

// Firebase
#define API_KEY "AIzaSyCeEBw9_xuIz0D7XyMIK06rkmATL-wmIm4"
#define DATABASE_URL "https://drainwatch-caca8-default-rtdb.asia-southeast1.firebasedatabase.app/"
#define USER_EMAIL "nabseee15f@gmail.com"
#define USER_PWD "asdfghjkl123"


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

unsigned long lastMonthReset = 0;
const unsigned long MONTH_MS = 30UL * DAY_MS; // approx 30 days


//Timer 
unsigned long sendDataPrevMillis=0;

float totalWaterLevel = 0;
int readingCount = 0;

void setup() {

  Serial.begin(115200);

  initPins();
  initWiFi();


  // ===== FIREBASE CONFIG =====
  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;

  auth.user.email = USER_EMAIL;
  auth.user.password = USER_PWD;

  Firebase.reconnectNetwork(true);
  fbdo.setResponseSize(2048);

  Firebase.begin(&config, &auth);
  Firebase.setDoubleDigits(5);

  config.timeout.serverResponse = 10 * 1000;

}

void loop() {

  if (Firebase.ready() &&
      (millis() - sendDataPrevMillis > 2000 || sendDataPrevMillis == 0)) {

    sendDataPrevMillis = millis();

  readSensors();
  trackIR();
  trackOverflow();
  computeVibrationWindow();
  detectFaults();
  computeBlockageScore();
  classifyDrainState();
  controlMotor();
  updateDailyAnalytics();
  updateMonthlyAnalytics();
  handleDashboardCommands();
  logEvents();
  sendLiveData();

  delay(2000);
      }
}
void initPins(){
  
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(MQ2_PIN, INPUT);
  pinMode(IR_PIN, INPUT);
  pinMode(FLOAT_PIN, INPUT_PULLUP);
  pinMode(VIB_PIN, INPUT);

  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  pinMode(ENA, OUTPUT);

  digitalWrite(IN1, LOW);
  digitalWrite(IN2, LOW);
  digitalWrite(ENA, LOW);
}

void initWiFi(){
  // Connect to WiFi
  Serial.print("Connecting to WiFi");
  WiFi.begin(ssid, pwd);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.print("Connected IP Address: ");
  Serial.println(WiFi.localIP()); // IP address allows the system to send data to Firebase.
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
  readingCount++; //All read sensors should be here readings, store it to a variable
}

void trackIR() {
 // how many objects passes through
  int currentIR = digitalRead(IR_PIN); //stores the current reading to currentIR

  if (currentIR == HIGH && lastIRState == LOW)
    dailyIRCount++;

  lastIRState = currentIR;
}

void trackOverflow() {

  int currentFloat = digitalRead(FLOAT_PIN); //input pulldown missing 

  if (currentFloat == HIGH && lastFloatState == LOW) {
    dailyOverflowCount++;
    monthlyOverflowCount++;
  }

  lastFloatState = currentFloat;
}

void computeVibrationWindow() {

  if (digitalRead(VIB_PIN) == HIGH) { //simplyfy this logic
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

  vibrationFault = (millis() - lastVibrationTime > 60000); //current time - last vibration detected

  //The vibration sensor has not detected any vibration for 1 minute, so it might be not working.
  //static means the value is remembered between loops.
  static int stableIR = 0;
  static int lastIR = -1;
  int currentIR = digitalRead(IR_PIN);

  if (currentIR == lastIR)
    stableIR++;
  else
    stableIR = 0;

  irFault = (stableIR > 50);
  lastIR = currentIR; //simplify
}

void computeBlockageScore() {

  //The blockage score is used to combine multiple sensor readings into one simple number that represents how blocked the drain is.
  //Instead of checking many sensors separately, the system calculates one value (0–100) to describe the drain condition.

  blockageScore = 0;

  if (waterLevelCM < 10) blockageScore += 40; //change the level cm
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

  //manual motor control

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

  //To keep daily data accurate by clearing yesterday’s counts and starting a new day.

  if (millis() - lastDayReset >= DAY_MS) {

    //millis() → time since ESP32 started
    //lastDayReset → time when we last reset the counts
    //DAY_MS = 86400000 → 24 hours in milliseconds

    // Get today’s date as string (or use a real RTC module for exact date)
    String today = "2026-03-07"; // replace with RTC date dynamically if you have one

    // Save yesterday’s daily overflow count to Firebase
    String path = "/drains/" + String(DRAIN_ID) + "/dailyOverflow/" + today;
    Firebase.RTDB.setInt(&fbdo, path, dailyOverflowCount);

    dailyIRCount = 0;
    dailyOverflowCount = 0;
    dailyVibrationCount = 0;
    motorRunTimeToday = 0;
    totalWaterLevel = 0;
    readingCount = 0;

    lastDayReset = millis();
    //You check a clock. If 24 hours have passed since yesterday, you start a new daily diary page.
  }
}

void updateMonthlyAnalytics() {
  if (millis() - lastMonthReset >= MONTH_MS) {
    monthlyOverflowCount = 0; // reset the monthly count
    lastMonthReset = millis();
  }
}


void handleDashboardCommands() {

  String path = "/drains/" + String(DRAIN_ID) + "/commands/motor";

  if (Firebase.RTDB.getString(&fbdo, path)) {

    String cmd = fbdo.stringData();

    if (cmd == "ON") {
      digitalWrite(IN1, HIGH);   // forward
      digitalWrite(IN2, LOW);
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

  //This function logs severe blockage events to Firebase so the dashboard always shows the latest critical drain condition.

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