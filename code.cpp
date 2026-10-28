#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <WiFi.h>
#include <HTTPClient.h>     
#include <PubSubClient.h>   

// ==========================================
// 1. CAU HINH WIFI & THINGSPEAK
// ==========================================
const char* ssid = "CirkitWifi"; 
const char* password = "";

const char* channel_id = "3349950"; 
String writeAPIKey = "KU4M2NDRH3ZGK622"; 

const char* mqtt_server = "broker.hivemq.com";
const int mqtt_port = 1883;
const char* mqtt_topic = "thientai/giamsat/vuhuytuan_12345"; 

// ==========================================
// 2. KHOI TAO DOI TUONG VA CHAN CAM
// ==========================================
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
#define OLED_ADDRESS  0x3C

#define I2C_SDA 21
#define I2C_SCL 22

#define SOIL_PIN      34   
#define BUZZER_PIN    25   
#define LED_ESP_PIN   2    
#define LED_OUT_PIN   26   

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
Adafruit_MPU6050 mpu;

WiFiClient espClient;
PubSubClient mqttClient(espClient);

const int DRY_VALUE = 0;    
const int WET_VALUE = 4095; 

// ==========================================
// NGUONG CANH BAO 
// ==========================================
const float VIB_WARNING_THRESHOLD = 6.5;    
const float EARTHQUAKE_THRESHOLD  = 8.5;    
const int   SOIL_WARNING_THRESHOLD  = 70;   

unsigned long lastDisplayTime = 0;
unsigned long lastMqttTime = 0;
unsigned long lastHttpTime = 0;

const int DISPLAY_INTERVAL = 500;           
const int MQTT_INTERVAL = 15000;            // Gửi MQTT 15 giây/lần
const int HTTP_INTERVAL = 30000;            // Gửi HTTP 30 giây/lần

// He thong nhay LED / Coi (Non-blocking)
unsigned long buzzerStartTime = 0;
bool isBuzzerActive = false;
unsigned long ledBlinkStartTime = 0;
int ledBlinkTarget = 0;     
int ledBlinkCount = 0;      
bool isLedHigh = false;     
const int BLINK_DURATION = 200; 
bool isFirstLoop = true;

// ==========================================
// 3. HAM KET NOI MQTT
// ==========================================
void reconnectMqtt() {
  if (!mqttClient.connected()) {
    Serial.print("\n[HETHONG] Dang ket noi HiveMQ MQTT... ");
    String clientId = "ESP32Client-" + String(random(0xffff), HEX);
    if (mqttClient.connect(clientId.c_str())) {
      Serial.println("Thanh cong!");
    } else {
      Serial.print("Loi, ma loi = "); Serial.println(mqttClient.state());
    }
  }
}

// ==========================================
// 4. SETUP
// ==========================================
void setup() {
  Serial.begin(115200);
  Serial.println("\n--- KHOI DONG HE THONG GIAM SAT ---");
  
  pinMode(BUZZER_PIN, OUTPUT); pinMode(LED_ESP_PIN, OUTPUT);
  pinMode(LED_OUT_PIN, OUTPUT); pinMode(SOIL_PIN, INPUT);

  Wire.begin(I2C_SDA, I2C_SCL); 
  delay(100); 

  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) { 
    Serial.println("[LOI] Khong tim thay man hinh OLED!"); 
  } else {
    display.clearDisplay(); display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 10); display.print("Dang ket noi WiFi..."); display.display();
  }

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  Serial.println("\n[WIFI] Da ket noi thanh cong!");
  
  display.clearDisplay(); display.setCursor(0, 10); display.println("WiFi: OK!"); display.display(); delay(1000);

  mqttClient.setServer(mqtt_server, mqtt_port);
  mqttClient.setBufferSize(256);

  if (!mpu.begin()) { Serial.println("[LOI] Khong tim thay cam bien MPU6050!"); }
  else { 
    mpu.setAccelerometerRange(MPU6050_RANGE_4_G); 
    mpu.setFilterBandwidth(MPU6050_BAND_21_HZ); 
  }

  for (int i = 0; i < 3; i++) { digitalWrite(BUZZER_PIN, HIGH); delay(100); digitalWrite(BUZZER_PIN, LOW); delay(100); }

  lastDisplayTime = millis();
  lastMqttTime = millis() - MQTT_INTERVAL + 2000; 
  lastHttpTime = millis() - HTTP_INTERVAL + 5000;
}

// ==========================================
// 5. LOOP
// ==========================================
void loop() {
  unsigned long currentMillis = millis();

  if (WiFi.status() == WL_CONNECTED) {
    if (!mqttClient.connected()) reconnectMqtt();
    mqttClient.loop();
  }

  // DOC GIA TOC
  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);
  float totalAccel_g = sqrt(a.acceleration.x * a.acceleration.x + 
                            a.acceleration.y * a.acceleration.y + 
                            a.acceleration.z * a.acceleration.z);
  float vibration = abs(totalAccel_g - 1.0) * 9.8; 
  if (vibration < 0.5) vibration = 0.0; 

  // DOC DO AM
  int soilValue = analogRead(SOIL_PIN);
  int soilMoisturePercent = map(soilValue, DRY_VALUE, WET_VALUE, 0, 100);
  soilMoisturePercent = constrain(soilMoisturePercent, 0, 100); 

  // CANH BAO
  if (soilMoisturePercent >= SOIL_WARNING_THRESHOLD) {
    if (!isBuzzerActive) {
      isBuzzerActive = true;
      buzzerStartTime = currentMillis;
      digitalWrite(BUZZER_PIN, HIGH);
    }
  }
  if (isBuzzerActive && (currentMillis - buzzerStartTime >= 1000)) {
    digitalWrite(BUZZER_PIN, LOW);
    if (currentMillis - buzzerStartTime >= 5000) { isBuzzerActive = false; }
  }

  if (ledBlinkCount == 0 && !isLedHigh) { 
    if (vibration >= EARTHQUAKE_THRESHOLD) {
      ledBlinkTarget = 3; 
      ledBlinkStartTime = currentMillis; isLedHigh = true;
      digitalWrite(LED_OUT_PIN, HIGH); digitalWrite(LED_ESP_PIN, HIGH);
    } else if (vibration >= VIB_WARNING_THRESHOLD) {
      ledBlinkTarget = 1; 
      ledBlinkStartTime = currentMillis; isLedHigh = true;
      digitalWrite(LED_OUT_PIN, HIGH); digitalWrite(LED_ESP_PIN, HIGH);
    }
  }

  if (ledBlinkTarget > 0) {
    if (currentMillis - ledBlinkStartTime >= BLINK_DURATION) {
      ledBlinkStartTime = currentMillis; 
      if (isLedHigh) {
        digitalWrite(LED_OUT_PIN, LOW); digitalWrite(LED_ESP_PIN, LOW);
        isLedHigh = false; ledBlinkCount++; 
      } else {
        if (ledBlinkCount < ledBlinkTarget) {
          digitalWrite(LED_OUT_PIN, HIGH); digitalWrite(LED_ESP_PIN, HIGH);
          isLedHigh = true;
        } else {
          ledBlinkTarget = 0; ledBlinkCount = 0;
        }
      }
    }
  }

  // OLED
  if (isFirstLoop || (currentMillis - lastDisplayTime >= DISPLAY_INTERVAL)) {
    isFirstLoop = false;
    lastDisplayTime = currentMillis;
    display.clearDisplay(); display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    
    display.setCursor(95, 0);
    if(mqttClient.connected()) display.print("MQ-OK"); else display.print("OFF");

    display.setCursor(0, 0);
    if (vibration >= EARTHQUAKE_THRESHOLD) display.println("!! DONG DAT !!");
    else if (soilMoisturePercent >= SOIL_WARNING_THRESHOLD) display.println("!! SAT LO !!");
    else if (vibration >= VIB_WARNING_THRESHOLD) display.println("! CANH BAO SOM !");
    else display.println("AN TOAN");
    
    display.setCursor(0, 20); display.print("Do Am: "); display.print(soilMoisturePercent); display.println("%");
    display.setCursor(0, 35); display.print("Rung : "); display.print(vibration, 1); display.println(" m/s2");
    
    int nextMqtt = (MQTT_INTERVAL - (currentMillis - lastMqttTime)) / 1000;
    int nextHttp = (HTTP_INTERVAL - (currentMillis - lastHttpTime)) / 1000;
    display.setCursor(0, 50); 
    display.print("MQ:"); display.print(max(0, nextMqtt)); display.print("s ");
    display.print("HT:"); display.print(max(0, nextHttp)); display.print("s");
    display.display();
  }

  // MQTT (15s)
  if (currentMillis - lastMqttTime >= MQTT_INTERVAL) {
    lastMqttTime = currentMillis;
    if (mqttClient.connected()) {
      String json = "{";
      json += "\"soil\":" + String(soilMoisturePercent) + ",";
      json += "\"vib\":" + String(vibration, 2);
      json += "}";
                       
      if (mqttClient.publish(mqtt_topic, json.c_str())) {
        Serial.println("\n[MQTT 15s] Da gui -> Rung: " + String(vibration,1) + " | Am: " + String(soilMoisturePercent) + "%");
      }
    }
  }

  // HTTP (30s)
  if (currentMillis - lastHttpTime >= HTTP_INTERVAL) {
    lastHttpTime = currentMillis;
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\n[HTTP 30s] Dang gui luu tru...");
      HTTPClient http;
      String url = "http://api.thingspeak.com/update?api_key=" + writeAPIKey + 
                   "&field1=" + String(soilMoisturePercent) + 
                   "&field2=" + String(vibration, 2);
      
      http.begin(url);
      int httpCode = http.GET(); 
      if (httpCode == 200) { Serial.println("[HTTP] Thanh cong!"); }
      http.end(); 
    }
  }
}
