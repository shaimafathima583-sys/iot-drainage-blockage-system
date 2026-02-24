// SMART DRAIN FLOOD PREVENTION

#define TRIG 5
#define ECHO 18
#define RAIN_PIN 34

float waterLevel;
int rainValue;

void setup() {
  Serial.begin(115200);

  pinMode(TRIG, OUTPUT);
  pinMode(ECHO, INPUT);
  pinMode(RAIN_PIN, INPUT);
}

void loop() {

  // ----- Measure Water Level -----
  digitalWrite(TRIG, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG, LOW);

  long duration = pulseIn(ECHO, HIGH);
  float distance = duration * 0.034 / 2;

  // Convert to percentage (adjust 30 based on drain height)
  waterLevel = map(distance, 2, 30, 100, 0);

  if (waterLevel < 0) waterLevel = 0;
  if (waterLevel > 100) waterLevel = 100;

  // ----- Read Rain Sensor -----
  rainValue = analogRead(RAIN_PIN);

  // ----- Smart Logic -----
  if (rainValue > 2000 && waterLevel > 80) {
    digitalWrite(BUZZER, HIGH);
    digitalWrite(RELAY, HIGH);
    Serial.println("⚠ FLASH FLOOD WARNING!");
  }
  else if (waterLevel > 60) {
    Serial.println("⚠ Possible Blockage");
    digitalWrite(BUZZER, LOW);
    digitalWrite(RELAY, LOW);
  }
  else {
    digitalWrite(BUZZER, LOW);
    digitalWrite(RELAY, LOW);
    Serial.println("System Normal");
  }

  // ----- Print Data -----
  Serial.print("Water Level: ");
  Serial.print(waterLevel);
  Serial.print("%  | Rain: ");
  Serial.println(rainValue);

  delay(2000);
}
