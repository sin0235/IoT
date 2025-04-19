/*********************** LIBRARIES **************************/
#include <LiquidCrystal_I2C.h>          // LCD I2C
#include <I2CKeyPad.h>                  // Keypad via PCF8574 (I²C)
#include <Wire.h>                       // I2C communication
#include <SPI.h>                        // SPI for RFID
#include <MFRC522.h>                    // RFID RC522
#include <SoftwareSerial.h>             // For fingerprint sensor
#include <Adafruit_Fingerprint.h>       // AS608 fingerprint sensor
#include <Servo.h>                      // Servo control
#include <EEPROM.h>                     // User data storage

/*********************** PIN DEFINITIONS **************************/
#define PIN_LCD_SDA  A4
#define PIN_LCD_SCL  A5
#define PCF8574_ADDR 0x20               // I2C address for PCF8574 keypad module
#define BEEP_FREQ    1000 // 1 kHz
#define SHORT_BEEP   150  // ms cho bíp ngắn
#define SHORT_PAUSE  100// ms giữa các bíp
#define LONG_BEEP    500 // ms cho bíp dài cuối cùng

// Fingerprint sensor AS608
#define PIN_FINGER_RX 2                 // Arduino RX (connects to AS608 TX)
#define PIN_FINGER_TX 3                 // Arduino TX (connects to AS608 RX)

// RFID RC522 - using SPI (SPI pins: D13, D11, D12)
#define PIN_RFID_SDA 4                  // RFID SDA (SS) connected to Arduino D4
#define PIN_RFID_RST 5                  // RFID RST connected to Arduino D5

// Other IO pins
#define PIN_RELAY 6                     // Electric lock relay
#define PIN_BUZZER 7                    // Buzzer
#define PIN_SERVO 9                     // SG90 servo
#define PIN_DOOR_BTN 10                 // Door open button (pulled up to 5V)

// EEPROM addresses
#define EEPROM_COUNT_ADDR 0             // User count address
#define ADMIN_PIN_EEPROM_ADDR 100       // Admin PIN address

// Timing constants
#define DOOR_OPEN_DURATION 7000         // Door stays open for 7 seconds
#define LOCKOUT_DURATION 30000          // 30 seconds lockout after failed attempts
#define TIMEOUT_DURATION 10000          // 10 seconds timeout for operations
#define SHORT_DELAY 500                 // Short delay for UI feedback
#define MEDIUM_DELAY 1000               // Medium delay
#define LONG_DELAY 2000                 // Long delay
#define DEBOUNCE_DELAY 200              // Button debounce delay

/*********************** USER STRUCTURE **************************/
#define MAX_USERS 10
#define USER_TYPE_FINGERPRINT 1
#define USER_TYPE_RFID        2
#define USER_TYPE_PIN         3

// User record structure - added 'active' field to support user deletion
struct UserRecord {
  uint8_t active;                       // 1: valid user, 0: deleted user
  uint8_t userType;                     // 1: FP, 2: RFID, 3: PIN
  uint8_t userID;                       // User ID number
  char credential[16];                  // Authentication data (PIN, RFID UID, FP ID)
};

/*********************** SYSTEM STATE DEFINITIONS **************************/
enum SystemState {
  STATE_IDLE,                           // System waiting for input
  STATE_PIN_ENTRY,                      // User entering PIN
  STATE_DOOR_OPEN,                      // Door is unlocked
  STATE_ADMIN_MENU,                     // In admin mode
  STATE_LOCKOUT                         // Temporary lockout due to failed attempts
};

/*********************** GLOBAL VARIABLES **************************/
// LCD initialization
LiquidCrystal_I2C lcd(0x27, 16, 2);

// Keypad initialization
I2CKeyPad keypad(PCF8574_ADDR);

// RFID RC522
MFRC522 mfrc522(PIN_RFID_SDA, PIN_RFID_RST);

// Fingerprint sensor
SoftwareSerial fingerSerial(PIN_FINGER_RX, PIN_FINGER_TX);
Adafruit_Fingerprint finger = Adafruit_Fingerprint(&fingerSerial);

// Door servo
Servo doorServo;

// System state variables
SystemState currentState = STATE_IDLE;
char inputPINBuffer[17] = {0};          // Buffer for PIN entry
uint8_t inputPINLength = 0;
uint8_t failCount = 0;
bool doorOpen = false;
unsigned long doorOpenTime = 0;
unsigned long lockoutStart = 0;
int adminMenuPage = 0;
String tempString = "";                 // For temporary string operations
// Thêm vào phần khai báo biến global
unsigned long lastKeyPressTime = 0;
uint8_t lastKeyCode = 0;

// Admin PIN stored in EEPROM
char adminPIN[5] = "0000";

// Custom characters for LCD
byte lockIcon[8] = {
  B01110, B10001, B10001, B11111, 
  B11011, B11011, B11111, B00000
};

byte unlockIcon[8] = {
  B01110, B10000, B10000, B11111, 
  B11011, B11011, B11111, B00000
};

byte fingerIcon[8] = {
  B01110, B11111, B11111, B11111, 
  B01110, B00100, B00000, B00000
};

byte cardIcon[8] = {
  B11111, B10001, B10101, B10101, 
  B10001, B11111, B00000, B00000
};

// Function prototypes
char mapKeypadCodeToChar(uint8_t keyCode);
void setupLCD();
void showIdleScreen();
void showPINEntry();
void beepOk();
void beepError();
void openDoor(const char* method);
void lockDoor();
int getUserCount();
void setUserCount(uint8_t count);
bool authenticateUser(uint8_t authType, const char* credential);
bool addUserRecord(UserRecord user);
bool updateUserRecord(int index, UserRecord &user);
bool removeUserRecord(uint8_t userID);
void listUsers();
void loadAdminPIN();
void storeAdminPIN(const char* newPIN);
void processRFID();
void processFingerprint();
void checkDoorButton();
void processRemoteCommand();
void adminModeMenu();
void handleIdleStateInput(char key);
void handlePinEntry(char key);
String getUIDFromRFIDTag();
int getFingerprintID();
void handleAddRFIDUser();
void handleAddPINUser();
void handleAddFingerprintUser();
void handleDeleteUser();
void handleChangeAdminPIN();
void showAuthResult(bool success, const char* method);
void showMenuPrompt(const char* title, const char* prompt);


/*********************** SETUP **************************/
void setup() {
  // Khởi tạo giao tiếp Serial cho cả debug và giao tiếp ESP32
  Serial.begin(9600);  // Đổi thành 9600 để khớp với cấu hình ESP32
  Serial.println(F("Door Access Control System"));
  
  // Chờ một chút để ổn định kết nối Serial
  delay(100);
  
  // Khởi tạo I2C
  Wire.begin();
  
  // Thiết lập LCD
  setupLCD();
  
  // Khởi tạo keypad
  keypad.begin();
  
  // Thiết lập RFID
  SPI.begin();
  delay(50);
  mfrc522.PCD_Init();
  delay(100);

  // Kiểm tra xem đầu đọc RFID có hoạt động không
  byte v = mfrc522.PCD_ReadRegister(mfrc522.VersionReg);
  if (v == 0x00 || v == 0xFF) {
    Serial.println(F("WARNING: RFID reader not working properly!"));
    lcd.clear();
    lcd.print(F("RFID Error!"));
    lcd.setCursor(0, 1);
    lcd.print(F("Check wiring"));
    delay(2000);
  } else {
    Serial.print(F("RFID reader found. Version: 0x"));
    Serial.println(v, HEX);
  }
  
  // Thiết lập cảm biến vân tay
  finger.begin(57600);
  if (finger.verifyPassword()) {
    Serial.println(F("Fingerprint sensor found"));
  } else {
    Serial.println(F("Fingerprint sensor not found"));
  }
  
  // Khởi tạo servo và đặt ở vị trí khóa
  doorServo.attach(PIN_SERVO);
  doorServo.write(0);
  doorServo.detach();
  
  // Cấu hình relay, buzzer và nút mở cửa
  pinMode(PIN_RELAY, OUTPUT);
  digitalWrite(PIN_RELAY, LOW);
  
  pinMode(PIN_BUZZER, OUTPUT);
  digitalWrite(PIN_BUZZER, LOW);
  
  pinMode(PIN_DOOR_BTN, INPUT_PULLUP);
  
  // Khởi tạo EEPROM nếu số lượng người dùng không hợp lệ
  if (getUserCount() > MAX_USERS) {
    setUserCount(0);
  }
  
  // Tải mã PIN quản trị từ EEPROM
  loadAdminPIN();
  
  // Gửi phản hồi ban đầu cho ESP32
  Serial.println(F("ARDUINO_READY"));
  
  // Sẵn sàng vận hành
  showIdleScreen();
  beepOk();
}

/*********************** MAIN LOOP **************************/
void loop() {
  // Lấy thời gian hiện tại cho các bộ hẹn giờ
  unsigned long currentTime = millis();
  
  // Luôn kiểm tra lệnh từ ESP32 với độ ưu tiên cao
  processRemoteCommand();
  
  // Xử lý hệ thống dựa trên trạng thái hiện tại
  switch (currentState) {
    case STATE_LOCKOUT:
      // Kiểm tra xem thời gian khóa đã hết chưa
      if (currentTime - lockoutStart > LOCKOUT_DURATION) {
        failCount = 0;
        currentState = STATE_IDLE;
        showIdleScreen();
      }
      // Trong thời gian khóa, chỉ xử lý lệnh từ xa
      break;
      
    case STATE_DOOR_OPEN:
      // Kiểm tra xem cửa có nên đóng không
      if (currentTime - doorOpenTime > DOOR_OPEN_DURATION) {
        // Thông báo sắp đóng cửa
        Serial.println(F("EVENT:DOOR_CLOSING"));
        
        // Đóng cửa
        lockDoor();
        currentState = STATE_IDLE;
        showIdleScreen();
      }
      
      // Khi cửa đang mở, vẫn xử lý các đầu vào
      processUserInput();
      checkDoorButton();
      break;
      
    case STATE_IDLE:
    case STATE_PIN_ENTRY:
      // Xử lý tất cả các phương thức đầu vào trong hoạt động bình thường
      processUserInput();
      checkDoorButton();
      
      // Kiểm tra các phương thức xác thực theo thời gian
      static unsigned long lastRFIDCheck = 0;
      if (currentTime - lastRFIDCheck > 100) {
        processRFID();
        lastRFIDCheck = currentTime;
      }
      
      static unsigned long lastFingerprintCheck = 0;
      if (currentTime - lastFingerprintCheck > 200) {
        processFingerprint();
        lastFingerprintCheck = currentTime;
      }
      break;
      
    case STATE_ADMIN_MENU:
      // Menu quản trị được xử lý riêng
      adminModeMenu();
      break;
  }
  
  // Phát hiện và báo cáo thay đổi trạng thái cửa
  static bool lastDoorState = false;
  if (doorOpen != lastDoorState) {
    lastDoorState = doorOpen;
    // Gửi cập nhật trạng thái cửa cho ESP32
    Serial.print(F("STATUS:"));
    Serial.println(doorOpen ? F("OPEN") : F("CLOSED"));
  }
  
  // Kiểm tra tính toàn vẹn của hệ thống theo định kỳ
  static unsigned long lastSystemCheck = 0;
  if (currentTime - lastSystemCheck > 30000) { // Mỗi 30 giây
    // Kiểm tra các cảm biến và thiết bị
    bool rfidOk = mfrc522.PCD_ReadRegister(mfrc522.VersionReg) != 0 &&
                 mfrc522.PCD_ReadRegister(mfrc522.VersionReg) != 0xFF;
    bool fpOk = finger.verifyPassword();
    
    // Gửi báo cáo trạng thái nếu có lỗi
    if (!rfidOk || !fpOk) {
      Serial.print(F("ALERT:DEVICE_ERROR,"));
      Serial.print(rfidOk ? F("RFID_OK") : F("RFID_ERROR"));
      Serial.print(F(","));
      Serial.println(fpOk ? F("FP_OK") : F("FP_ERROR"));
    }
    
    lastSystemCheck = currentTime;
  }
  
  // Giảm tải CPU
  delay(10);
}


/*********************** INPUT PROCESSING **************************/
void processUserInput() {
  uint8_t keyCode = keypad.getKey();
  unsigned long currentTime = millis();
    lastKeyPressTime = 0;
  lastKeyCode = 0;
  // Chỉ xử lý khi phím hợp lệ được nhấn (không phải 0 hoặc 16)
  if (keyCode != 0 && keyCode != 16) {
    // Kiểm tra chống dính phím
    if ((keyCode != lastKeyCode) || (currentTime - lastKeyPressTime >= DEBOUNCE_DELAY)) {
      char key = mapKeypadCodeToChar(keyCode);
      
      if (key != 0) {
        Serial.print(F("Key pressed: "));
        Serial.println(key);
        
        // Cập nhật thời gian nhấn phím và giá trị phím
        lastKeyCode = keyCode;
        lastKeyPressTime = currentTime;
        
        // Xử lý phím dựa trên trạng thái hiện tại
        if (currentState == STATE_IDLE) {
          handleIdleStateInput(key);
        } else if (currentState == STATE_PIN_ENTRY) {
          handlePinEntry(key);
        }
      }
    }
  } else {
    // Reset lastKeyCode nếu không có phím nào được nhấn trong một khoảng thời gian
    if (currentTime - lastKeyPressTime > DEBOUNCE_DELAY * 2) {
      lastKeyCode = 0;
    }
  }
  
  // Thêm độ trễ nhỏ để giảm tải cho CPU
  delay(10);
}

void handleIdleStateInput(char key) {
  if ((key >= '0' && key <= '9') || (key >= 'A' && key <= 'D')) {
    // Start PIN entry
    memset(inputPINBuffer, 0, sizeof(inputPINBuffer));
    inputPINLength = 0;
    
    // Add first digit
    inputPINBuffer[inputPINLength++] = key;
    inputPINBuffer[inputPINLength] = 0;
    
    currentState = STATE_PIN_ENTRY;
    showPINEntry();
  }
}

void handlePinEntry(char key) {
  if (key == '#') {
    // Check if PIN is admin PIN
    if (strcmp(inputPINBuffer, adminPIN) == 0) {
      // Enter admin mode
      lcd.clear();
      lcd.print(F("ADMIN MODE"));
      lcd.setCursor(0, 1);
      lcd.print(F("Accessing..."));
      delay(MEDIUM_DELAY);
      
      currentState = STATE_ADMIN_MENU;
      adminMenuPage = 0;
    } else {
      // Try to authenticate with PIN
      if (authenticateUser(USER_TYPE_PIN, inputPINBuffer)) {
        openDoor("PIN");
      } else {
        showAuthResult(false, "PIN");
        beepError();
        failCount++;
        
        // Check for lockout
        if (failCount >= 3) {
          activateLockout();
        } else {
          currentState = STATE_IDLE;
          showIdleScreen();
        }
      }
    }
    
    // Clear PIN buffer
    memset(inputPINBuffer, 0, sizeof(inputPINBuffer));
    inputPINLength = 0;
  } 
  else if (key == '*') {
    // Clear current PIN
    memset(inputPINBuffer, 0, sizeof(inputPINBuffer));
    inputPINLength = 0;
    lcd.clear();
    lcd.print(F("PIN Cleared"));
    delay(SHORT_DELAY);
    
    currentState = STATE_IDLE;
    showIdleScreen();
  } 
  else if ((key >= '0' && key <= '9') || (key >= 'A' && key <= 'D')) {
    // Add digit to PIN
    if (inputPINLength < 16) {
      inputPINBuffer[inputPINLength++] = key;
      inputPINBuffer[inputPINLength] = 0;
      showPINEntry();
    }
  }
}

void activateLockout() {
  currentState = STATE_LOCKOUT;
  lockoutStart = millis();
  
  lcd.clear();
  lcd.print(F("ALERT: Too many"));
  lcd.setCursor(0, 1);
  lcd.print(F("failed attempts"));
  
  tone(PIN_BUZZER, 2000);
  Serial.println(F("ALERT:AUTH_FAIL_MAX"));
  delay(LONG_DELAY);
  noTone(PIN_BUZZER);
  beepWarning();
}



// Hàm cảnh báo bằng buzzer
// times: số lần bíp ngắn trước khi bíp dài
void beepWarning() {
  for (uint8_t i = 0; i < 5; i++) {
    tone(PIN_BUZZER, BEEP_FREQ);
    delay(SHORT_BEEP);
    noTone(PIN_BUZZER);
    delay(SHORT_PAUSE);
  }
  // Bíp dài kết thúc
  tone(PIN_BUZZER, BEEP_FREQ);
  delay(LONG_BEEP);
  noTone(PIN_BUZZER);
}

/*********************** RFID PROCESSING **************************/
void processRFID() {
  if (!mfrc522.PICC_IsNewCardPresent()) return;
  if (!mfrc522.PICC_ReadCardSerial()) return;
  
  String uidStr = getUIDFromRFIDTag();
  
  lcd.clear();
  lcd.write(3); // Card icon
  lcd.print(F(" RFID:"));
  lcd.setCursor(0, 1);
  lcd.print(uidStr);
  delay(MEDIUM_DELAY);
  
  if (authenticateUser(USER_TYPE_RFID, uidStr.c_str())) {
    openDoor("RFID");
  } else {
    showAuthResult(false, "RFID");
    beepError();
    failCount++;
    
    if (failCount >= 3) {
      activateLockout();
    }
  }
  
  mfrc522.PICC_HaltA();
}

String getUIDFromRFIDTag() {
  String uidStr = "";
  for (byte i = 0; i < mfrc522.uid.size; i++) {
    if (mfrc522.uid.uidByte[i] < 0x10)
      uidStr += "0";
    uidStr += String(mfrc522.uid.uidByte[i], HEX);
  }
  uidStr.toUpperCase();
  return uidStr;
}

/*********************** FINGERPRINT PROCESSING **************************/
void processFingerprint() {
  int fpID = getFingerprintID();
  if (fpID != -1) {
    // Convert fpID to string for authentication
    char fpStr[8];
    sprintf(fpStr, "%d", fpID);
    
    if (authenticateUser(USER_TYPE_FINGERPRINT, fpStr)) {
      openDoor("Finger");
    } else {
      showAuthResult(false, "Finger");
      beepError();
      failCount++;
      
      if (failCount >= 3) {
        activateLockout();
      }
    }
  }
}

int getFingerprintID() {
  uint8_t p = finger.getImage();
  if (p != FINGERPRINT_OK) return -1;
  
  p = finger.image2Tz();
  if (p != FINGERPRINT_OK) return -1;
  
  p = finger.fingerFastSearch();
  if (p == FINGERPRINT_OK)
    return finger.fingerID;
  else
    return -1;
}

/*********************** DOOR BUTTON PROCESSING **************************/
void checkDoorButton() {
  static unsigned long lastDoorButtonTime = 0;
  static bool buttonPressProcessed = false;
  
  if (digitalRead(PIN_DOOR_BTN) == LOW) {
    // Debounce button
    if (millis() - lastDoorButtonTime > 500 && !buttonPressProcessed) {
      lcd.clear();
      lcd.print(F("Door button"));
      lcd.setCursor(0, 1);
      lcd.print(F("pressed"));
      
      openDoor("Button");
      lastDoorButtonTime = millis();
      buttonPressProcessed = true;
    }
  } else {
    // Reset button state when released
    buttonPressProcessed = false;
  }
}

void beepSuccess() {
  for (uint8_t i = 0; i < 2; i++) {
    tone(PIN_BUZZER, BEEP_FREQ);
    delay(SHORT_BEEP);
    noTone(PIN_BUZZER);
    delay(SHORT_PAUSE);
  }
}


/*********************** DOOR CONTROL **************************/
void openDoor(const char* method) {
  showAuthResult(true, method);
  beepSuccess();
  
  // Ghi log sự kiện truy cập
  Serial.print(F("EVENT:ACCESS_GRANTED,"));
  Serial.println(method);
  
  // Kích hoạt servo một cách đáng tin cậy
  doorServo.attach(PIN_SERVO);
  delay(50); // Đợi servo khởi động
  
  // Mở khóa cửa với góc mở phù hợp (điều chỉnh nếu cần)
  doorServo.write(110);
  digitalWrite(PIN_RELAY, HIGH);
  
  // Cập nhật trạng thái
  doorOpen = true;
  doorOpenTime = millis();
  failCount = 0;
  currentState = STATE_DOOR_OPEN;
  
  // Gửi xác nhận trạng thái
  Serial.println(F("STATUS:OPEN"));
  
  showIdleScreen();
}



void lockDoor() {
  // Đảm bảo servo được kết nối
  doorServo.attach(PIN_SERVO);
  delay(50); // Đợi servo khởi động

  
  // Đảm bảo vị trí cuối cùng chính xác
  doorServo.write(0);
  delay(200);
  
  // Tắt relay
  digitalWrite(PIN_RELAY, LOW);
  doorOpen = false;
  
  // Ngắt kết nối servo để tiết kiệm năng lượng và giảm rung
  delay(500); // Đợi servo ổn định
  doorServo.detach();
  
  // Gửi xác nhận trạng thái
  Serial.println(F("STATUS:CLOSED"));
  Serial.println(F("EVENT:DOOR_LOCKED"));
  
  showIdleScreen();
}


/*********************** LCD INTERFACE **************************/
void setupLCD() {
  lcd.init();
  lcd.backlight();
  
  // Register custom icons
  lcd.createChar(0, lockIcon);
  lcd.createChar(1, unlockIcon);
  lcd.createChar(2, fingerIcon);
  lcd.createChar(3, cardIcon);
  
  // Show startup screen
  lcd.clear();
  lcd.print(F("Door Access IoT"));
  lcd.setCursor(0, 1);
  lcd.print(F("System Ready"));
  delay(LONG_DELAY);
}

void showIdleScreen() {
  lcd.clear();
  lcd.write(doorOpen ? 1 : 0); // Show lock/unlock icon
  lcd.print(F(" Door "));
  lcd.print(doorOpen ? F("OPEN") : F("LOCKED"));
  lcd.setCursor(0, 1);
  lcd.print(F("PIN/#/RFID/FP"));
}

void showPINEntry() {
  lcd.clear();
  lcd.print(F("Enter PIN:"));
  lcd.setCursor(0, 1);
  
  // Show asterisks for PIN
  for (uint8_t i = 0; i < inputPINLength; i++) {
    lcd.print('*');
  }
  delay(100);
}

void showAuthResult(bool success, const char* method) {
  lcd.clear();
  if (success) {
    lcd.print(F("Access granted"));
    lcd.setCursor(0, 1);
    lcd.print(F("via "));
    lcd.print(method);
  } else {
    lcd.print(F("Access denied"));
    lcd.setCursor(0, 1);
    lcd.print(method);
    lcd.print(F(" ERROR"));
  }
  delay(MEDIUM_DELAY);
}

void showMenuPrompt(const char* title, const char* prompt) {
  lcd.clear();
  lcd.print(title);
  lcd.setCursor(0, 1);
  lcd.print(prompt);
}

/*********************** AUDIO FEEDBACK **************************/
void beepOk() {
  tone(PIN_BUZZER, 1000, 100);
  delay(150);
  tone(PIN_BUZZER, 1500, 100);
  delay(150);
  noTone(PIN_BUZZER);
}

void beepError() {
  for (int i = 0; i < 3; i++) {
    tone(PIN_BUZZER, 500, 100);
    delay(150);
  }
  noTone(PIN_BUZZER);
}

/*********************** ADMIN MENU **************************/
void adminModeMenu() {
  bool exitAdminMenu = false;
  
  // Biến cho cơ chế chống dính phím
  lastKeyPressTime = 0;
  lastKeyCode = 0;
  
  // Hiển thị trang menu hiện tại
  showAdminMenu(adminMenuPage);
  
  while (!exitAdminMenu) {
    uint8_t keyCode = keypad.getKey();
    unsigned long currentTime = millis();
    
    // Xử lý phím hợp lệ
    if (keyCode != 0 && keyCode != 16) {
      // Kiểm tra chống dính phím
      if ((keyCode != lastKeyCode) || (currentTime - lastKeyPressTime >= DEBOUNCE_DELAY)) {
        char key = mapKeypadCodeToChar(keyCode);
        
        if (key != 0) {
          // Cập nhật thời gian nhấn phím và mã phím
          lastKeyCode = keyCode;
          lastKeyPressTime = currentTime;
          
          Serial.print(F("Admin key: "));
          Serial.println(key);
          
          // Phản hồi âm thanh khi nhấn phím
          beepOk();
          
          // Xử lý điều hướng menu và tùy chọn
          switch (key) {
            case '2':
              handleAddRFIDUser();
              break;
            case '3':
              handleAddPINUser();
              break;
            case '4':
              handleAddFingerprintUser();
              break;
            case 'A':
              handleChangeAdminPIN();
              break;
            case 'D':
              handleDeleteUser();
              break;
            case 'B':
              lcd.clear();
              lcd.print(F("Listing Users"));
              lcd.setCursor(0, 1);
              lcd.print(F("Check Serial"));
              listUsers();
              delay(LONG_DELAY);
              break;
            case '*':
              exitAdminMenu = true;
              currentState = STATE_IDLE;
              lcd.clear();
              lcd.print(F("Exit Admin"));
              delay(MEDIUM_DELAY);
              showIdleScreen();
              break;
            case '#':
              adminMenuPage = (adminMenuPage + 1) % 3;
              showAdminMenu(adminMenuPage);
              break;
            default:
              // Phím không hợp lệ trong menu này
              lcd.clear();
              lcd.print(F("Invalid option"));
              delay(800);
              break;
          }
          
          // Nếu không thoát, làm mới menu
          if (!exitAdminMenu && key != '#') {
            showAdminMenu(adminMenuPage);
          }
        }
      }
    } else {
      // Reset lastKeyCode nếu không có phím nào được nhấn trong một khoảng thời gian
      if (currentTime - lastKeyPressTime > DEBOUNCE_DELAY * 2) {
        lastKeyCode = 0;
      }
    }
    
    // Xử lý lệnh từ xa trong khi ở menu admin
    processRemoteCommand();
    
    // Giảm tải CPU
    delay(25);
  }
}

void showAdminMenu(int menuPage) {
  lcd.clear();
  switch (menuPage) {
    case 0:
      lcd.print(F("ADMIN MENU 1/3"));
      lcd.setCursor(0, 1);
      lcd.print(F("2RFID 3PIN 4FP"));
      break;
    case 1:
      lcd.print(F("ADMIN MENU 2/3"));
      lcd.setCursor(0, 1);
      lcd.print(F("A:ChgAdmin D:Del"));
      break;
    case 2:
      lcd.print(F("ADMIN MENU 3/3"));
      lcd.setCursor(0, 1);
      lcd.print(F("B:List *:Exit"));
      break;
  }
}

/*********************** ADMIN FUNCTIONS **************************/
void handleAddRFIDUser() {
  lcd.clear();
  lcd.print(F("Scan new RFID"));
  lcd.setCursor(0, 1);
  lcd.print(F("card now..."));
  
  unsigned long startTime = millis();
  bool cardFound = false;
  String uidStr = "";
  
  // Wait for RFID card (10 second timeout)
  while (millis() - startTime < TIMEOUT_DURATION && !cardFound) {
    if (mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial()) {
      uidStr = getUIDFromRFIDTag();
      
      lcd.clear();
      lcd.print(F("RFID Found:"));
      lcd.setCursor(0, 1);
      lcd.print(uidStr);
      delay(MEDIUM_DELAY);
      
      // Check if card already exists
      if (!authenticateUser(USER_TYPE_RFID, uidStr.c_str())) {
        UserRecord newUser;
        newUser.active = 1;
        newUser.userType = USER_TYPE_RFID;
        newUser.userID = getUserCount() + 1;
        uidStr.toCharArray(newUser.credential, 16);
        
        if (addUserRecord(newUser)) {
          lcd.clear();
          lcd.print(F("RFID Added"));
          lcd.setCursor(0, 1);
          lcd.print(F("ID: "));
          lcd.print(newUser.userID);
          beepOk();
        } else {
          lcd.clear();
          lcd.print(F("EEPROM Full"));
          beepError();
        }
      } else {
        lcd.clear();
        lcd.print(F("RFID already"));
        lcd.setCursor(0, 1);
        lcd.print(F("exists!"));
        beepError();
      }
      
      cardFound = true;
      mfrc522.PICC_HaltA();
    }
    delay(50);
  }
  
  if (!cardFound) {
    lcd.clear();
    lcd.print(F("No card found"));
    lcd.setCursor(0, 1);
    lcd.print(F("Timeout!"));
    beepError();
  }
  
  delay(LONG_DELAY);
}

void handleAddPINUser() {
  const uint8_t MIN_PIN_LENGTH = 4;
  const uint8_t MAX_PIN_LENGTH = 16;
lastKeyCode = 0;
lastKeyPressTime = 0;
  
  // Biến theo dõi trạng thái
  bool inputComplete = false;
  bool confirmComplete = false;
  
  // Biến theo dõi thời gian chống dính phím

  
  // Get new PIN
  char newPIN[MAX_PIN_LENGTH + 1] = {0};
  char confirmPIN[MAX_PIN_LENGTH + 1] = {0};
  uint8_t pinLength = 0;
  
  // Hiển thị hướng dẫn ban đầu
  lcd.clear();
  lcd.print(F("Enter new PIN:"));
  lcd.setCursor(0, 1);
  lcd.print(F("(4-16 digits)"));
  delay(800); // Thời gian dài hơn để người dùng đọc hướng dẫn
  
  lcd.clear();
  lcd.print(F("Enter new PIN:"));
  lcd.setCursor(0, 1);
  
  // Vòng lặp nhập PIN mới
  while (!inputComplete) {
    uint8_t kCode = keypad.getKey();
    unsigned long currentTime = millis();
    
    if (kCode != 0 && kCode != 16) {
      // Kiểm tra chống dính phím
      if ((kCode != lastKeyCode) || (currentTime - lastKeyPressTime >= DEBOUNCE_DELAY)) {
        lastKeyCode = kCode;
        lastKeyPressTime = currentTime;
        
        char k = mapKeypadCodeToChar(kCode);
        
        // Xử lý phím Enter (#)
        if (k == '#') {
          if (pinLength >= MIN_PIN_LENGTH) {
            inputComplete = true;
            beepOk();
          } else {
            lcd.clear();
            lcd.print(F("PIN too short!"));
            lcd.setCursor(0, 1);
            lcd.print(F("Min: "));
            lcd.print(MIN_PIN_LENGTH);
            lcd.print(F(" digits"));
            beepError();
            delay(1200);
            
            // Quay lại trạng thái nhập
            lcd.clear();
            lcd.print(F("Enter new PIN:"));
            lcd.setCursor(0, 1);
            for (uint8_t i = 0; i < pinLength; i++) {
              lcd.print('*');
            }
          }
        }
        
        // Xử lý phím Backspace (*)
        else if (k == '*' && pinLength > 0) {
          pinLength--;
          newPIN[pinLength] = 0;
          
          // Xóa dòng và hiển thị lại
          lcd.setCursor(0, 1);
          lcd.print(F("                ")); // Xóa dòng hoàn toàn
          lcd.setCursor(0, 1);
          for (uint8_t i = 0; i < pinLength; i++) {
            lcd.print('*');
          }
          beepOk();
        }
        
        // Xử lý phím số
        else if (k >= '0' && k <= '9') {
          if (pinLength < MAX_PIN_LENGTH) {
            newPIN[pinLength] = k;
            pinLength++;
            newPIN[pinLength] = 0; // Đảm bảo kết thúc chuỗi
            
            lcd.setCursor(pinLength - 1, 1);
            lcd.print('*');
            beepOk();
          } else {
            lcd.clear();
            lcd.print(F("PIN too long!"));
            lcd.setCursor(0, 1);
            lcd.print(F("Max: "));
            lcd.print(MAX_PIN_LENGTH);
            lcd.print(F(" digits"));
            beepError();
            delay(1200);
            
            // Quay lại trạng thái nhập
            lcd.clear();
            lcd.print(F("Enter new PIN:"));
            lcd.setCursor(0, 1);
            for (uint8_t i = 0; i < pinLength; i++) {
              lcd.print('*');
            }
          }
        }
      }
    }
    
    // Reset lastKeyCode nếu không có phím nào được nhấn trong một khoảng thời gian
    if (kCode == 0 && (currentTime - lastKeyPressTime > DEBOUNCE_DELAY * 2)) {
      lastKeyCode = 0;
    }
    
    delay(10); // Làm giảm việc quét liên tục
  }
  
  // Reset biến chống dính phím
  lastKeyPressTime = 0;
  lastKeyCode = 0;
  
  // Xác nhận lại PIN
  lcd.clear();
  lcd.print(F("Confirm PIN:"));
  lcd.setCursor(0, 1);
  uint8_t confirmLength = 0;
  
  // Vòng lặp xác nhận PIN
  while (!confirmComplete) {
    uint8_t kCode = keypad.getKey();
    unsigned long currentTime = millis();
    
    if (kCode != 0 && kCode != 16) {
      // Kiểm tra chống dính phím
      if ((kCode != lastKeyCode) || (currentTime - lastKeyPressTime >= DEBOUNCE_DELAY)) {
        lastKeyCode = kCode;
        lastKeyPressTime = currentTime;
        
        char k = mapKeypadCodeToChar(kCode);
        
        // Xử lý phím Enter (#)
        if (k == '#') {
          confirmComplete = true;
          beepOk();
        }
        
        // Xử lý phím Backspace (*)
        else if (k == '*' && confirmLength > 0) {
          confirmLength--;
          confirmPIN[confirmLength] = 0;
          
          // Xóa dòng và hiển thị lại
          lcd.setCursor(0, 1);
          lcd.print(F("                ")); // Xóa dòng hoàn toàn
          lcd.setCursor(0, 1);
          for (uint8_t i = 0; i < confirmLength; i++) {
            lcd.print('*');
          }
          beepOk();
        }
        
        // Xử lý phím số
        else if (k >= '0' && k <= '9') {
          if (confirmLength < MAX_PIN_LENGTH) {
            confirmPIN[confirmLength] = k;
            confirmLength++;
            confirmPIN[confirmLength] = 0; // Đảm bảo kết thúc chuỗi
            
            lcd.setCursor(confirmLength - 1, 1);
            lcd.print('*');
            beepOk();
          } else {
            beepError();
          }
        }
      }
    }
    
    // Reset lastKeyCode nếu không có phím nào được nhấn trong một khoảng thời gian
    if (kCode == 0 && (currentTime - lastKeyPressTime > DEBOUNCE_DELAY * 2)) {
      lastKeyCode = 0;
    }
    
    delay(10);
  }
  
  // Kiểm tra và lưu PIN
  lcd.clear();
  lcd.print(F("Processing..."));
  
  if (strcmp(newPIN, confirmPIN) == 0) {
    // Kiểm tra xem PIN đã tồn tại chưa
    if (!authenticateUser(USER_TYPE_PIN, newPIN)) {
      // Kiểm tra không gian EEPROM trước khi tạo người dùng
      if (hasSpaceForNewUser()) {
        UserRecord newUser;
        newUser.active = 1;
        newUser.userType = USER_TYPE_PIN;
        newUser.userID = getUserCount() + 1;
        strncpy(newUser.credential, newPIN, MAX_PIN_LENGTH);
        newUser.credential[MAX_PIN_LENGTH] = 0; // Đảm bảo kết thúc chuỗi
        
        if (addUserRecord(newUser)) {
          lcd.clear();
          lcd.print(F("Success!"));
          lcd.setCursor(0, 1);
          lcd.print(F("User ID: "));
          lcd.print(newUser.userID);
          // Dùng mẫu âm thanh thành công đặc biệt (beep gấp đôi)
          beepOk();
          delay(100);
          beepOk();
        } else {
          lcd.clear();
          lcd.print(F("Error saving"));
          lcd.setCursor(0, 1);
          lcd.print(F("PIN user"));
          beepError();
        }
      } else {
        lcd.clear();
        lcd.print(F("Memory Full"));
        lcd.setCursor(0, 1);
        lcd.print(F("Delete users first"));
        beepError();
      }
    } else {
      lcd.clear();
      lcd.print(F("PIN already"));
      lcd.setCursor(0, 1);
      lcd.print(F("exists!"));
      beepError();
    }
  } else {
    lcd.clear();
    lcd.print(F("PINs don't match!"));
    beepError();
  }
  
  delay(LONG_DELAY);
}

// Add this helper function
bool hasSpaceForNewUser() {
  return (getUserCount() < MAX_USERS);
}

void handleAddFingerprintUser() {
  lcd.clear();
  lcd.print(F("Enter FP ID #:"));
  delay(200);  
  
  char idStr[8] = {0};
  uint8_t idLength = 0;
    lastKeyPressTime = 0;
  lastKeyCode = 0;
  // Get fingerprint ID number
  while (true) {
    uint8_t kCode = keypad.getKey();
    unsigned long currentTime = millis();
    
    // Process key only if it's different from last key or enough time has passed
    if (kCode != 0 && kCode != 16 && 
        (kCode != lastKeyCode || (currentTime - lastKeyPressTime) > DEBOUNCE_DELAY)) {
      
      // Update debounce tracking variables
      lastKeyCode = kCode;
      lastKeyPressTime = currentTime;
      
      char k = mapKeypadCodeToChar(kCode);
      
      if (k == '#') break;
      
      if (k >= '0' && k <= '9' && idLength < 7) {
        idStr[idLength] = k;
        idStr[idLength+1] = 0;
        idLength++;
        
        lcd.setCursor(0, 1);
        lcd.print(idStr);
      }
    }
    delay(10); // Reduced delay for better responsiveness
  }
  
  if (idLength > 0) {
    int id = atoi(idStr);
    if (id > 0) {
      // Enroll fingerprint process
      lcd.clear();
      lcd.print(F("Place finger"));
      lcd.setCursor(0, 1);
      lcd.print(F("on sensor..."));
      
      // Get first fingerprint image
      bool success = false;
      int p = -1;
      
      unsigned long fpStartTime = millis();
      while (millis() - fpStartTime < TIMEOUT_DURATION && p != FINGERPRINT_OK) {
        p = finger.getImage();
        if (p == FINGERPRINT_OK) {
          lcd.clear();
          lcd.print(F("Image taken"));
          success = true;
          break;
        }
        delay(100);
      }
      
      if (success) {
        // Convert fingerprint image
        p = finger.image2Tz(1);
        if (p == FINGERPRINT_OK) {
          lcd.clear();
          lcd.print(F("Remove finger"));
          delay(LONG_DELAY);
          
          p = 0;
          while (p != FINGERPRINT_NOFINGER) {
            p = finger.getImage();
            delay(50);
          }
          
          // Get second fingerprint image
          lcd.clear();
          lcd.print(F("Place same"));
          lcd.setCursor(0, 1);
          lcd.print(F("finger again"));
          
          success = false; // Reset success flag for second attempt
          p = -1;
          fpStartTime = millis();
          while (millis() - fpStartTime < TIMEOUT_DURATION && p != FINGERPRINT_OK) {
            p = finger.getImage();
            if (p == FINGERPRINT_OK) {
              lcd.clear();
              lcd.print(F("Image taken"));
              success = true;
              break;
            }
            delay(100);
          }
          
          if (success) {
            // Convert second image
            p = finger.image2Tz(2);
            if (p == FINGERPRINT_OK) {
              // Create model
              p = finger.createModel();
              if (p == FINGERPRINT_OK) {
                // Store model
                p = finger.storeModel(id);
                if (p == FINGERPRINT_OK) {
                  // Save to system
                  UserRecord newUser;
                  newUser.active = 1;
                  newUser.userType = USER_TYPE_FINGERPRINT;
                  newUser.userID = getUserCount() + 1;
                  sprintf(newUser.credential, "%d", id);
                  
                  if (addUserRecord(newUser)) {
                    lcd.clear();
                    lcd.print(F("Fingerprint"));
                    lcd.setCursor(0, 1);
                    lcd.print(F("Added as ID #"));
                    lcd.print(id);
                    beepOk();
                  } else {
                    lcd.clear();
                    lcd.print(F("EEPROM Full"));
                    beepError();
                  }
                } else {
                  lcd.clear();
                  lcd.print(F("Storage Err: "));
                  lcd.print(p);
                  beepError();
                }
              } else {
                lcd.clear();
                lcd.print(F("Prints not match"));
                beepError();
              }
            } else {
              lcd.clear();
              lcd.print(F("Process Error"));
              beepError();
            }
          } else {
            lcd.clear();
            lcd.print(F("Timeout"));
            beepError();
          }
        } else {
          lcd.clear();
          lcd.print(F("Process Error"));
          beepError();
        }
      } else {
        lcd.clear();
        lcd.print(F("No finger found"));
        lcd.setCursor(0, 1);
        lcd.print(F("Timeout!"));
        beepError();
      }
    } else {
      lcd.clear();
      lcd.print(F("Invalid ID"));
      beepError();
    }
  } else {
    lcd.clear();
    lcd.print(F("No ID entered"));
    beepError();
  }
  
  delay(LONG_DELAY);
}
void handleDeleteUser() {
  lcd.clear();
  lcd.print(F("Del User ID:"));
  lcd.setCursor(0, 1);
  
  // Biến cho cơ chế chống dính phím
lastKeyCode = 0;
lastKeyPressTime = 0;
  
  char idStr[8] = {0};
  uint8_t idLength = 0;
  
  // Vòng lặp nhập ID người dùng cần xóa
  while (true) {
    uint8_t kCode = keypad.getKey();
    unsigned long currentTime = millis();
    
    if (kCode != 0 && kCode != 16) {
      // Kiểm tra chống dính phím
      if ((kCode != lastKeyCode) || (currentTime - lastKeyPressTime >= DEBOUNCE_DELAY)) {
        char k = mapKeypadCodeToChar(kCode);
        
        // Cập nhật thời gian nhấn phím và mã phím
        lastKeyCode = kCode;
        lastKeyPressTime = currentTime;
        
        // Xử lý phím Enter (#)
        if (k == '#') {
          beepOk();
          break;
        }
        
        // Xử lý phím Backspace (*)
        else if (k == '*' && idLength > 0) {
          idLength--;
          idStr[idLength] = 0;
          
          // Xóa dòng và hiển thị lại
          lcd.setCursor(0, 1);
          lcd.print(F("       ")); // Xóa hiển thị ID
          lcd.setCursor(0, 1);
          lcd.print(idStr);
          beepOk();
        }
        
        // Xử lý phím số
        else if (k >= '0' && k <= '9' && idLength < 7) {
          idStr[idLength] = k;
          idLength++;
          idStr[idLength] = 0; // Đảm bảo kết thúc chuỗi
          
          lcd.setCursor(0, 1);
          lcd.print(idStr);
          beepOk();
        } else if (idLength >= 7) {
          // ID quá dài
          beepError();
        }
      }
    } else {
      // Reset lastKeyCode nếu không có phím nào được nhấn trong một khoảng thời gian
      if (currentTime - lastKeyPressTime > DEBOUNCE_DELAY * 2) {
        lastKeyCode = 0;
      }
    }
    
    delay(20); // Giảm tải CPU
  }
  
  // Xử lý xóa người dùng
  lcd.clear();
  lcd.print(F("Processing..."));
  
  if (idLength > 0) {
    uint16_t userID = atoi(idStr); // Sử dụng uint16_t để hỗ trợ ID lớn hơn
    
    if (removeUserRecord(userID)) {
      lcd.clear();
      lcd.print(F("User #"));
      lcd.print(userID);
      lcd.setCursor(0, 1);
      lcd.print(F("Removed"));
      beepOk();
      delay(100);
      beepOk(); // Âm thanh xác nhận kép cho hành động quan trọng
    } else {
      lcd.clear();
      lcd.print(F("User #"));
      lcd.print(userID);
      lcd.setCursor(0, 1);
      lcd.print(F("Not Found"));
      beepError();
    }
  } else {
    lcd.clear();
    lcd.print(F("No ID entered"));
    lcd.setCursor(0, 1);
    lcd.print(F("Operation canceled"));
    beepError();
  }
  
  delay(LONG_DELAY);
}
void handleChangeAdminPIN() {
  lcd.clear();
  lcd.print(F("New Admin PIN:"));
  
  char newAdminPIN[5] = {0};
  uint8_t pinLength = 0;
  
  // Variables for debouncing
lastKeyCode = 0;
  lastKeyPressTime  = 0;

  
  // Get new admin PIN
  while (true) {
    uint8_t kCode = keypad.getKey();
    unsigned long currentTime = millis();
    
    // Process key only if it's different from last key or enough time has passed
    if (kCode != 0 && (kCode != lastKeyCode || (currentTime -   lastKeyPressTime ) > DEBOUNCE_DELAY)) {
      lastKeyCode = kCode;
        lastKeyPressTime  = currentTime;
      
      // Skip key #16 (often used internally by keypad libraries)
      if (kCode == 16) continue;
      
      char k = mapKeypadCodeToChar(kCode);
      
      // Exit condition
      if (k == '#') break;
      
      // Process valid keys
      if ((k >= '0' && k <= '9') || (k >= 'A' && k <= 'D')) {
        if (pinLength < 4) {
          newAdminPIN[pinLength] = k;
          newAdminPIN[pinLength+1] = 0; // Keep string null-terminated
          pinLength++;
          
          // Update display
          lcd.setCursor(0, 1);
          lcd.print(newAdminPIN);
        }
      }
    }
    
    // Small delay for system stability
    delay(10);
  }
  
  // Process the entered PIN
  if (strlen(newAdminPIN) == 4) {
    storeAdminPIN(newAdminPIN);
    lcd.clear();
    lcd.print(F("Admin updated"));
    beepOk();
  } else {
    lcd.clear();
    lcd.print(F("Invalid PIN"));
    beepError();
  }
  
  delay(LONG_DELAY);
}
/*********************** REMOTE COMMAND PROCESSING **************************/
void processRemoteCommand() {
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    
    // Hiển thị lệnh nhận được trên LCD để debug
    lcd.clear();
    lcd.print(F("CMD: "));
    lcd.print(cmd.substring(0, 10));
    
    if (cmd.equals("PING")) {
      Serial.println(F("OK"));
    }
    // Lệnh mở cửa
    else if (cmd.equals("DOOR:OPEN")) {
      lcd.clear();
      lcd.print(F("Web Request:"));
      lcd.setCursor(0, 1);
      lcd.print(F("Opening Door"));
      
      // Đảm bảo gửi phản hồi trước khi thực hiện hành động
      Serial.println(F("OK:PROCESSING"));
      delay(100); // Đảm bảo ESP32 nhận được phản hồi
      
      openDoor("ESP32_Web");
      
      // Gửi thêm một xác nhận
      Serial.println(F("STATUS:OPEN"));
    }
    
    
    // Thông tin hệ thống
    else if (cmd.equals("SYSTEM_INFO")) {
      Serial.print(F("INFO:USERS="));
      Serial.print(getUserCount());
      Serial.print(F(",FIRMWARE="));
      Serial.print(F("1.0.0"));
      Serial.print(F(",DOOR="));
      Serial.print(doorOpen ? F("OPEN") : F("CLOSED"));
      Serial.print(F(",SERVO="));
      Serial.println(F("READY"));
    }
    
    // Lệnh mở cửa
    else if (cmd.equals("DOOR:OPEN")) {
      // Chỉ mở cửa nếu cửa đang đóng
      if (!doorOpen) {
        openDoor("ESP32");
        Serial.println(F("OK:DOOR_OPENED"));
      } else {
        // Nếu cửa đã mở, cập nhật thời gian để kéo dài
        doorOpenTime = millis();
        Serial.println(F("OK:ALREADY_OPEN"));
      }
    }
    
    // Lệnh đóng cửa
    else if (cmd.equals("DOOR:CLOSE")) {
      if (doorOpen) {
        lockDoor();
        Serial.println(F("OK:DOOR_CLOSED"));
      } else {
        Serial.println(F("OK:ALREADY_CLOSED"));
      }
    }
    
    // Lệnh đóng mở cửa khẩn cấp
    else if (cmd.equals("UNLOCK_NOW") || cmd.equals("EMERGENCY_OPEN")) {
      openDoor("Emergency");
      Serial.println(F("OK:EMERGENCY_UNLOCKED"));
    }
    
    // Liệt kê người dùng
    else if (cmd.equals("LIST_USERS")) {
      listUsers();
      Serial.println(F("OK:USER_LIST_COMPLETE"));
    }
    
    // Lệnh thiết lập lại thời gian
    else if (cmd.startsWith("SET_TIME,")) {
      Serial.println(F("OK:TIME_SET"));
    }
    
    // Thêm người dùng RFID
    else if (cmd.startsWith("ADD_RFID,")) {
      int idx = cmd.indexOf(',');
      String uid = cmd.substring(idx + 1);
      
      UserRecord newUser;
      newUser.active = 1;
      newUser.userType = USER_TYPE_RFID;
      newUser.userID = getUserCount() + 1;
      uid.toCharArray(newUser.credential, 16);
      
      if (addUserRecord(newUser))
        Serial.println(F("OK:RFID_ADDED"));
      else
        Serial.println(F("ERR:EEPROM_FULL"));
    }
    
    // Thêm người dùng PIN
    else if (cmd.startsWith("ADD_PIN,")) {
      int idx = cmd.indexOf(',');
      String pin = cmd.substring(idx + 1);
      
      UserRecord newUser;
      newUser.active = 1;
      newUser.userType = USER_TYPE_PIN;
      newUser.userID = getUserCount() + 1;
      pin.toCharArray(newUser.credential, 16);
      
      if (addUserRecord(newUser))
        Serial.println(F("OK:PIN_ADDED"));
      else
        Serial.println(F("ERR:EEPROM_FULL"));
    }
    
    // Thêm người dùng vân tay
    else if (cmd.startsWith("ADD_FP,")) {
      int idx = cmd.indexOf(',');
      String fpIDstr = cmd.substring(idx + 1);
      
      UserRecord newUser;
      newUser.active = 1;
      newUser.userType = USER_TYPE_FINGERPRINT;
      newUser.userID = getUserCount() + 1;
      fpIDstr.toCharArray(newUser.credential, 16);
      
      if (addUserRecord(newUser))
        Serial.println(F("OK:FINGERPRINT_ADDED"));
      else
        Serial.println(F("ERR:EEPROM_FULL"));
    }
    
    // Xóa người dùng
    else if (cmd.startsWith("REMOVE_USER,")) {
      int idx = cmd.indexOf(',');
      String idStr = cmd.substring(idx + 1);
      uint8_t userID = idStr.toInt();
      
      if (removeUserRecord(userID))
        Serial.println(F("OK:USER_REMOVED"));
      else
        Serial.println(F("ERR:USER_NOT_FOUND"));
    }
    
    // Kiểm tra Servo
    else if (cmd.equals("TEST_SERVO")) {
      // Kiểm tra servo trong khi vẫn duy trì trạng thái hiện tại
      bool wasDoorOpen = doorOpen;
      
      // Chạy kiểm tra servo
      Serial.println(F("START:SERVO_TEST"));
      doorServo.attach(PIN_SERVO);
      delay(100);
      
      // Di chuyển từ từ tới vị trí khóa
      doorServo.write(0);
      delay(1000);
      
      // Di chuyển từ từ tới vị trí mở
      for (int pos = 0; pos <= 110; pos += 10) {
        doorServo.write(pos);
        delay(100);
      }
      
      // Đảm bảo vị trí mở đầy đủ
      doorServo.write(110);
      delay(1000);
      
      // Di chuyển về vị trí khóa
      for (int pos = 110; pos >= 0; pos -= 10) {
        doorServo.write(pos);
        delay(100);
      }
      
      doorServo.write(0);
      delay(500);
      
      // Khôi phục trạng thái trước khi kiểm tra
      if (wasDoorOpen) {
        doorServo.write(110);
        doorOpen = true;
      } else {
        doorServo.write(0);
        doorServo.detach();
        doorOpen = false;
      }
      
      Serial.println(F("END:SERVO_TEST"));
      Serial.println(F("OK:SERVO_TESTED"));
    }
    
    // Thay đổi mã PIN quản trị
    else if (cmd.startsWith("SET_ADMIN,")) {
      int idx = cmd.indexOf(',');
      String newPIN = cmd.substring(idx+1);
      
      if (newPIN.length() == 4) {
        storeAdminPIN(newPIN.c_str());
        Serial.println(F("OK:ADMIN_PIN_UPDATED"));
      } else {
        Serial.println(F("ERR:INVALID_PIN_LENGTH"));
      }
    }
    
    // Lệnh không xác định
    else {
      Serial.print(F("ERR:UNKNOWN_COMMAND-"));
      Serial.println(cmd);
    }
    
    // Khôi phục màn hình nếu cần
    if (currentState == STATE_IDLE) {
      showIdleScreen();
    }
  }
}


/*********************** EEPROM FUNCTIONS **************************/
int getUserCount() {
  return EEPROM.read(EEPROM_COUNT_ADDR);
}

void setUserCount(uint8_t count) {
  EEPROM.write(EEPROM_COUNT_ADDR, count);
}

bool addUserRecord(UserRecord user) {
  int count = getUserCount();
  if (count >= MAX_USERS) return false;
  
  int addr = EEPROM_COUNT_ADDR + 1 + count * sizeof(UserRecord);
  user.active = 1; // Mark user as valid
  EEPROM.put(addr, user);
  
  count++;
  setUserCount(count);
  return true;
}

bool updateUserRecord(int index, UserRecord &user) {
  if (index < 0 || index >= getUserCount()) return false;
  
  int addr = EEPROM_COUNT_ADDR + 1 + index * sizeof(UserRecord);
  EEPROM.put(addr, user);
  return true;
}

bool removeUserRecord(uint8_t userID) {
  int count = getUserCount();
  UserRecord u;
  
  for (int i = 0; i < count; i++) {
    int addr = EEPROM_COUNT_ADDR + 1 + i * sizeof(UserRecord);
    EEPROM.get(addr, u);
    
    if (u.active == 1 && u.userID == userID) {
      u.active = 0; // Mark user as invalid
      EEPROM.put(addr, u);
      return true;
    }
  }
  return false;
}

void listUsers() {
  int count = getUserCount();
  Serial.println(F("User List:"));
  
  UserRecord u;
  for (int i = 0; i < count; i++) {
    int addr = EEPROM_COUNT_ADDR + 1 + i * sizeof(UserRecord);
    EEPROM.get(addr, u);
    
    if (u.active == 1) {
      Serial.print(F("ID: "));
      Serial.print(u.userID);
      Serial.print(F(" Type: "));
      
      if (u.userType == USER_TYPE_FINGERPRINT)
        Serial.print(F("Fingerprint"));
      else if (u.userType == USER_TYPE_RFID)
        Serial.print(F("RFID"));
      else if (u.userType == USER_TYPE_PIN)
        Serial.print(F("PIN"));
      
      Serial.print(F(" Credential: "));
      Serial.println(u.credential);
    }
  }
}

void loadAdminPIN() {
  // Check if EEPROM has admin PIN (0xFF indicates uninitialized)
  if (EEPROM.read(ADMIN_PIN_EEPROM_ADDR) == 0xFF) {
    // Save default PIN
    for (int i = 0; i < 4; i++) {
      EEPROM.write(ADMIN_PIN_EEPROM_ADDR + i, adminPIN[i]);
    }
  } else {
    // Load PIN from EEPROM
    for (int i = 0; i < 4; i++) {
      adminPIN[i] = EEPROM.read(ADMIN_PIN_EEPROM_ADDR + i);
    }
    adminPIN[4] = '\0';
  }
}

void storeAdminPIN(const char* newPIN) {
  strncpy(adminPIN, newPIN, 4);
  adminPIN[4] = '\0';
  
  for (int i = 0; i < 4; i++) {
    EEPROM.write(ADMIN_PIN_EEPROM_ADDR + i, adminPIN[i]);
  }
}

/*********************** AUTHENTICATION **************************/
bool authenticateUser(uint8_t authType, const char* credential) {
  int count = getUserCount();
  UserRecord u;
  
  for (int i = 0; i < count; i++) {
    int addr = EEPROM_COUNT_ADDR + 1 + i * sizeof(UserRecord);
    EEPROM.get(addr, u);
    
    if (u.active == 1 && u.userType == authType) {
      if (strcmp(u.credential, credential) == 0) {
        return true;
      }
    }
  }
  return false;
}

/*********************** UTILITY FUNCTIONS **************************/
// Map keypad code to character
char mapKeypadCodeToChar(uint8_t keyCode) {
  if (keyCode == 0 || keyCode > 16) {
    return 0;
  }
  
  const char keyMap[18] = {
    '2', '3', 'A',
    '4', '5', '6', 'B',
    '7', '8', '9', 'C',
    '*', '0', '#', 'D'
  };
  
  return keyMap[keyCode - 1];
}
