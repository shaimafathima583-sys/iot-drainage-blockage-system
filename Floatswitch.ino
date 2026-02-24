#include <WiFi.h>

// ===== WiFi Credentials =====
const char* ssid = "Redmi 14C";
const char* password = "12345678";

// ===== Float Switch Pin =====
#define FLOAT_PIN 27

WiFiServer server(80);

void setup() {
  Serial.begin(115200);

  pinMode(FLOAT_PIN, INPUT_PULLUP);  // internal pull-up resistor

  Serial.print("Connecting to WiFi");
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.print("Connected! IP Address: ");
  Serial.println(WiFi.localIP());

  server.begin();
}

void loop() {
  WiFiClient client = server.available();
  if (!client) return;

  while (!client.available()) delay(1);

  int floatState = digitalRead(FLOAT_PIN);

  String waterStatus;
  if (floatState == LOW) {
    waterStatus = "HIGH WATER (Float Triggered)";
  } else {
    waterStatus = "LOW WATER (Safe)";
  }

  Serial.println(waterStatus);

  // ===== Send Web Page =====
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: text/html");
  client.println("Connection: close");
  client.println();

  client.println("<!DOCTYPE html>");
  client.println("<html>");
  client.println("<head>");
  client.println("<meta http-equiv='refresh' content='2'>");
  client.println("<meta name='viewport' content='width=device-width, initial-scale=1'>");
  client.println("<title>ESP32 Float Switch</title>");
  client.println("<style>");
  client.println("body { font-family: Arial; text-align: center; background: #f2f2f2; }");
  client.println("h1 { color: #333; }");
  client.println(".status { font-size: 24px; padding: 20px; }");
  client.println(".high { color: red; font-weight: bold; }");
  client.println(".low { color: green; font-weight: bold; }");
  client.println("</style>");
  client.println("</head>");
  client.println("<body>");

  client.println("<h1>Drain Water Level (Float Switch)</h1>");

  if (floatState == LOW) {
    client.println("<div class='status high'>WATER LEVEL: HIGH</div>");
  } else {
    client.println("<div class='status low'>WATER LEVEL: LOW</div>");
  }

  client.println("</body>");
  client.println("</html>");

  delay(300);
}
