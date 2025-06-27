#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Adafruit_Fingerprint.h>
#include <ESPAsyncWebServer.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include "time.h"

#include "SPIFFS.h"
#include "pitches.h"

#define TOUCH_PIN 23
#define BUZZER_PIN 26

const char* ssid = "ERTS";
const char* password = "balstre101";

// request format- GET https://script.google.com/macros/s/AKfycbwKpx01iNtau94br_nvXGwyTKx9CrIspYPxdzB7WPa4XVSvzdHu_NDloKQFXqWT7AoZ/exec?userId=1065&name=Akash&project=HardwareGrade&room=CC104

const char* csv_url = "https://docs.google.com/spreadsheets/d/1AxonN8zQ13o75t8qmSzUVAp1pmrbt7sMMXO5xpnx8FM/gviz/tq?tqx=out:csv&sheet=Sheet1";
const char* attendance_url = "https://script.google.com/macros/s/AKfycbwKpx01iNtau94br_nvXGwyTKx9CrIspYPxdzB7WPa4XVSvzdHu_NDloKQFXqWT7AoZ/exec";

const char* csv_file_path = "/data.csv";

const char* ntpServer = "ntp.iitb.ac.in";
const long  gmtOffset_sec =  19800; // 19800 seconds
const int   daylightOffset_sec = 0; // IST does NOT use daylight saving

struct Student {
  String user_id;
  String name;
  String project;
  String room;
};


int melody[] = {
  NOTE_C4, NOTE_G3, NOTE_G3, NOTE_A3, NOTE_G3, 0, NOTE_B3, NOTE_C4
};

int noteDurations[] = {
  4, 8, 8, 4, 4, 4, 4, 4
};

const size_t MAX_ENCODED_LEN = 256;
char encodedUserId[MAX_ENCODED_LEN];
char encodedName[MAX_ENCODED_LEN];
char encodedProject[MAX_ENCODED_LEN];
char encodedRoom[MAX_ENCODED_LEN];

int finger_id = 0, enroll_id =0;
bool enroll_mode = false;
bool _sync = false;

String delete_id;
unsigned long enroll_start_time = 0;

bool saveCSVtoSPIFFS(const char* csvInput, const char* filename = "/data.csv");
String urlEncode(const String &src);
bool getStudentByFingerprintId(const char* filename, int fingerprintId, Student &student);
String constructURL(const char* baseURL, const Student &s);
bool syncData();
bool postData(String url);

uint8_t getFingerprintEnroll(int id);
int getFingerprintID();
void initFingerprintSensor();
uint8_t deleteFingerprint(uint8_t id);
int getFingerprintIdByUserId(const char* filename, const char* targetUserId);
bool appendAttendance(const Student &student, unsigned long epochTime);

void beep(int duration_ms);
time_t getEpochTime();
void printLocalTime();

Student student = {
  .user_id = "",
  .name = "",
  .project = "",
  .room = ""
};

AsyncWebServer server(80);
Adafruit_Fingerprint finger = Adafruit_Fingerprint(&Serial2);
LiquidCrystal_I2C lcd(0x3F,16,2);  

void setup() {
  pinMode(TOUCH_PIN, INPUT_PULLUP);

  lcd.init();
  lcd.backlight();
  lcd.setCursor(0,0);
  lcd.print("Initializing...");

  Serial.begin(115200);
  initFingerprintSensor();

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Connecting to");
  lcd.setCursor(0, 1);
  lcd.print("WiFi...");

  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi...");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    lcd.setCursor(0, 7);
    lcd.print(".");
  }
  Serial.println(" connected!");

  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS mount failed");
    return;
  }

  Serial.println(WiFi.localIP());
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("eYSIP 2025!");
  lcd.setCursor(0, 1);
  lcd.print("Attendance");
  syncData();

  server.on("/", HTTP_GET, [](AsyncWebServerRequest * request) {
    request->send(SPIFFS, "/index.html", "text/html");
  });

  server.on("/enroll", HTTP_GET, [](AsyncWebServerRequest *request) {
    String userId;
    if (request->hasParam("userId")) {
      userId = request->getParam("userId")->value();
      char* end;
      long value = strtol(userId.c_str(), &end, 10);
      enroll_id = static_cast<int>(value);
      enroll_mode = true;
    }
    request->send(200, "text/plain", "Enrollment Started");
  });

  server.on("/csv", HTTP_GET, [](AsyncWebServerRequest * request) {
    request->send(SPIFFS, "/data.csv", "text/csv");
  });

  server.on("/attendance", HTTP_GET, [](AsyncWebServerRequest * request) {
    request->send(SPIFFS, "/attendance.csv", "text/csv");
  });

  server.on("/sync", HTTP_GET, [](AsyncWebServerRequest * request) {
    _sync = true;
    request->send(200, "text/plain", "Syncing data...");
  });

  server.on("/delete", HTTP_GET, [](AsyncWebServerRequest * request) {
    if (request->hasParam("userId")) {
      delete_id = request->getParam("userId")->value();
      char* end;
      long value = strtol(delete_id.c_str(), &end, 10);
      int delete_jaison  = static_cast<int>(value);
      deleteFingerprint(value);
    }
    request->send(200, "text/plain", "Deleted");
  });

  server.begin();

  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  printLocalTime();
}

void loop() {
  if(WiFi.status() != WL_CONNECTED){
    Serial.println("WiFi disconnected, attempting to reconnect...");
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      Serial.print(".");
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("WiFi");
      lcd.setCursor(0, 1);
      lcd.print("Disconnected");
    }
    Serial.println("Reconnected to WiFi!");
  }
  if(_sync){
    syncData();
    _sync = false;
  }
  // if(Serial.available() > 2){
  //   int data = Serial.parseInt();
  //   if(data == 69){
  //     File f = SPIFFS.open("/data.csv");
  //     while (f.available()) Serial.write(f.read());
  //     f.close();
  //     for (int thisNote = 0; thisNote < 8; thisNote++) {
  //       int noteDuration = 1000 / noteDurations[thisNote];
  //       tone(BUZZER_PIN, melody[thisNote], noteDuration);

  //       int pauseBetweenNotes = noteDuration * 1.30;
  //       delay(pauseBetweenNotes);
  //       noTone(BUZZER_PIN);
  //     }
  //   } else {
  //       Serial.println("Deleting fingerprint with ID: " + String(data));
  //       uint8_t result = deleteFingerprint(data);
  //   }
  // }

  if(!digitalRead(TOUCH_PIN) && !enroll_mode){
    finger_id = getFingerprintID();
    if (finger_id == -1){
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("No match found");
      delay(300);
      lcd.clear();
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("eYSIP 2025!");
      lcd.setCursor(0, 1);
      lcd.print("Attendance");
      Serial.println("No valid fingerprint found");
    } else {
      getStudentByFingerprintId(csv_file_path, finger_id, student);
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("ID: " + student.name);

      String url = constructURL(attendance_url, student);
      appendAttendance(student, getEpochTime());
      postData(url);
      finger_id = 0; // reset after processing

      delay(500);
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("eYSIP 2025!");
      lcd.setCursor(0, 1);
      lcd.print("Attendance");
    }
  } else if (enroll_mode) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Enroll ID: " + String(enroll_id));
    Serial.print("Enrolling fingerprint with ID: ");
    Serial.println(enroll_id);
    int temp = getFingerprintEnroll(getFingerprintIdByUserId(csv_file_path, String(enroll_id).c_str()));
    Serial.println("Enrollment completed for ID: " + String(temp));
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("eYSIP 2025!");
    lcd.setCursor(0, 1);
    lcd.print("Attendance");
    enroll_id = 0;
    enroll_mode = false;
  }
}


bool syncData(){
  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;

  Serial.println("Sending initial request...");
  Serial.println(csv_url);
  http.begin(client, csv_url);

  http.setUserAgent("Mozilla/5.0");

  int httpCode = http.GET();

  String redirectedUrl = csv_url;

  if (httpCode>0) {
    Serial.print("HTTP Response code: ");
    Serial.println(httpCode);
    String payload = http.getString();
    // Serial.println(payload);
    saveCSVtoSPIFFS(payload.c_str(), csv_file_path);
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Data Synced!");
    delay(1000);
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("eYSIP 2025!");
    lcd.setCursor(0, 1);
    lcd.print("Attendance");
    http.end();
    return true;
  } else {
    Serial.print("Error code: ");
    Serial.println(httpCode);
    http.end();
    return false;
  }
}

bool postData(String url){
  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  Serial.println("Sending POST request...");
  Serial.println(url);

  http.begin(client, url.c_str());

  http.setUserAgent("Mozilla/5.0");

  int httpCode = http.GET();

  if (httpCode>0) {
    Serial.print("HTTP Response code: ");
    Serial.println(httpCode);
    String payload = http.getString();
    lcd.clear();
    lcd.setCursor(0, 0); 
    lcd.print("Attendance");
    lcd.setCursor(0, 1);
    lcd.print("Maked!");
    delay(1000);
    // Serial.println(payload);
    http.end();
    return true;
  } else {
    Serial.print("Error code: ");
    Serial.println(httpCode);
    http.end();
    return false;
  }
}

uint8_t deleteFingerprint(uint8_t id) {
  uint8_t p = -1;

  p = finger.deleteModel(id);

  if (p == FINGERPRINT_OK) {
    Serial.println("Deleted!");
  } else if (p == FINGERPRINT_PACKETRECIEVEERR) {
    Serial.println("Communication error");
  } else if (p == FINGERPRINT_BADLOCATION) {
    Serial.println("Could not delete in that location");
  } else if (p == FINGERPRINT_FLASHERR) {
    Serial.println("Error writing to flash");
  } else {
    Serial.print("Unknown error: 0x"); Serial.println(p, HEX);
  }

  return p;
}

bool saveCSVtoSPIFFS(const char* csvInput, const char* filename) {
  File file = SPIFFS.open(filename, FILE_WRITE);
  if (!file) {
    Serial.println("Failed to open file for writing");
    return false;
  }

  const char* ptr = csvInput;
  bool isHeader = true;
  int fingerprintId = 1;

  while (*ptr) {
    const char* lineEnd = strchr(ptr, '\n');
    if (!lineEnd) lineEnd = ptr + strlen(ptr);
    String line(ptr, lineEnd - ptr);

    // Add Fingerprint ID column
    if (isHeader) {
      line += ",\"Fingerprint ID\"";
      isHeader = false;
    } else {
      line += ",\"" + String(fingerprintId++) + "\"";
    }

    file.println(line);

    if (*lineEnd == '\n') ptr = lineEnd + 1;
    else break;
  }

  file.close();
  Serial.println("CSV with linear Fingerprint ID saved to SPIFFS.");
  return true;
}

String urlEncode(const String &src) {
  const char* hex = "0123456789ABCDEF";
  String encoded = "";

  for (size_t i = 0; i < src.length(); ++i) {
    char c = src[i];
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      encoded += c;
    } else {
      encoded += '%';
      encoded += hex[(c >> 4) & 0xF];
      encoded += hex[c & 0xF];
    }
  }

  return encoded;
}

String constructURL(const char* baseURL, const Student &s) {
  String encodedUserId = urlEncode(s.user_id);
  String encodedName = urlEncode(s.name);
  String encodedProject = urlEncode(s.project);
  String encodedRoom = urlEncode(s.room);

  String finalURL = String(baseURL) + "?userId=" + encodedUserId + "&name=" + encodedName +
                    "&project=" + encodedProject + "&room=" + encodedRoom;

  return finalURL;
}

bool getStudentByFingerprintId(const char* filename, int fingerprintId, Student &student) {
  File file = SPIFFS.open(filename, FILE_READ);
  if (!file) {
    Serial.println("Failed to open file for reading");
    return false;
  }

  bool isHeader = true;
  while (file.available()) {
    String line = file.readStringUntil('\n');
    line.trim();

    if (isHeader) {
      isHeader = false; // skip header line
      continue;
    }

    // Parse CSV line with 5 quoted fields
    int quotePos = 0;
    String fields[5];

    for (int i = 0; i < 5; i++) {
      int startQuote = line.indexOf('\"', quotePos);
      if (startQuote == -1) break;
      int endQuote = line.indexOf('\"', startQuote + 1);
      if (endQuote == -1) break;

      fields[i] = line.substring(startQuote + 1, endQuote);
      quotePos = endQuote + 1;

      int commaPos = line.indexOf(',', quotePos);
      if (commaPos == -1 && i < 4) break;
      quotePos = commaPos + 1;
    }

    if (fields[4].toInt() == fingerprintId) {
      // Assign to student struct
      student.user_id = fields[0];
      student.name = fields[1];
      student.project = fields[2];
      student.room = fields[3];

      file.close();
      return true;
    }
  }

  file.close();
  return false;
}

void beep(int duration_ms) {
  digitalWrite(BUZZER_PIN, HIGH);
  delay(duration_ms);
  digitalWrite(BUZZER_PIN, LOW);
}

void initFingerprintSensor(){
  finger.begin(57600);

  if (finger.verifyPassword()) {
    Serial.println("Found fingerprint sensor!");
  } else {
    Serial.println("Did not find fingerprint sensor :(");
    while (1) { delay(1); }
  }

  finger.setSecurityLevel(2); 

  Serial.println(F("Reading sensor parameters"));
  finger.getParameters();
  Serial.print(F("Status: 0x")); Serial.println(finger.status_reg, HEX);
  Serial.print(F("Sys ID: 0x")); Serial.println(finger.system_id, HEX);
  Serial.print(F("Capacity: ")); Serial.println(finger.capacity);
  Serial.print(F("Security level: ")); Serial.println(finger.security_level);
  Serial.print(F("Device address: ")); Serial.println(finger.device_addr, HEX);
  Serial.print(F("Packet len: ")); Serial.println(finger.packet_len);
  Serial.print(F("Baud rate: ")); Serial.println(finger.baud_rate);


  finger.getTemplateCount();

  if (finger.templateCount == 0) {
    Serial.print("Sensor doesn't contain any fingerprint data. Please run the 'enroll' example.");
  }
  else {
    Serial.println("Waiting for valid finger...");
      Serial.print("Sensor contains "); Serial.print(finger.templateCount); Serial.println(" templates");
  }
}

uint8_t getFingerprintEnroll(int id) {
  int p = -1;
  lcd.setCursor(0, 1);
  lcd.print("                ");
  lcd.setCursor(0, 1);
  lcd.print("Press finger");
  delay(300);
  Serial.print("Waiting for valid finger to enroll as #"); Serial.println(id);
  enroll_start_time = millis();
  while (p != FINGERPRINT_OK) {
    p = finger.getImage();
    if (millis() - enroll_start_time > 5000) {
      p = FINGERPRINT_TIMEOUT;
      enroll_mode = false;
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Timeout!");
      delay(1000);
      Serial.println("Timeout while waiting for finger");
      return -1;
    }
    switch (p) {
    case FINGERPRINT_OK:
      lcd.setCursor(0, 1);
      lcd.print("                ");
      lcd.setCursor(0, 1);
      lcd.print("Image Taken");
      Serial.println("Image taken");
      break;
    case FINGERPRINT_NOFINGER:
      lcd.setCursor(0, 1);
      lcd.print("                ");
      lcd.setCursor(0, 1);
      lcd.print(".");
      // Serial.println(millis() - enroll_start_time);
      break;
    case FINGERPRINT_PACKETRECIEVEERR:
      lcd.setCursor(0, 1);
      lcd.print("                ");
      lcd.setCursor(0, 1);
      lcd.print("Comm error");
      Serial.println("Communication error");
      break;
    case FINGERPRINT_IMAGEFAIL:
      lcd.setCursor(0, 1);
      lcd.print("                ");
      lcd.setCursor(0, 1);      
      lcd.print("Image Error");
      Serial.println("Imaging error");
      break;
    case FINGERPRINT_TIMEOUT:
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Timeout!");
      delay(1000);
      break;
    default:
      lcd.setCursor(0, 1);
      lcd.print("                ");
      lcd.setCursor(0, 1);
      lcd.print("Unkown error");
      Serial.println("Unknown error");
      break;
    }
  }

  // OK success!
  // lcd.clear();
  p = finger.image2Tz(1);
  switch (p) {
    case FINGERPRINT_OK:
      Serial.println("Image converted");
      lcd.setCursor(0, 1);
      lcd.print("                ");
      lcd.setCursor(0, 1);
      lcd.print("IMG convert");
      break;
    case FINGERPRINT_IMAGEMESS:
      Serial.println("Image too messy");
      lcd.setCursor(0, 1);
      lcd.print("                ");
      lcd.setCursor(0, 1);
      lcd.print("IMG convert");
      return p;
    case FINGERPRINT_PACKETRECIEVEERR:
      Serial.println("Communication error");
      lcd.setCursor(0, 1);
      lcd.print("                ");
      lcd.setCursor(0, 1);
      lcd.print("Comm Error");
      return p;
    case FINGERPRINT_FEATUREFAIL:
      lcd.setCursor(0, 1);
      lcd.print("                ");
      lcd.setCursor(0, 1);
      lcd.print("Feature Error");
      Serial.println("Could not find fingerprint features");
      return p;
    case FINGERPRINT_INVALIDIMAGE:
      Serial.println("Could not find fingerprint features");
      return p;
    default:
      Serial.println("Unknown error");
      return p;
  }

  lcd.setCursor(0, 1);
  lcd.print("Remove finger");
  Serial.println("Remove finger");
  delay(2000);
  p = 0;
  while (p != FINGERPRINT_NOFINGER) {
    p = finger.getImage();
  }
  Serial.print("ID "); Serial.println(id);
  p = -1;
  lcd.clear();
  lcd.setCursor(0, 1);
  lcd.print("Place finger");
  delay(300);
  Serial.println("Place same finger again");
  enroll_start_time = millis();
  while (p != FINGERPRINT_OK) {
    p = finger.getImage();

    if (millis() - enroll_start_time > 10000) {
      p = FINGERPRINT_TIMEOUT;
      enroll_mode = false;
      Serial.println("Timeout while waiting for finger");
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("eYSIP 2025!");
      lcd.setCursor(0, 1);
      lcd.print("Attendance");
      return -1;
    }
    switch (p) {
    case FINGERPRINT_OK:
      lcd.setCursor(0, 1);
      lcd.print("                ");
      lcd.setCursor(0, 1);
      lcd.print("Image Taken");
      Serial.println("Image taken");
      delay(300);
      break;
    case FINGERPRINT_NOFINGER:
      lcd.setCursor(0, 1);
      lcd.print("                ");
      lcd.print(".");
      Serial.print(".");
      break;
    case FINGERPRINT_PACKETRECIEVEERR:
      lcd.setCursor(0, 1);
      lcd.print("                ");
      lcd.setCursor(0, 1);
      lcd.print("Comm error");
      Serial.println("Communication error");
      delay(300);
      break;
    case FINGERPRINT_IMAGEFAIL:
      lcd.setCursor(0, 1);
      lcd.print("                ");
      lcd.setCursor(0, 1);      
      lcd.print("Image Error");
      Serial.println("Imaging error");
      delay(300);
      break;
    default:
      lcd.setCursor(0, 1);
      lcd.print("                ");
      lcd.setCursor(0, 1);
      lcd.print("Unkown error");
      Serial.println("Unknown error");
      break;
    }
  }

  // OK success!

  p = finger.image2Tz(2);
  switch (p) {
    case FINGERPRINT_OK:
      Serial.println("Image converted");
      lcd.setCursor(0, 1);
      lcd.print("                ");
      lcd.setCursor(0, 1);
      lcd.print("IMG convert");
      delay(300);
      break;
    case FINGERPRINT_IMAGEMESS:
      Serial.println("Image too messy");
      lcd.setCursor(0, 1);
      lcd.print("                ");
      lcd.setCursor(0, 1);
      lcd.print("IMG convert");
      delay(300);
      return p;
    case FINGERPRINT_PACKETRECIEVEERR:
      Serial.println("Communication error");
      lcd.setCursor(0, 1);
      lcd.print("                ");
      lcd.setCursor(0, 1);
      lcd.print("Comm Error");
      delay(300);
      return p;
    case FINGERPRINT_FEATUREFAIL:
      lcd.setCursor(0, 1);
      lcd.print("                ");
      lcd.setCursor(0, 1);
      lcd.print("Feature Error");
      Serial.println("Could not find fingerprint features");
      delay(300);
      return p;
    case FINGERPRINT_INVALIDIMAGE:
      Serial.println("Could not find fingerprint features");
      return p;
    default:
      Serial.println("Unknown error");
      return p;
  }

  // OK converted!
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Creating model");
  lcd.setCursor(0, 1);
  lcd.print(String(id) + "...");
  Serial.print("Creating model for #");  Serial.println(id);
  delay(300);

  p = finger.createModel();
  if (p == FINGERPRINT_OK) {
    lcd.setCursor(0, 1);
    lcd.print("                ");
    lcd.setCursor(0, 1);
    lcd.print("Model created");
    Serial.println("Prints matched!");
    delay(500);
  } else if (p == FINGERPRINT_PACKETRECIEVEERR) {
    lcd.setCursor(0, 1);
    lcd.print("                ");
    lcd.setCursor(0, 1);
    lcd.print("Comm error");
    Serial.println("Communication error");
    delay(500);
    return p;
  } else if (p == FINGERPRINT_ENROLLMISMATCH) {
    lcd.setCursor(0, 1);
    lcd.print("                ");
    lcd.setCursor(0, 1);
    lcd.print("Didn't match");
    Serial.println("Fingerprints did not match");
    delay(500);
    return p;
  } else {
    Serial.println("Unknown error");
    return p;
  }

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Storing model");
  lcd.setCursor(0, 1);
  lcd.print(String(id));
  Serial.print("ID "); Serial.println(id);

  p = finger.storeModel(id);
  if (p == FINGERPRINT_OK) {
    lcd.setCursor(0, 1);
    lcd.print("                ");
    lcd.setCursor(0, 1);
    lcd.print("Stored!");
    delay(500);
    Serial.println("Stored!");
  } else if (p == FINGERPRINT_PACKETRECIEVEERR) {
    Serial.println("Communication error");
    return p;
  } else if (p == FINGERPRINT_BADLOCATION) {
    Serial.println("Could not store in that location");
    return p;
  } else if (p == FINGERPRINT_FLASHERR) {
    Serial.println("Error writing to flash");
    return p;
  } else {
    Serial.println("Unknown error");
    return p;
  }
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Done ID: ");
  lcd.print(String(id));
  delay(1000);
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("eYSIP 2025!");
  lcd.setCursor(0, 1);
  lcd.print("Attendance");
  return true;
}

int getFingerprintID() {
  uint8_t p = finger.getImage();
  if (p != FINGERPRINT_OK)  return -1;

  p = finger.image2Tz();
  if (p != FINGERPRINT_OK)  return -1;

  p = finger.fingerFastSearch();
  if (p != FINGERPRINT_OK)  return -1;

  // found a match!
  Serial.print("Found ID #"); Serial.print(finger.fingerID);
  Serial.print(" with confidence of "); Serial.println(finger.confidence);
  return finger.fingerID;
}

int getFingerprintIdByUserId(const char* filename, const char* targetUserId) {
  File file = SPIFFS.open(filename, FILE_READ);
  if (!file) {
    Serial.println("Failed to open CSV file.");
    return -1;
  }

  bool isHeader = true;
  while (file.available()) {
    String line = file.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) continue;

    // Skip header
    if (isHeader) {
      isHeader = false;
      continue;
    }

    // Parse quoted CSV fields
    int quotePos = 0;
    String fields[5];

    for (int i = 0; i < 5; i++) {
      int startQuote = line.indexOf('\"', quotePos);
      if (startQuote == -1) break;
      int endQuote = line.indexOf('\"', startQuote + 1);
      if (endQuote == -1) break;

      fields[i] = line.substring(startQuote + 1, endQuote);
      quotePos = endQuote + 1;

      int commaPos = line.indexOf(',', quotePos);
      if (commaPos == -1 && i < 4) break;
      quotePos = commaPos + 1;
    }

    if (fields[0] == targetUserId) {
      file.close();
      return fields[4].toInt();  // Fingerprint ID
    }
  }

  file.close();
  return -1;  // Not found
}

bool appendAttendance(const Student &student, unsigned long epochTime) {
  // Open the file in append mode
  File file = SPIFFS.open("/attendance.csv", "a");
  if (!file) {
    Serial.println("Failed to open attendance.csv for appending.");
    return false;
  }

  // Format the data as a CSV row
  String row = String(student.user_id) + "," +
               student.name + "," +
               student.project + "," +
               student.room + "," +
               String(epochTime) + "\n";

  // Write the row to the file
  if (file.print(row)) {
    Serial.println("Attendance record appended successfully.");
  } else {
    Serial.println("Failed to append attendance record.");
    file.close();
    return false;
  }

  // Close the file
  file.close();
  return true;
}

time_t getEpochTime() {
  time_t now;
  time(&now);
  return now;
}

void printLocalTime() {
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)){
    Serial.println("Failed to obtain time");
    return;
  }
  Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S");
  Serial.print("Day of week: ");
  Serial.println(&timeinfo, "%A");
  Serial.print("Month: ");
  Serial.println(&timeinfo, "%B");
  Serial.print("Day of Month: ");
  Serial.println(&timeinfo, "%d");
  Serial.print("Year: ");
  Serial.println(&timeinfo, "%Y");
  Serial.print("Hour: ");
  Serial.println(&timeinfo, "%H");
  Serial.print("Hour (12 hour format): ");
  Serial.println(&timeinfo, "%I");
  Serial.print("Minute: ");
  Serial.println(&timeinfo, "%M");
  Serial.print("Second: ");
  Serial.println(&timeinfo, "%S");

  Serial.println("Time variables");
  char timeHour[3];
  strftime(timeHour, 3, "%H", &timeinfo);
  Serial.println(timeHour);
  char timeWeekDay[10];
  strftime(timeWeekDay, 10, "%A", &timeinfo);
  Serial.println(timeWeekDay);
  Serial.println();
}
