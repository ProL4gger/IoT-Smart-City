#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <HTTPClient.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// --- CONFIGURATION ---
// Update this to match your laptop's IP address
String serverIP = "192.168.1.54"; 
int serverPort = 5000;

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define PIR_PIN 15    // Motion Sensor Pin
#define BOOT_BUTTON 0 // Built-in button for Factory Reset

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
WebServer server(80);
Preferences preferences;

// Variables
String teamName, projName, ssid, password;
int interval;
unsigned long lastUploadTime = 0;

// HTML Page for Configuration
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html><head><meta name="viewport" content="width=device-width, initial-scale=1">
<style>body{font-family:Arial;padding:20px;background:#f4f4f4}.container{background:white;padding:20px;max-width:400px;margin:auto;border-radius:8px}</style>
</head><body><div class="container">
<h2>Smart City Setup</h2>
<form action="/save" method="POST">
  <label>Team Name</label><br><input type="text" name="team"><br>
  <label>Project Name</label><br><input type="text" name="project"><br>
  <label>Wi-Fi SSID</label><br><input type="text" name="ssid"><br>
  <label>Wi-Fi Password</label><br><input type="password" name="pass"><br>
  <label>Upload Interval (sec)</label><br><input type="number" name="interval" value="10"><br><br>
  <input type="submit" value="Save & Connect">
</form></div></body></html>
)rawliteral";

void setup() {
  Serial.begin(115200);
  pinMode(PIR_PIN, INPUT);
  pinMode(BOOT_BUTTON, INPUT_PULLUP);
  
  // Initialize OLED Display
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("OLED failed"));
    for(;;);
  }
  display.clearDisplay();
  display.setTextColor(WHITE);
  
  // --- RESET LOGIC ---
  // Hold BOOT button while plugging in to erase settings
  if(digitalRead(BOOT_BUTTON) == LOW) {
    display.clearDisplay();
    display.setCursor(0,0);
    display.setTextSize(1);
    display.println("RESETTING...");
    display.display();
    delay(2000);
    
    preferences.begin("smartcity", false);
    preferences.clear();
    preferences.end();
    
    display.println("DONE! Restarting");
    display.display();
    delay(1000);
    ESP.restart();
  }

  // Load Saved Settings
  preferences.begin("smartcity", true); 
  ssid = preferences.getString("ssid", "");
  password = preferences.getString("pass", "");
  teamName = preferences.getString("team", "MyTeam");
  interval = preferences.getInt("interval", 10);
  preferences.end();

  // Mode Selection
  if (ssid == "") {
    setupAPMode(); // No settings found -> Config Mode
  } else {
    setupClientMode(); // Settings found -> Operational Mode
  }
}

// --- MODE 1: CONFIGURATION (Access Point) ---
void setupAPMode() {
  Serial.println("Starting AP Mode...");
  WiFi.softAP("LilyGO_Config"); 
  
  display.clearDisplay();
  display.setCursor(0,0);
  display.setTextSize(1);
  display.println("CONFIG MODE");
  display.println("Connect to Wi-Fi:");
  display.println("LilyGO_Config");
  display.println("Go to: 192.168.4.1");
  display.display();

  server.on("/", []() { server.send(200, "text/html", index_html); });
  server.on("/save", handleSave);
  server.begin();
}

// Save inputs to memory
void handleSave() {
  preferences.begin("smartcity", false);
  preferences.putString("team", server.arg("team"));
  preferences.putString("project", server.arg("project"));
  preferences.putString("ssid", server.arg("ssid"));
  preferences.putString("pass", server.arg("pass"));
  preferences.putInt("interval", server.arg("interval").toInt());
  preferences.end();

  server.send(200, "text/html", "Saved! Restarting...");
  delay(1000);
  ESP.restart(); 
}

// --- MODE 2: OPERATIONAL (Wi-Fi Client) ---
void setupClientMode() {
  display.clearDisplay();
  display.setCursor(0,0);
  display.setTextSize(1);
  display.println("Connecting to:");
  display.println(ssid);
  display.display();
  
  WiFi.begin(ssid.c_str(), password.c_str());
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    attempts++;
  }

  // Fallback to config if connection fails
  if(WiFi.status() != WL_CONNECTED) {
    display.println("Failed! Resetting...");
    display.display();
    delay(2000);
    preferences.begin("smartcity", false);
    preferences.clear();
    preferences.end();
    ESP.restart();
  }

  display.clearDisplay();
  display.setCursor(0,0);
  display.println("OPERATIONAL");
  display.println("Sending data...");
  display.display();
}

void loop() {
  // If in Config Mode, keep server running
  if (WiFi.getMode() == WIFI_AP) {
    server.handleClient();
    return;
  }

  // --- OPERATIONAL LOOP ---
  if (millis() - lastUploadTime > interval * 1000) {
    if(WiFi.status() == WL_CONNECTED) {
      
      // 1. Read Sensor
      int value = digitalRead(PIR_PIN);
      
      // 2. Update Screen (Visual Interface)
      display.clearDisplay();
      display.setCursor(0,0);
      display.println("OPERATIONAL");
      display.println("----------------");
      display.print("Motion: ");
      if (value == 1) {
        display.println("YES (1)");
      } else {
        display.println("NO (0)");
      }
      display.println("Sent to Server!");
      display.display();

     // 3. SEND DATA TO YOUR LAPTOP (original)
    // 3. SEND DATA TO YOUR LAPTOP (UPDATED JSON FORMAT)
    HTTPClient http;
    String url = "http://" + serverIP + ":" + String(serverPort) + "/telemetry";

    http.begin(url);
    http.addHeader("Content-Type", "application/json");

    // Build JSON exactly as Flask expects
    String json =
    "{"
      "\"project_id\":\"SmartCity_Team_PIR\","
      "\"data\":{"
        "\"motion\":" + String(value) +
      "}"
    "}";

    int responseCode = http.POST(json);

    Serial.println("=================================");
    Serial.println("Sending to Flask Gateway");
    Serial.println("URL: " + url);
    Serial.println("Payload: " + json);
    Serial.println("Response Code: " + String(responseCode));
    Serial.println("=================================");

    http.end();


    // // 4. SEND DATA TO THINGSBOARD
    // HTTPClient http2;
    // String tbUrl = "http://thingsboard.cloud/api/v1/d16z0vsay7nb64osbbzq/telemetry";

    // http2.begin(tbUrl);
    // http2.addHeader("Content-Type", "application/json");

    // // You can send the SAME JSON or a new one. Here’s a clean version:
    // String tbJson = "{\"motion\":" + String(value) + "}";

    // int tbCode = http2.POST(tbJson);
    // Serial.print("ThingsBoard Response: ");
    // Serial.println(tbCode);

    // http2.end();


    }
    lastUploadTime = millis();
  }
}