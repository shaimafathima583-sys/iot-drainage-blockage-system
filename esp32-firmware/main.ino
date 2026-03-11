#include <WiFi.h>
#include <Firebase_ESP_Client.h>
//to save time
#include <time.h>

// WiFi
const char* ssid = "Nphn";
const char* pwd  = "Twinkle12259";

// Drain identity
#define DRAIN_ID "DRAIN_001"

// Firebase
#define API_KEY "AIzaSyCeEBw9_xuIz0D7XyMIK06rkmATL-wmIm4"
#define DB_URL "https://drainwatch-caca8-default-rtdb.asia-southeast1.firebasedatabase.app/"
#define USER_EMAIL "nabseee15f@gmail.com"
#define USER_PWD "asdfghjkl123" 

//create firebase objects for data transfer, authentication and configuration
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;
FirebaseData fbdoCmd;   // separate stream object for commands (non-blocking reads)

// Pins
#define TRIG_PIN 5
#define ECHO_PIN 21
#define MQ2_PIN 34
#define IR_PIN 23
#define FLOAT_PIN 19
#define VIB_AO 32
#define VIB_DO 4
#define IN1 26
#define IN2 27
#define ENA 25

// Motor PWM for speed control
#define PWM_CHANNEL 0
#define PWM_FREQ 1000   // Hz
#define PWM_RESOLUTION 8 // 8-bit → 0–255
//ntp n time zone
#define NTP_SERVER "pool.ntp.org"
#define GMT 19800    // UTC+5:30 TZ
#define DST 0

//timer
unsigned long sendDataPrevMillis = 0;
unsigned long lastCmdRead = 0;
unsigned long lastMidnightChk = 0;
int lastRollupDay = -1;   // tracks which day we last rolled up

// Sensor values
float waterLvl = 0;
int gasVal = 0;

// States
int blockageScore = 0;
int healthScore = 100;
String drainState = "Normal";
String predictedRisk = "LOW";
//motor
unsigned long motorRunTimeToday = 0;   // ms
bool motorActive = false;
bool manualOverride = false;
int motorSpeed = 200;
String motorDir = "LEFT";

// Event counters
int dailyIRCount = 0;
int dailyOverflowCount = 0;
int dailyVibrationCount = 0;

//might rmv 
int monthlyOverflowCount = 0;

// Fault flags
bool ultrasonicFault = false;
bool vibrationFault = false;
bool mq2Fault = false;
bool floatFault = false;

// Tracking variables
int lastIRState = LOW;
int lastFloatState = LOW;
int lastRaw = 4095;

bool vibInWindow = false;
unsigned long vibWindowStart = 0;
const unsigned long VIB_WINDOW = 1000; //1s debounce

// daily accumulators (for avgs)
float totalWaterLevel  = 0;
float totalGas = 0;
float totalHealthScore = 0;
int   readingCount = 0;


void setup() {
  Serial.begin(115200);

  initPins();
  initWiFi();
  initNTP();

  // Firebase config
  config.api_key = API_KEY;
  config.database_url = DB_URL;
  auth.user.email = USER_EMAIL;
  auth.user.password = USER_PWD;

  Firebase.reconnectNetwork(true);
  fbdo.setResponseSize(2048);
  fbdoCmd.setResponseSize(512);   // small buffer = faster
  Firebase.begin(&config, &auth);
  // Resume counters from Firebase
  if (Firebase.RTDB.getInt(&fbdo, "/drains/" DRAIN_ID "/daily/irCount"))
    dailyIRCount = fbdo.intData();
  if (Firebase.RTDB.getInt(&fbdo, "/drains/" DRAIN_ID "/daily/overflowCount"))
    dailyOverflowCount = fbdo.intData();
  if (Firebase.RTDB.getInt(&fbdo, "/drains/" DRAIN_ID "/daily/vibrationCount"))
    dailyVibrationCount = fbdo.intData();
  if (Firebase.RTDB.getInt(&fbdo, "/drains/" DRAIN_ID "/daily/motorRunTime_sec"))
    motorRunTimeToday = (unsigned long)fbdo.intData() * 1000;
  if (Firebase.RTDB.getInt(&fbdo, "/drains/" DRAIN_ID "/monthly/overflowCount"))
    monthlyOverflowCount = fbdo.intData();
  Firebase.setDoubleDigits(4);
  config.timeout.serverResponse = 5 * 1000;
  
  // Record startup day so midnight rollup doesn't trigger immediately
  struct tm t;
  if (getLocalTime(&t)) lastRollupDay = t.tm_mday;

}

void loop() {

  if (!Firebase.ready()) return;
  unsigned long now = millis();

  // Live sensors + send every 2s
  if (now - sendDataPrevMillis >= 2000 || sendDataPrevMillis == 0) {
    sendDataPrevMillis = now;

    readSensors();
    trackTrash();
    trackOverflow();
    trackVibration();

    if (motorActive) motorRunTimeToday += 2000;   // accumulate ms

    detectFaults();
    computeBlockageScore();
    classifyDrainState();
    predictBlockageRisk();
    controlMotor();

    logEvents();
    sendLiveData(); //replaces 15 individual calls by updating it in one node     
  }
  // every 500 ms run indep. Separate from live loop so actuator response is fast (~500 ms)
  if (now - lastCmdRead >= 500) {
    lastCmdRead = now;
    handleDashboardCommands();
  }

  // Midnight rollup check every 60 s
  if (now - lastMidnightChk >= 60000) {
    lastMidnightChk = now;
    checkMidnightRollup();
  }

}

void initNTP() {
  configTime(GMT, DST, NTP_SERVER);
  Serial.print("Syncing NTP time");
  struct tm t;
  int tries = 0;
  while (!getLocalTime(&t) && tries++ < 20) {
    delay(500);
    Serial.print(".");
  }
  char buf[32];
  strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &t);
  Serial.printf("\nTime synced: %s\n", buf);
}

void initPins() {
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(MQ2_PIN, INPUT);
  pinMode(IR_PIN, INPUT);
  pinMode(FLOAT_PIN, INPUT_PULLUP);
  pinMode(VIB_AO, INPUT);
  pinMode(VIB_DO, INPUT);

  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  ledcSetup(PWM_CHANNEL, PWM_FREQ, PWM_RESOLUTION);
  ledcAttachPin(ENA, PWM_CHANNEL);
  ledcWrite(PWM_CHANNEL, 0);
  //ledcAttach(ENA, PWM_FREQ, PWM_RESOLUTION);
  //ledcWrite(ENA, 0);
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, LOW);
}

void initWiFi() {
  Serial.print("Connecting to WiFi");
  WiFi.begin(ssid, pwd);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("Connected IP Address: ");
  Serial.println(WiFi.localIP());
}

void readSensors() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  long duration = pulseIn(ECHO_PIN, HIGH, 15000);
  if (duration > 0)
    waterLvl = duration * 0.034 / 2;
  else
   waterLvl = 0;

  gasVal = analogRead(MQ2_PIN);

  totalWaterLevel += waterLvl;
  totalGas += gasVal;
  totalHealthScore += healthScore;
  readingCount++;

}

void trackTrash() {
  int currentIR = digitalRead(IR_PIN);
 if (currentIR == LOW && lastIRState == HIGH)
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
void trackVibration() {

  int rawAO = analogRead(VIB_AO);
  int rawDO = digitalRead(VIB_DO);

  int delta = lastRaw - rawAO;   // vibration causes drop from ~4095

  // Detect vibration hit
  if (!vibInWindow && (delta > 500 || rawDO == 0)) {

    dailyVibrationCount++;
    vibInWindow = true;
    vibWindowStart = millis();

    Serial.print("Vibration HIT! Total: ");
    Serial.println(dailyVibrationCount);
  }

  // Close debounce window after 1 second
  if (vibInWindow && millis() - vibWindowStart >= VIB_WINDOW) {
    vibInWindow = false;
  }

  lastRaw = rawAO;
}



void detectFaults() {
  ultrasonicFault = (waterLvl <= 0.5 || waterLvl > 400);

  // Vibration sensor disconnected (AO reads 0)
  if (analogRead(VIB_AO) <= 0) {
    vibrationFault = true;
  } else {
    vibrationFault = false;
  }

  // MQ2 dead / disconnected
  if (gasVal <= 10) {
    mq2Fault = true;
  } else {
    mq2Fault = false;
  }

  // Float says overflow but ultrasonic says drain is empty — contradiction
  if (digitalRead(FLOAT_PIN) == HIGH && waterLvl > 7 && !ultrasonicFault) {
    floatFault = true;
  } else {
    floatFault = false;
  }
}

void computeBlockageScore() {
  blockageScore = 0;

  if (waterLvl < 4 ) blockageScore += 40;
  if (gasVal > 3500) blockageScore += 20;
  if (dailyVibrationCount > 15) blockageScore += 10;
  if (digitalRead(FLOAT_PIN) == HIGH) blockageScore += 100;

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
  if (!manualOverride) {
    // Auto mode
    if (drainState == "Severe") {
      motorDir    = "LEFT";
      motorSpeed  = 200;
      motorActive = true;
    } else {
      motorSpeed  = 0;
      motorActive = false;
    }
  }

  if (motorActive) {
    if (motorDir == "LEFT") {
      digitalWrite(IN1, HIGH);
      digitalWrite(IN2, LOW);
    } else {
      digitalWrite(IN1, LOW);
      digitalWrite(IN2, HIGH);
    }
    ledcWrite(PWM_CHANNEL, motorSpeed);
  } else {
    digitalWrite(IN1, LOW);
    digitalWrite(IN2, LOW);
    ledcWrite(PWM_CHANNEL, 0);
  }
}

void predictBlockageRisk() {
  if (waterLvl < 4 && gasVal > 3800)
    predictedRisk = "HIGH";
  else if (waterLvl <= 6 )
    predictedRisk = "MEDIUM";
  else
    predictedRisk = "LOW";
}

void handleDashboardCommands() {
  
  String path = "/drains/" + String(DRAIN_ID) + "/commands/";
  if (Firebase.RTDB.getString(&fbdoCmd, path + "motor")) {
    String cmd = fbdoCmd.stringData();
    if (cmd == "ON") {
      motorActive    = true;
      manualOverride = true;
    } else if (cmd == "OFF") {
      motorActive    = false;
      manualOverride = true;
    } else if (cmd == "AUTO") {
      manualOverride = false;
    }
  }
  if (Firebase.RTDB.getInt(&fbdoCmd, path + "motorSpeed")) {
    motorSpeed = constrain(fbdoCmd.intData(), 0, 255);
  }

  // Direction
  if (Firebase.RTDB.getString(&fbdoCmd, path + "motorDir")) {
    String d = fbdoCmd.stringData();
    if (d == "LEFT" || d == "RIGHT") {
      motorDir = d;
    }
  }
}

void logEvents() {
  if (drainState == "Severe") {
    struct tm t;
    time_t now = 0;
    if (getLocalTime(&t)) {
      now = mktime(&t);
    }
    String path = "/drains/" + String(DRAIN_ID) + "/events/latest";
    FirebaseJson ev;
    ev.set("message",  "Severe blockage detected");
    ev.set("timestamp", (int)now);
    Firebase.RTDB.updateNode(&fbdo, path, &ev);
  }
}

void sendLiveData() {
  String base = "/drains/" + String(DRAIN_ID) ;
  int rssi = WiFi.RSSI();

  // Running daily averages
  float avgWaterLevel = 0;
  float avgGas = 0;
  float avgHealth = 0;

  if (readingCount > 0) {
    avgWaterLevel = totalWaterLevel / readingCount;
    avgGas = totalGas / readingCount;
    avgHealth = totalHealthScore / readingCount;
  }

  // WiFi strength label
  String wifiStrength = "";
  if (rssi >= -60) {
    wifiStrength = "Strong";
  } else if (rssi >= -75) {
    wifiStrength = "Medium";
  } else {
    wifiStrength = "Weak";
  }

  //timestamp
  struct tm t;
  time_t now    = 0;
  char timeBuf[32] = "N/A";

  if (getLocalTime(&t)) {
    now = mktime(&t);
    strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M:%S", &t);
  }
  Serial.printf("[%s]  State: %s  Score: %d  Water: %.1f cm\n", timeBuf, drainState.c_str(), blockageScore, waterLvl);

  FirebaseJson j;
   // live/
  j.set("live/waterLevel", waterLvl);
  j.set("live/gasVal", gasVal);
  j.set("live/float", digitalRead(FLOAT_PIN) == HIGH);
  j.set("live/irDetected", digitalRead(IR_PIN) == LOW);
  j.set("live/vibDetected", vibInWindow);   // true during active 1s debounce window
  j.set("live/blockageScore", blockageScore);
  j.set("live/healthScore",  healthScore);
  j.set("live/state", drainState);
  j.set("live/motorActive", motorActive);
  j.set("live/rssi", rssi);
  j.set("live/wifiStrength", wifiStrength);
  j.set("live/predictedRisk", predictedRisk);
  j.set("live/lastSeen", (int)now);
  j.set("live/lastSeenStr", String(timeBuf));
  j.set("live/motorSpeed", motorSpeed);
  j.set("live/motorDir", motorDir);

  // faults/
  j.set("faults/ultrasonic", ultrasonicFault);
  j.set("faults/vibration", vibrationFault);

  j.set("faults/mq2", mq2Fault);
  j.set("faults/float", floatFault);

  // daily/
  j.set("daily/irCount", dailyIRCount);
  j.set("daily/overflowCount", dailyOverflowCount);
  j.set("daily/vibrationCount", dailyVibrationCount);
  j.set("daily/motorRunTime_sec", (int)(motorRunTimeToday / 1000));
  j.set("daily/avgWaterLevel", avgWaterLevel);
  j.set("daily/avgGas", avgGas);
  j.set("daily/avgHealthScore", avgHealth);

  // prediction/
  j.set("prediction/risk", predictedRisk);

  // monthly/ ??????
  j.set("monthly/overflowCount", monthlyOverflowCount);

  //push fb
  if (!Firebase.RTDB.updateNode(&fbdo, base, &j)) {
    Serial.print("Firebase error: ");
    Serial.println(fbdo.errorReason());
  }
}
void checkMidnightRollup() {

  // Check if day has changed (can change / enhance it)
  struct tm t;
  if (!getLocalTime(&t)) {
    return;
  }
  if (t.tm_mday == lastRollupDay) {
    return;
  }
  lastRollupDay = t.tm_mday;

  // Build yesterday date string
  time_t yesterday = mktime(&t) - 86400;
  struct tm* yt = localtime(&yesterday);
  char dateBuf[12];
  strftime(dateBuf, sizeof(dateBuf), "%Y-%m-%d", yt);

  // Daily averages
  float avgWaterLevel = 0;
  float avgGas = 0;
  float avgHealth = 0;

  if (readingCount > 0) {
    avgWaterLevel = totalWaterLevel  / readingCount;
    avgGas = totalGas / readingCount;
    avgHealth = totalHealthScore / readingCount;
  }

  //Save snapshot to Firebase
  String histPath = "/drains/" + String(DRAIN_ID) + "/history/" + String(dateBuf);

  FirebaseJson snap;
  snap.set("irCount", dailyIRCount);
  snap.set("overflowCount", dailyOverflowCount);
  snap.set("vibrationCount", dailyVibrationCount);
  snap.set("motorRunTime_sec", (int)(motorRunTimeToday / 1000));
  snap.set("avgWaterLevel", avgWaterLevel);
  snap.set("avgGas", avgGas);
  snap.set("avgHealthScore", avgHealth);

  if (Firebase.RTDB.updateNode(&fbdo, histPath, &snap)) {
    Serial.printf("History saved: %s\n", dateBuf);
  } else {
    Serial.printf("History write failed: %s\n", fbdo.errorReason().c_str());
  }

  //Reset daily accumulators
  dailyIRCount = 0;
  dailyOverflowCount = 0;
  dailyVibrationCount = 0;
  motorRunTimeToday = 0;
  totalWaterLevel = 0;
  totalGas = 0;
  totalHealthScore = 0;
  readingCount = 0;
}
