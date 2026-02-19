#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <HTTPClient.h>
#include "esp_wifi.h"
#include "esp_bt.h"

// --- OLED Setup ---
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// --- Ultrasonic Pins ---
#define TRIG_PIN1 14
#define ECHO_PIN1 35
#define TRIG_PIN2 13
#define ECHO_PIN2 15
#define TIMEOUT_US 25000UL

// --- AP credentials ---
const char* apSSID     = "LilyGO_Config";
const char* apPassword = "12345678";

// --- Web server ---
WebServer server(80);
Preferences prefs;

// --- Timing ---
unsigned long lastPost = 0;
unsigned long intervalMs = 10000; // default 10s, overwritten from prefs

// --- Globals for config ---
String team, project;
bool configSaved = false;
int peopleCount = 0;   // Tracks number of people
// --- Counting state ---
const float threshold = 20.0;      // cm
const unsigned long debounceMs = 200; // minimal time between steps
enum Step { NONE, S1_FIRST, S2_FIRST };
Step stepState = NONE;
unsigned long lastStepTs = 0;
// --- Posting task control ---
TaskHandle_t postTaskHandle = nullptr;
volatile bool postTrigger = false;
float postD1 = 0, postD2 = 0;  // buffers for sensor readings
unsigned long lastSensorRead = 0;
const unsigned long sensorInterval = 100; // 100 ms between sensor reads
float d1 = -1, d2 = -1; // store latest readings

// --- Local Flask Gateway Server (M4) ---
String gatewayIp = "192.168.165.187"; // Default fallback
String gatewayServer = "";            // Flask endpoint, will contain flask server IP, to be constructed in setup()

// --- Ultrasonic function ---
float readUltrasonic(int trigPin, int echoPin) {
  pinMode(trigPin, OUTPUT);
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);
  pinMode(echoPin, INPUT);
  long duration = pulseInLong(echoPin, HIGH, TIMEOUT_US);
  if (duration == 0) return -1;
  return duration * 0.034 / 2.0;
}

// --- Config portal handlers ---
void handleRoot() {
  String html =
    "<!DOCTYPE html><html><head><meta charset='utf-8'>"
    "<title>LilyGO Configuration</title>"
    "<style>body{font-family:sans-serif;margin:24px}label{display:block;margin-top:12px}"
    "input{width:280px;padding:6px;margin-top:4px}button{margin-top:16px;padding:8px 16px}</style>"
    "</head><body>"
    "<h2>Device Configuration</h2>"
    "<form action='/save' method='POST'>"
      "<label>Team Name<input type='text' name='team' required></label>"
      "<label>Project Name<input type='text' name='project' required></label>"
      "<label>SSID<input type='text' name='ssid' required></label>"
      "<label>Password<input type='password' name='password'></label>"
      "<label>Upload Interval (seconds)<input type='number' name='interval' min='1' value='10' required></label>"
      "<label>Gateway IP<input type='text' name='gatewayIp' placeholder='e.g. 192.168.1.15' required></label>"
      "<button type='submit'>Save</button>"
    "</form>"
    "</body></html>";
  server.send(200, "text/html", html);
}

void handleSave() {
  team     = server.arg("team");
  project  = server.arg("project");
  String ssid     = server.arg("ssid");
  String password = server.arg("password");
  int interval    = server.arg("interval").toInt();
  String newIp = server.arg("gatewayIp");

  prefs.begin("smartcity", false);
  prefs.putString("team", team);
  prefs.putString("project", project);
  prefs.putString("ssid", ssid);
  prefs.putString("password", password);
  prefs.putInt("interval", interval);
  prefs.putString("gatewayIp", newIp);
  prefs.putBool("configured", true);
  prefs.end();

  configSaved = true;
  server.send(200, "text/html", "<h3>Configuration saved. You can safely exit this page.</h3>");
}

// --- POST sensor data via HTTP ---
void postSensorData(float d1, float d2) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;

  http.begin(gatewayServer);
  http.addHeader("Content-Type", "application/json");

    // Build JSON packet for gateway
    String payload = "{";
    payload += "\"project_id\":\"" + team + "_" + project + "\",";

    payload += "\"timestamp\":\"" + String(millis()) + "\",";   // Add timestamp

    payload += "\"data\":{";
    payload += "\"sensor1\":" + String(d1, 1) + ",";
    payload += "\"sensor2\":" + String(d2, 1) + ",";
    payload += "\"peopleCount\":" + String(peopleCount);
    payload += "}}";

    int httpResponseCode = http.POST(payload);
    Serial.println("Gateway POST Response: " + String(httpResponseCode));
    http.end();

  } else {
    Serial.println("Wi-Fi not connected. Cannot POST data.");
  }
}

void postTask(void* pv) {
  for (;;) {
    if (postTrigger) {
      postSensorData(postD1, postD2);  // post to Flask gateway
      postTrigger = false;             // reset flag
    }
    vTaskDelay(pdMS_TO_TICKS(10));     // yield
  }
}

// --- Setup ---
void setup() {
  esp_bt_controller_disable();
  Serial.begin(115200);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED init failed!");
    while (true);
  }
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("Booting...");
  display.display();

  WiFi.softAP(apSSID, apPassword);
  server.on("/", handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.begin();

  Serial.println("Config portal active for 100 seconds...");
  unsigned long start = millis();
  
  while (!configSaved && millis() - start < 100000) {
    server.handleClient();
    delay(10);
  }

  WiFi.softAPdisconnect(true);
  server.stop();

  prefs.begin("smartcity", true);
  bool configured = prefs.getBool("configured", false);

  if (configured) {
    team     = prefs.getString("team", "N/A");
    project  = prefs.getString("project", "N/A");
    String ssid     = prefs.getString("ssid", "");
    String password = prefs.getString("password", "");
    intervalMs      = prefs.getInt("interval", 10) * 1000;
    // Load saved IP, or use default if missing
    gatewayIp = prefs.getString("gatewayIp", "192.168.165.187"); 
    // Construct the full URL
    gatewayServer = "http://" + gatewayIp + ":5000/api/telemetry";
    Serial.println("Gateway URL set to: " + gatewayServer);
    prefs.end();

    WiFi.mode(WIFI_STA);
        // Start posting task on core 0 (sensor/render runs on core 1 by default)
    if (postTaskHandle == nullptr) {
      xTaskCreatePinnedToCore(postTask, "postTask", 4096, nullptr, 1, &postTaskHandle, 0);
    }
    WiFi.begin(ssid.c_str(), password.c_str());

    Serial.println("Connecting to saved Wi-Fi...");
    unsigned long startAttemptTime = millis();
    const unsigned long wifiTimeout = 20000;

    while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < wifiTimeout) {
      delay(500);
      Serial.print(".");
    }

    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\nConnected! IP: " + WiFi.localIP().toString());
    } else {
      Serial.println("\nWi-Fi failed. Staying in AP mode.");
      WiFi.softAP(apSSID, apPassword);
      server.on("/", handleRoot);
      server.on("/save", HTTP_POST, handleSave);
      server.begin();
    }
  } else {
    prefs.end();
    Serial.println("No credentials saved. Staying in AP mode.");
  }
}

// --- Loop ---
void loop() {
  unsigned long now = millis();
  if (now - lastSensorRead >= sensorInterval) {
    d1 = readUltrasonic(TRIG_PIN1, ECHO_PIN1);
    d2 = readUltrasonic(TRIG_PIN2, ECHO_PIN2);
    lastSensorRead = now;
  }
  // Counting logic: sequence-based with debounce
  static bool s1Active = false;
  static bool s2Active = false;

  // Detect active when below threshold and valid echo
  s1Active = (d1 > 0 && d1 < threshold);
  s2Active = (d2 > 0 && d2 < threshold);

  // Start a step when one sensor activates first
  if (stepState == NONE && (now - lastStepTs > debounceMs)) {
    if (s1Active && !s2Active) { stepState = S1_FIRST; lastStepTs = now; }
    else if (s2Active && !s1Active) { stepState = S2_FIRST; lastStepTs = now; }
  }

  // Complete a step when the other sensor activates shortly after
  if (stepState == S1_FIRST && s2Active) {
    peopleCount++;
    Serial.println("Person entered. Count = " + String(peopleCount));
    stepState = NONE;
    lastStepTs = now;
  } else if (stepState == S2_FIRST && s1Active) {
    if (peopleCount > 0) peopleCount--;
    Serial.println("Person exited. Count = " + String(peopleCount));
    stepState = NONE;
    lastStepTs = now;
  }

  // Timeout/reset if nothing completes the sequence
  if (stepState != NONE && (now - lastStepTs > 1000)) {
    stepState = NONE;
  }

  Serial.print(millis());
  Serial.print(",");
  Serial.print(d1);
  Serial.print(",");
  Serial.println(d2);

   // OLED display
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  // Top section: Sensor 1
  display.setCursor(0, 0);
  display.println("Sensor 1:");
  if (d1 < 0) display.println("No echo");
  else { display.print(d1, 1); display.println(" cm"); }

  // Middle section: Sensor 2
  display.setCursor(0, 32);
  display.println("Sensor 2:");
  if (d2 < 0) display.println("No echo");
  else { display.print(d2, 1); display.println(" cm"); }

  // Bottom line: Counter
  display.setCursor(0, 54);
  display.print("Counter: ");
  display.println(peopleCount);

  // Draw after writing all lines
  display.display();

  if (WiFi.getMode() & WIFI_AP) {
    server.handleClient();
  } else if (WiFi.status() == WL_CONNECTED) {
    if ((millis() - lastPost > intervalMs) && !postTrigger) {
      postD1 = d1;
      postD2 = d2;
      postTrigger = true;   // signal the other core to run postSensorData()
      lastPost = millis();
    } 

  }
  
  vTaskDelay(1);
}