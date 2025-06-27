#include <Arduino.h>
#include <Keypad.h>
#include "data.h"
#include <WiFi.h>
#include <Wire.h>
#include <ESPAsyncWebServer.h>
#include "FS.h"
#include "SPIFFS.h"
#include "LiquidCrystal_I2C.h"

// #define INIT_RTC
//----------------------------------------RTC---------------------------------------
#define DS3231_READ  0xD1
#define DS3231_WRITE 0xD0
#define DS3231_ADDR  0x68

#define DS3231_SECONDS  0x00
#define DS3231_MINUTES  0x01
#define DS3231_HOURS  0x02
#define DS3231_DAY 0x03
#define DS3231_DATE 0x04
#define DS3231_CEN_MONTH 0x05
#define DS3231_DEC_YEAR 0x06
#define DS3231_TEMP_MSB 0x11
#define DS3231_TEMP_LSB 0x12

// 12 14  27 26 25 33 32 13
const byte ROWS = 4;
const byte COLS = 4;
char keys[ROWS][COLS] = {
{'1','2','3', 'A'},
{'4','5','6', 'B'},
{'7','8','9', 'C'},
{'*','0','#', 'D'}
};

byte rowPins[ROWS] = {12, 14, 27, 26}; //connect to the row pinouts of the kpd
byte colPins[COLS] = {25, 33, 32, 13}; //connect to the column pinouts of the kpd
const char* ssid = "RTR_Attendance";
const char* password = "RTR@2023";

// Objects
AsyncWebServer server(80);
LiquidCrystal_I2C lcd(0x27, 16, 2);
Keypad keypad = Keypad( makeKeymap(keys), rowPins, colPins, ROWS, COLS );

char key;
uint8_t value = 0;
char const * wdays[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
uint8_t hours, minutes, seconds, datee, month, yearr, day ;
String dataMessage;
float temp;
ulong timer_A;

// Function Prototypes
void getDate();
void getTime();
uint8_t toBcd(uint8_t num);
uint8_t fromBcd(uint8_t bcd);
uint8_t readRegister(uint8_t reg);
void writeRegister(uint8_t reg, uint8_t data);
void logSDCard();
void listDir(fs::FS &fs, const char * dirname, uint8_t levels);
void readFile(fs::FS &fs, const char * path);
void writeFile(fs::FS &fs, const char * path, const char * message);
void appendFile(fs::FS &fs, const char * path, const char * message);
void renameFile(fs::FS &fs, const char * path1, const char * path2);
void deleteFile(fs::FS &fs, const char * path);
void markAttendance(int rollNum, String name);
void markDeparture(int rollNum, String name);

void setup(){
   Serial.begin(115200);
   Wire.begin();
   Serial.print("Setting AP (Access Point)…");
   WiFi.softAP(ssid, password);

   IPAddress IP = WiFi.softAPIP();
   Serial.print("AP IP address: ");
   Serial.println(IP);

   server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
             { request->send(SPIFFS, "/index.html", "text/html"); });

   server.on("/csv", HTTP_GET, [](AsyncWebServerRequest *request)
             { request->send(SPIFFS, "/RTR_Attendance.csv", "text/csv"); });

   server.on("/test", HTTP_GET, [](AsyncWebServerRequest * request) {
   request->send_P(200, "text/plain", "Hello World");
   });

  server.begin();

  if (!SPIFFS.begin(true)){
     Serial.println("An Error has occurred while mounting SPIFFS");
     return;
  }

  File file = SPIFFS.open("/test.txt");
  if (!file){
     Serial.println("Failed to open file for reading");
     return;
  }

  Serial.println("File Content:");
  while (file.available()){
     Serial.print((char)file.read());
  }
  file.close();

#ifdef INIT_RTC
  writeRegister(DS3231_HOURS, 11);
  writeRegister(DS3231_MINUTES, 42);
  writeRegister(DS3231_SECONDS, 0);

  writeRegister(DS3231_DAY, 3);
  writeRegister(DS3231_DATE, 21);
  writeRegister(DS3231_CEN_MONTH, 12);
  writeRegister(DS3231_DEC_YEAR, 22);
#endif
  getDate();
  getTime();
  lcd.init();
  lcd.backlight();
  lcd.print(wdays[day]);
  lcd.print(" " + (String)datee + ":" + (String)month + ":" + (String)yearr);
  lcd.setCursor(0, 1);
  lcd.print("* Arr | D Depart");
  lcd.setCursor(0, 0);
  Serial.println(" ");
}

void loop() {
  key = keypad.getKey();

  if(key == '*'){
    lcd.clear();
    lcd.print("Enter Your RNum");
    lcd.setCursor(0,1);
    key = ' ';
    while(key == NULL || key == ' ' || key == '*' || key == 'B' || key == 'D' || key == 'A'){
      key = keypad.getKey();
      if(key == 'B' || key == 'D' || key == 'A'){
         lcd.setCursor(0,0);
         lcd.print("Enter Valid Num:");
      }
    } 
    if(key == 'C'){
       getDate();
       getTime();
       lcd.clear();
       lcd.print(wdays[day]);
       lcd.print(" " + (String)datee + ":" + (String)month + ":" + (String)yearr);
       lcd.setCursor(0, 1);
       lcd.print("* Arr | D Depart");
       lcd.setCursor(0, 0);
       return;
    }
    lcd.setCursor(0, 1);
    lcd.print(key);
    value = (int)key - 48;
    value = value * 10;
    key = ' ';
    while(key == NULL || key == ' ' || key == '*' || key == 'B' || key == 'D' || key == 'A'){
       key = keypad.getKey();
       if (key == 'B' || key == 'D' || key == 'A'){
         lcd.setCursor(0, 0);
         lcd.print("Enter Valid Num:");
       }
    }
    if(key == 'C'){
       getDate();
       getTime();
       lcd.clear();
       lcd.print(wdays[day]);
       lcd.print(" " + (String)datee + ":" + (String)month + ":" + (String)yearr);
       lcd.setCursor(0, 1);
       lcd.print("* Arr | D Depart");
       lcd.setCursor(0, 0);
       return;
    }
    lcd.print(key);
    value = value + ((int)key - 48);
    lcd.setCursor(0,0);
    lcd.print("# Confirm C Abrt");
   //  Serial.println("Press # to mark attendance or Press C to cancel");
    while(key != 'C' && key != '#') key = keypad.getKey();
    if(key == 'C'){
       getDate();
       getTime();
       lcd.clear();
       lcd.print(wdays[day]);
       lcd.print(" " + (String)datee + ":" + (String)month + ":" + (String)yearr);
       lcd.setCursor(0, 1);
       lcd.print("* Arr | D Depart");
       lcd.setCursor(0, 0);
       value = 0;
    } 
    else if (key == '#') {
      getDate();
      getTime();
      if (student_data.find(value) == student_data.end()){
         lcd.clear();
         lcd.setCursor(0, 0);
         lcd.print("Error");
         lcd.setCursor(0, 1);
         lcd.print("User Not Found");
         delay(2000);
      } else {
         auto student = student_data.find(value);
         markAttendance(value, student->second);
         lcd.clear();
         lcd.setCursor(0, 0);
         lcd.print("Welcome Back");
         lcd.setCursor(0, 1);
         lcd.print(student->second);
         delay(2000);
      }
         getDate();
         getTime();
         lcd.clear();
         lcd.print(wdays[day]);
         lcd.print(" " + (String)datee + ":" + (String)month + ":" + (String)yearr);
         lcd.setCursor(0, 1);
         lcd.print("* Arr | D Depart");
         lcd.setCursor(0, 0);
      }
    key = ' ';
  } else if(key == 'D'){
    lcd.clear();
    lcd.print("Enter Your RNum");
    lcd.setCursor(0, 1);
    key = ' ';
    while(key == NULL || key == ' ' || key == '*' || key == 'B' || key == 'A' || key == 'D'){
      key = keypad.getKey();
      if (key == 'B' || key == '*' || key == 'A' || key == 'D'){
         lcd.setCursor(0, 0);
         lcd.print("Enter Valid Num:");
      }
    }
    if(key == 'C'){
      getDate();
      getTime();
      lcd.clear();
      lcd.print(wdays[day]);
      lcd.print(" " + (String)datee + ":" + (String)month + ":" + (String)yearr);
      lcd.setCursor(0, 1);
      lcd.print("* Arr | D Depart");
      lcd.setCursor(0, 0);
      value = 0;
      return;
    }
    lcd.setCursor(0, 1);
    lcd.print(key);
    value = (int)key - 48;
    value = value * 10;
    key = ' ';
    while(key == NULL || key == ' ' || key == '*' || key == 'B' || key == '*' || key == 'A'){
      key = keypad.getKey();
      if (key == 'B' || key == '*' || key == 'A'){
         lcd.setCursor(0, 0);
         lcd.print("Enter Valid Num:");
      }
    }
    if(key == 'C'){
      getDate();
      getTime();
      lcd.clear();
      lcd.print(wdays[day]);
      lcd.print(" " + (String)datee + ":" + (String)month + ":" + (String)yearr);
      lcd.setCursor(0, 1);
      lcd.print("* Arr | D Depart");
      lcd.setCursor(0, 0);
      value = 0;
      return;
    }
    lcd.print(key);
    value = value + ((int)key - 48);
    lcd.setCursor(0, 0);
    lcd.print("# Confirm C Abrt");
   //  Serial.println("Press # to mark attendance or Press C to cancel");
    while(key != 'C' && key != '#') key = keypad.getKey();
    if (key == 'C'){
      getDate();
      getTime();
      lcd.clear();
      lcd.print(wdays[day]);
      lcd.print(" " + (String)datee + ":" + (String)month + ":" + (String)yearr);
      lcd.setCursor(0, 1);
      lcd.print("* Arr | D Depart");
      lcd.setCursor(0, 0);
      value = 0;
    }
    else if (key == '#') {
      getDate();
      getTime();
      if(student_data.find(value) == student_data.end()){
         lcd.clear();
         lcd.setCursor(0, 0);
         lcd.print("Error");
         lcd.setCursor(0,1);
         lcd.print("User Not Found");
         delay(2000);
      } else {
         auto student = student_data.find(value);
         markDeparture(value, student->second);
         lcd.clear();
         lcd.setCursor(0, 0);
         lcd.print("See You Soon");
         lcd.setCursor(0, 1);
         lcd.print(student->second);
         delay(2000);
      }

      getDate();
      getTime();
      lcd.clear();
      lcd.print(wdays[day]);
      lcd.print(" " + (String)datee + ":" + (String)month + ":" + (String)yearr);
      lcd.setCursor(0, 1);
      lcd.print("* Arr | D Depart");
      lcd.setCursor(0, 0);
    }
    key = ' ';
  }

  if(key == 'B'){
   lcd.clear();
   lcd.setCursor(0,0);
   lcd.print(WiFi.softAPIP());
   delay(2000);
   lcd.clear();
   lcd.print(wdays[day]);
   lcd.print(" " + (String)datee + ":" + (String)month + ":" + (String)yearr);
   lcd.setCursor(0, 1);
   lcd.print("* Arr | D Depart");
   lcd.setCursor(0, 0);
  }
}


void getDate() {
  day     = readRegister(DS3231_DAY)     ;
  hours   = readRegister(DS3231_HOURS)   ;
  minutes = readRegister(DS3231_MINUTES) ;
  seconds = readRegister(DS3231_SECONDS) ;
  datee   = readRegister(DS3231_DATE)    ;
  month   = readRegister(DS3231_CEN_MONTH);
  yearr   = readRegister(DS3231_DEC_YEAR) ;
  temp    = ( ( readRegister(DS3231_TEMP_MSB) << 8 | readRegister(DS3231_TEMP_LSB) ) >> 6 ) / 4.0f ;
}

void getTime() {
  hours   = readRegister(DS3231_HOURS)   ;
  minutes = readRegister(DS3231_MINUTES) ;
  seconds = readRegister(DS3231_SECONDS) ;
  yearr   = readRegister(DS3231_DEC_YEAR) ;
  temp    = ( ( readRegister(DS3231_TEMP_MSB) << 8 | readRegister(DS3231_TEMP_LSB) ) >> 6 ) / 4.0f ;
}

//----------------------------------------RTC FUNCTIONS---------------------------------------
uint8_t toBcd(uint8_t num){
  uint8_t bcd = ((num / 10) << 4) + (num % 10);
  return bcd;
}

uint8_t fromBcd(uint8_t bcd) {
  uint8_t num = (10 * ((bcd & 0xf0) >> 4)) + (bcd & 0x0f);
  return num;
}

uint8_t readRegister(uint8_t reg) {
  Wire.beginTransmission(DS3231_ADDR);
  Wire.write(reg);
  Wire.endTransmission();
  Wire.requestFrom(DS3231_ADDR, 1);
  return fromBcd(Wire.read());
}

void writeRegister(uint8_t reg, uint8_t data) {
  Wire.beginTransmission(DS3231_ADDR);
  Wire.write(reg);
  Wire.write(toBcd(data));
  Wire.endTransmission();
}


//----------------------------------------SD FUNCTIONS---------------------------------------
void markAttendance(int rollNum, String name) {
 dataMessage =  (String)datee + ":"  + (String)month + ":" + (String)yearr + ","
                +(String)hours + ":"  + (String)minutes + ":" + (String)seconds + ","
                +(String)rollNum  + "," + (String)name + "," + "Arrival" + "\n";

 Serial.print("Attendacne Marked: ");
 Serial.println(dataMessage);
 appendFile(SPIFFS, "/RTR_Attendance.csv", dataMessage.c_str());
}

void markDeparture(int rollNum, String name){
 dataMessage = (String)datee + ":" + (String)month + ":" + (String)yearr + ","
               + (String)hours + ":" + (String)minutes + ":" + (String)seconds + ","
               + (String)rollNum + "," + (String)name + "," + "Departure" + "\n";

 Serial.print("Attendacne Marked: ");
 Serial.println(dataMessage);
 appendFile(SPIFFS, "/RTR_Attendance.csv", dataMessage.c_str());
}

void listDir(fs::FS &fs, const char * dirname, uint8_t levels){
   Serial.printf("Listing directory: %s\r\n", dirname);

   File root = fs.open(dirname);
   if(!root){
      Serial.println("− failed to open directory");
      return;
   }
   if(!root.isDirectory()){
      Serial.println(" − not a directory");
      return;
   }

   File file = root.openNextFile();
   while(file){
      if(file.isDirectory()){
         Serial.print("  DIR : ");
         Serial.println(file.name());
         if(levels){
            listDir(fs, file.name(), levels -1);
         }
      } else {
         Serial.print("  FILE: ");
         Serial.print(file.name());
         Serial.print("\tSIZE: ");
         Serial.println(file.size());
      }
      file = root.openNextFile();
   }
}

void readFile(fs::FS &fs, const char * path){
   Serial.printf("Reading file: %s\r\n", path);

   File file = fs.open(path);
   if(!file || file.isDirectory()){
       Serial.println("− failed to open file for reading");
       return;
   }

   Serial.println("− read from file:");
   while(file.available()){
      Serial.write(file.read());
   }
}

void writeFile(fs::FS &fs, const char * path, const char * message){
   Serial.printf("Writing file: %s\r\n", path);

   File file = fs.open(path, FILE_WRITE);
   if(!file){
      Serial.println("− failed to open file for writing");
      return;
   }
   if(file.print(message)){
      Serial.println("− file written");
   }else {
      Serial.println("− frite failed");
   }
}

void appendFile(fs::FS &fs, const char * path, const char * message){
   Serial.printf("Appending to file: %s\r\n", path);

   File file = fs.open(path, FILE_APPEND);
   if(!file){
      Serial.println("− failed to open file for appending");
      return;
   }
   if(file.print(message)){
      Serial.println("− message appended");
   } else {
      Serial.println("− append failed");
   }
}

void renameFile(fs::FS &fs, const char * path1, const char * path2){
   Serial.printf("Renaming file %s to %s\r\n", path1, path2);
   if (fs.rename(path1, path2)) {
      Serial.println("− file renamed");
   } else {
      Serial.println("− rename failed");
   }
}

void deleteFile(fs::FS &fs, const char * path){
   Serial.printf("Deleting file: %s\r\n", path);
   if(fs.remove(path)){
      Serial.println("− file deleted");
   } else {
      Serial.println("− delete failed");
   }
}