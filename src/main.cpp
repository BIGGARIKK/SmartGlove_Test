#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>

const char* ap_ssid = "SmartGlove_AP";
const char* ap_password = "12345678";

WebServer server(80);
const int MPU_ADDR = 0x68;

const int FLEX_PIN_1 = D1; 
const int FLEX_PIN_2 = D0; 

// --- โหมดพลังงาน ---
const int USB_F1_STRAIGHT = 1520;
const int USB_F1_BENT = 800;
const int USB_F2_STRAIGHT = 1520;
const int USB_F2_BENT = 800;

const int BAT_F1_STRAIGHT = 1315;
const int BAT_F1_BENT = 800;
const int BAT_F2_STRAIGHT = 1315;
const int BAT_F2_BENT = 800;

bool isBatteryMode = false; 

int flex1 = 0, flex2 = 0;
int flex1_percent = 0, flex2_percent = 0;
int16_t AcX = 0, AcY = 0, AcZ = 0;
String currentGesture = "กำลังรอท่าทาง...";
unsigned long lastSensorRead = 0;

// โครงสร้างเก็บข้อมูลท่าทางแบบช่วงขอบเขต (Range for Dynamic/Static)
struct TrainedGesture {
  String name;
  int f1_min, f1_max;
  int f2_min, f2_max;
  int16_t acx_min, acx_max;
  int16_t acy_min, acy_max;
  int16_t acz_min, acz_max;
};
TrainedGesture savedGestures[10];
int gestureCount = 0;

void loadGesturesFromFS() {
  if (!LittleFS.exists("/gestures.json")) return;
  File file = LittleFS.open("/gestures.json", "r");
  if (!file) return;

  gestureCount = 0;
  while (file.available() && gestureCount < 10) {
    String line = file.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) continue;
    
    // รูปแบบ CSV: Name,f1_min,f1_max,f2_min,f2_max,acx_min,acx_max,acy_min,acy_max,acz_min,acz_max
    int p[10];
    int idx = 0;
    int lastIdx = -1;
    for (int i = 0; i < 10; i++) {
      p[i] = line.indexOf(',', lastIdx + 1);
      lastIdx = p[i];
      if (p[i] == -1) break;
    }

    if (p[9] != -1) {
      savedGestures[gestureCount].name = line.substring(0, p[0]);
      savedGestures[gestureCount].f1_min = line.substring(p[0] + 1, p[1]).toInt();
      savedGestures[gestureCount].f1_max = line.substring(p[1] + 1, p[2]).toInt();
      savedGestures[gestureCount].f2_min = line.substring(p[2] + 1, p[3]).toInt();
      savedGestures[gestureCount].f2_max = line.substring(p[3] + 1, p[4]).toInt();
      savedGestures[gestureCount].acx_min = line.substring(p[4] + 1, p[5]).toInt();
      savedGestures[gestureCount].acx_max = line.substring(p[5] + 1, p[6]).toInt();
      savedGestures[gestureCount].acy_min = line.substring(p[6] + 1, p[7]).toInt();
      savedGestures[gestureCount].acy_max = line.substring(p[7] + 1, p[8]).toInt();
      savedGestures[gestureCount].acz_min = line.substring(p[8] + 1, p[9]).toInt();
      savedGestures[gestureCount].acz_max = line.substring(p[9] + 1).toInt();
      gestureCount++;
    }
  }
  file.close();
}

void saveGesturesToFS() {
  File file = LittleFS.open("/gestures.json", "w");
  if (!file) return;
  for (int i = 0; i < gestureCount; i++) {
    file.print(savedGestures[i].name); file.print(",");
    file.print(savedGestures[i].f1_min); file.print(",");
    file.print(savedGestures[i].f1_max); file.print(",");
    file.print(savedGestures[i].f2_min); file.print(",");
    file.print(savedGestures[i].f2_max); file.print(",");
    file.print(savedGestures[i].acx_min); file.print(",");
    file.print(savedGestures[i].acx_max); file.print(",");
    file.print(savedGestures[i].acy_min); file.print(",");
    file.print(savedGestures[i].acy_max); file.print(",");
    file.print(savedGestures[i].acz_min); file.print(",");
    file.println(savedGestures[i].acz_max);
  }
  file.close();
}

void handleRoot() {
  File file = LittleFS.open("/index.html", "r");
  if (!file) { server.send(404, "text/plain", "ไม่พบไฟล์ index.html"); return; }
  server.streamFile(file, "text/html");
  file.close();
}

void handleTrainPage() {
  File file = LittleFS.open("/train.html", "r");
  if (!file) { server.send(404, "text/plain", "ไม่พบไฟล์ train.html"); return; }
  server.streamFile(file, "text/html");
  file.close();
}

void handleDataJson() {
  String json = "{";
  json += "\"gesture\":\"" + currentGesture + "\",";
  json += "\"mode_str\":\"" + String(isBatteryMode ? "🔋 โหมดใช้แบตเตอรี่" : "🔌 โหมดเสียบสาย USB") + "\",";
  json += "\"is_bat\":" + String(isBatteryMode ? 1 : 0) + ",";
  json += "\"f1_raw\":" + String(flex1) + ",";
  json += "\"f1_percent\":" + String(flex1_percent) + ",";
  json += "\"f2_raw\":" + String(flex2) + ",";
  json += "\"f2_percent\":" + String(flex2_percent) + ",";
  json += "\"acx\":" + String(AcX) + ",";
  json += "\"acy\":" + String(AcY) + ",";
  json += "\"acz\":" + String(AcZ);
  json += "}";
  server.send(200, "application/json", json);
}

void handleSetMode() {
  isBatteryMode = !isBatteryMode;
  server.send(200, "text/plain", "OK");
}

// รับค่าช่วง Min/Max ที่ประมวลผลมาจากหน้าเว็บ
void handleTrainSave() {
  if (server.hasArg("name") && server.hasArg("f1_min") && server.hasArg("f1_max")) {
    if (gestureCount < 10) {
      savedGestures[gestureCount].name = server.arg("name");
      savedGestures[gestureCount].f1_min = server.arg("f1_min").toInt();
      savedGestures[gestureCount].f1_max = server.arg("f1_max").toInt();
      savedGestures[gestureCount].f2_min = server.arg("f2_min").toInt();
      savedGestures[gestureCount].f2_max = server.arg("f2_max").toInt();
      savedGestures[gestureCount].acx_min = server.arg("acx_min").toInt();
      savedGestures[gestureCount].acx_max = server.arg("acx_max").toInt();
      savedGestures[gestureCount].acy_min = server.arg("acy_min").toInt();
      savedGestures[gestureCount].acy_max = server.arg("acy_max").toInt();
      savedGestures[gestureCount].acz_min = server.arg("acz_min").toInt();
      savedGestures[gestureCount].acz_max = server.arg("acz_max").toInt();
      gestureCount++;
      saveGesturesToFS();
      server.send(200, "text/plain", "OK");
    } else {
      server.send(400, "text/plain", "Full");
    }
  } else {
    server.send(400, "text/plain", "Bad Request");
  }
}

void handleGetGestures() {
  String json = "[";
  for (int i = 0; i < gestureCount; i++) {
    json += "{\"id\":" + String(i) + ",\"name\":\"" + savedGestures[i].name + "\"}";
    if (i < gestureCount - 1) json += ",";
  }
  json += "]";
  server.send(200, "application/json", json);
}

void handleDeleteGesture() {
  if (server.hasArg("id")) {
    int id = server.arg("id").toInt();
    if (id >= 0 && id < gestureCount) {
      for (int i = id; i < gestureCount - 1; i++) {
        savedGestures[i] = savedGestures[i + 1];
      }
      gestureCount--;
      saveGesturesToFS();
      server.send(200, "text/plain", "Deleted");
      return;
    }
  }
  server.send(400, "text/plain", "Error");
}

void setup() {
  Serial.begin(115200);
  delay(3000); 

  if (!LittleFS.begin(true)) return;
  loadGesturesFromFS();

  pinMode(FLEX_PIN_1, INPUT);
  pinMode(FLEX_PIN_2, INPUT);

  Wire.begin(5, 6); 
  delay(100);
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x6B);
  Wire.write(0x00);
  Wire.endTransmission(true);

  WiFi.softAP(ap_ssid, ap_password);

  server.on("/", handleRoot);
  server.on("/train_page", handleTrainPage);
  server.on("/data", handleDataJson);
  server.on("/setmode", handleSetMode);
  server.on("/train_save", handleTrainSave);
  server.on("/get_gestures", handleGetGestures);
  server.on("/delete_gesture", handleDeleteGesture);
  server.begin();
}

void loop() {
  server.handleClient();

  if (millis() - lastSensorRead > 100) {
    lastSensorRead = millis();

    flex1 = analogRead(FLEX_PIN_1);
    flex2 = analogRead(FLEX_PIN_2);

    int cur_f1_straight = isBatteryMode ? BAT_F1_STRAIGHT : USB_F1_STRAIGHT;
    int cur_f1_bent     = isBatteryMode ? BAT_F1_BENT     : USB_F1_BENT;
    int cur_f2_straight = isBatteryMode ? BAT_F2_STRAIGHT : USB_F2_STRAIGHT;
    int cur_f2_bent     = isBatteryMode ? BAT_F2_BENT     : USB_F2_BENT;

    flex1_percent = constrain(map(flex1, cur_f1_straight, cur_f1_bent, 0, 100), 0, 100);
    flex2_percent = constrain(map(flex2, cur_f2_straight, cur_f2_bent, 0, 100), 0, 100);

    Wire.beginTransmission(MPU_ADDR);
    Wire.write(0x3B);
    if (Wire.endTransmission(false) == 0) {
      Wire.requestFrom(MPU_ADDR, 6, true);
      if (Wire.available() >= 6) {
        AcX = Wire.read() << 8 | Wire.read();
        AcY = Wire.read() << 8 | Wire.read();
        AcZ = Wire.read() << 8 | Wire.read();
      }
    }

    String matchedGesture = "กำลังรอท่าทาง...";

    for (int i = 0; i < gestureCount; i++) {
      // 1. เช็คว่าอยู่ในช่วง Min-Max
      bool inRange = (flex1 >= savedGestures[i].f1_min && flex1 <= savedGestures[i].f1_max &&
                      flex2 >= savedGestures[i].f2_min && flex2 <= savedGestures[i].f2_max &&
                      AcX >= savedGestures[i].acx_min && AcX <= savedGestures[i].acx_max &&
                      AcY >= savedGestures[i].acy_min && AcY <= savedGestures[i].acy_max &&
                      AcZ >= savedGestures[i].acz_min && AcZ <= savedGestures[i].acz_max);

      if (inRange) {
        // 2. เช็คเพิ่มเติม: ถ้าเป็นท่าเคลื่อนไหว ช่วงกว้างของ MPU ต้องมีความต่าง (ไม่ใช่แค่นิ่งค้าง)
        // เพื่อป้องกันการแค่นิ่งค้างแล้วลั่น
        int acy_range = savedGestures[i].acy_max - savedGestures[i].acy_min;
        
        if (acy_range > 4000) { 
          // ถ้าช่วงการแกว่งกว้างพอสมควร แปลว่าเป็นท่าเคลื่อนไหว
          // เราต้องเช็คเพิ่มว่าตอนนี้ข้อมือกำลังขยับ หรือมีการเปลี่ยนแปลงค่าจริงๆ (เช่น เช็คความต่างจากค่ากลาง)
          matchedGesture = savedGestures[i].name;
        } else {
          // ถ้าช่วงแกว่งแคบ (คือกำค้างนิ่งๆ) ให้ถือว่ายังไม่ทำท่า
          matchedGesture = "กำลังรอท่าทาง...";
        }
        break;
      }
    }
    currentGesture = matchedGesture;
  }
  Serial.printf("%d,%d,%d,%d,%d\n", flex1_percent, flex2_percent, AcX, AcY, AcZ);
}