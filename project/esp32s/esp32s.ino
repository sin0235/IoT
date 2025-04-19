/**
 * He Thong Quan Ly Cua Thong Minh - ESP32S
 * Ket noi voi Arduino Uno qua giao tiep UART va module chuyen doi muc logic
 */

#include <WiFi.h>
#include <WebServer.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <ESPmDNS.h>
bool adminCardMode = false; 

#include <SPI.h>
#include <MFRC522.h>

// Định nghĩa chân cho RFID
#define RFID_RST_PIN     22    // RC522 RST pin
#define RFID_SS_PIN      5     // RC522 SS/SDA pin
MFRC522 mfrc522(RFID_SS_PIN, RFID_RST_PIN);
// ===== CAU HINH WIFI =====
const char* Ssid = "Wi-MESH 2.4G";     
const char* Password = "25032005"; 

bool APMode = false;
const char* AP_SSID = "SmartDoor";
const char* AP_PASSWORD = "12345678";


// ===== CAU HINH UART =====
#define ARDUINO_BAUD_RATE 9600
#define COMMAND_TIMEOUT 3000  // 3 giay timeout cho lenh Arduino

// ===== BIEN TOAN CUC =====
WebServer server(80);
Preferences preferences;
bool doorIsOpen = false;
String lastAccessUser = "";
unsigned long lastAccessTime = 0;

// Cau truc du lieu nguoi dung
struct User {
  String username;
  String password;
  bool isAdmin;
  bool active;
};

// Mang luu tru nguoi dung (toi da 20 nguoi dung)
User users[20];
int userCount = 0;

// Buffer giao tiep Serial
String serialBuffer = "";
bool responseReceived = false;
String lastResponse = "";

// Function declarations
String sendCommandToArduino(String command, long unsigned int timeout = COMMAND_TIMEOUT);
void checkArduinoResponse();
bool checkArduinoConnection();
void loadUsers();
void saveUsers();
bool authenticateUser(String username, String password);
bool isAdmin(String username);
bool addUser(String username, String password, bool isAdmin);
bool deleteUser(String username);
bool connectToWiFi(String ssid, String password, int timeout = 20);
void setupServerRoutes();
void setupWiFiConfigRoutes();
void setupAP();
void saveWiFiConfig(String ssid, String password);
bool loadWiFiConfig();
void clearWiFiConfig();
void handleRoot();
void handleLogin();
void handleAdminPanel();
void handleUserDashboard();
void handleDoorStatus();
void handleOpenDoor();
void handleGetUsers();
void handleAddUser();
void handleDeleteUser();
void handleGetLogs();
void handleWiFiSetup();
void handleWiFiScan();
void handleSaveWiFi();
void handleResetWiFi();
void restart();

// ===== KHOI TAO HE THONG =====
void setup() {
  // Khởi tạo cổng Serial để debug
  Serial.begin(115200);
  Serial.println("\n\n=== Hệ Thống Quản Lý Cửa Thông Minh ===");
  
  // Khởi tạo cổng Serial2 để giao tiếp với Arduino
  Serial2.begin(ARDUINO_BAUD_RATE, SERIAL_8N1, 16, 17);
  Serial.println("Đã khởi tạo kết nối với Arduino qua Serial2");
  SPI.begin();
  mfrc522.PCD_Init();
  delay(50);
  
  // Kiểm tra RFID reader
  byte v = mfrc522.PCD_ReadRegister(mfrc522.VersionReg);
  if (v == 0x00 || v == 0xFF) {
    Serial.println("CẢNH BÁO: Không thể kết nối với đầu đọc RFID!");
  } else {
    Serial.print("Đầu đọc RFID đã kết nối. Phiên bản: 0x");
    Serial.println(v, HEX);
  }
  
  // Khởi tạo SPIFFS
  if(!SPIFFS.begin(true)) {
    Serial.println("Lỗi khởi tạo SPIFFS");
  } else {
    Serial.println("SPIFFS đã khởi tạo thành công");
  }
  
  // Tải danh sách người dùng
  loadUsers();
  Serial.println("Đã tải danh sách người dùng");
  
  // Thử kết nối WiFi từ cấu hình đã lưu
  if (!loadWiFiConfig()) {
    Serial.println("Không có cấu hình WiFi hoặc không thể kết nối, chuyển sang chế độ AP");
    setupAP();
  }
  
  // Thiết lập các routes cho web server
  setupServerRoutes();
  setupWiFiConfigRoutes(); // Thêm routes cho cấu hình WiFi
  Serial.println("Đã thiết lập các routes cho web server");
  
  // Khởi động server
  server.begin();
  Serial.println("Máy chủ HTTP đã khởi động");
  
  
  // Làm sạch buffer Serial2 trước khi kiểm tra kết nối
  while(Serial2.available()) {
    Serial2.read();
  }
  
  // Kiểm tra kết nối với Arduino
  Serial.println("Dang kiem tra ket noi voi Arduino...");
  
  // Đợi Arduino khởi động xong (nếu mới khởi động lại)
  delay(2000);
  
  // Gửi lệnh PING kiểm tra kết nối
  bool arduinoConnected = false;
  int maxAttempts = 3;
  
  for(int attempt = 1; attempt <= maxAttempts; attempt++) {
    Serial.print("Lan thu ");
    Serial.print(attempt);
    Serial.println(" gui lenh PING...");
    
    String response = sendCommandToArduino("PING", 2000);
    
    if(response == "OK") {
      Serial.println("Ket noi voi Arduino thanh cong!");
      arduinoConnected = true;
      break;
    } else if(response == "TIMEOUT") {
      Serial.println("Khong nhan duoc phan hoi, thu lai...");
    } else {
      Serial.print("Phan hoi khong mong muon: ");
      Serial.println(response);
    }
    
    delay(1000);
  }
  
  if(!arduinoConnected) {
    Serial.println("CANH BAO: Khong the ket noi voi Arduino sau nhieu lan thu!");
    Serial.println("He thong se tiep tuc hoat dong, nhung cac chuc nang lien quan den Arduino co the khong khả dụng");
  }
  
  // Kiểm tra trạng thái cửa ban đầu từ Arduino
  if(arduinoConnected) {
    String doorStatus = sendCommandToArduino("STATUS");
    if(doorStatus.startsWith("STATUS:")) {
      doorIsOpen = (doorStatus.substring(7) == "OPEN");
      Serial.print("Trang thai cua ban dau: ");
      Serial.println(doorIsOpen ? "MO" : "DONG");
    }
  }
  
  // Yêu cầu thông tin hệ thống từ Arduino
  if(arduinoConnected) {
    String systemInfo = sendCommandToArduino("SYSTEM_INFO");
    if(systemInfo.startsWith("INFO:")) {
      Serial.print("Thong tin he thong Arduino: ");
      Serial.println(systemInfo.substring(5));
    }
  }
  
  // Yêu cầu lấy danh sách người dùng từ Arduino
  if(arduinoConnected) {
    String cmdResponse = sendCommandToArduino("LIST_USERS");
    if(cmdResponse.startsWith("OK:")) {
      Serial.println("Da yeu cau danh sach nguoi dung tu Arduino");
    }
  }
  
  // Hiển thị thông tin trạng thái hệ thống
  Serial.println("He thong Quan ly Cua Thong Minh da san sang!");
  if (APMode) {
    Serial.println("Chế độ AP - SSID: " + String(AP_SSID) + ", Password: " + String(AP_PASSWORD));
    Serial.println("IP AP: " + WiFi.softAPIP().toString());
  } else {
    Serial.println("Chế độ STA - Đã kết nối WiFi: " + WiFi.SSID());
    Serial.println("IP: " + WiFi.localIP().toString());
  }
  Serial.println("Su dung trinh duyet de truy cap theo dia chi IP tren");
}

// ===== VONG LAP CHINH =====
void loop() {
  // Xử lý các yêu cầu HTTP
  server.handleClient();
  
  // Đọc dữ liệu từ Arduino nếu có
  if(Serial2.available()) {
    checkArduinoResponse();
  }
  static unsigned long lastRfidCheckTime = 0;
    if (millis() - lastRfidCheckTime > 100) { // Kiểm tra mỗi 100ms
      processRFID();
      lastRfidCheckTime = millis();
    }
    
  static unsigned long lastStatusCheck = 0;
  if (millis() - lastStatusCheck > 3000) { // Every 3 seconds
    String status = sendCommandToArduino("STATUS", 1000);
    if (status.startsWith("STATUS:")) {
      bool newDoorState = (status.substring(7) == "OPEN");
      if (doorIsOpen != newDoorState) {
        Serial.println("Correcting door state mismatch");
        doorIsOpen = newDoorState;
      }
    }
    lastStatusCheck = millis();
  }

  
  // Kiểm tra các cập nhật trạng thái từ Arduino
  if(serialBuffer.indexOf("STATUS:") >= 0) {
    int startPos = serialBuffer.indexOf("STATUS:") + 7;
    int endPos = serialBuffer.indexOf("\n", startPos);
    
    if(endPos > startPos) {
      String status = serialBuffer.substring(startPos, endPos);
      serialBuffer = serialBuffer.substring(endPos + 1);
      
      // Cập nhật trạng thái cửa
      doorIsOpen = (status == "OPEN");
      Serial.println("Cap nhat trang thai cua: " + status);
    }
  }
  
  // Xử lý các log từ Arduino
  if(serialBuffer.indexOf("LOG:") >= 0) {
    int startPos = serialBuffer.indexOf("LOG:") + 4;
    int endPos = serialBuffer.indexOf("\n", startPos);
    
    if(endPos > startPos) {
      String logMessage = serialBuffer.substring(startPos, endPos);
      serialBuffer = serialBuffer.substring(endPos + 1);
      
      // Ghi lại log từ Arduino
      Serial.println("Log tu Arduino: " + logMessage);
    }
  }
  
  // Xử lý các thông báo sự kiện từ Arduino
  if(serialBuffer.indexOf("EVENT:") >= 0) {
    int startPos = serialBuffer.indexOf("EVENT:") + 6;
    int endPos = serialBuffer.indexOf("\n", startPos);
    
    if(endPos > startPos) {
      String eventData = serialBuffer.substring(startPos, endPos);
      serialBuffer = serialBuffer.substring(endPos + 1);
      
      // Xử lý sự kiện
      handleArduinoEvent(eventData);
    }
  }
  
  // Kiểm tra kết nối Arduino định kỳ
  static unsigned long lastPingTime = 0;
  if (millis() - lastPingTime > 30000) { // Ping mỗi 30 giây
    String response = sendCommandToArduino("PING", 1000);
    if (response != "OK") {
      Serial.println("Canh bao: Mat ket noi voi Arduino!");
    }
    lastPingTime = millis();
  }
  
  // Kiểm tra kết nối WiFi định kỳ (nếu trong chế độ STA)
  static unsigned long lastWiFiCheckTime = 0;
  if (!APMode && millis() - lastWiFiCheckTime > 60000) { // Kiểm tra mỗi phút
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("Phát hiện mất kết nối WiFi, đang thử kết nối lại...");
      WiFi.reconnect();
    }
    lastWiFiCheckTime = millis();
  }
  static unsigned long resetButtonPressTime = 0;
  if (digitalRead(0) == LOW) {
    if (resetButtonPressTime == 0) {
      resetButtonPressTime = millis();
    } else if (millis() - resetButtonPressTime > 5000) {
      Serial.println("Nút reset được nhấn giữ 5 giây, xóa cấu hình WiFi và khởi động lại...");
      clearWiFiConfig();
      ESP.restart();
    }
  } else {
    resetButtonPressTime = 0;
  }
  // Giảm tải CPU
  delay(10);
}

// Hàm khởi động lại ESP32
void restart() {
  Serial.println("Khởi động lại ESP32...");
  delay(1000);
  ESP.restart();
}

// Hàm xử lý sự kiện từ Arduino
void handleArduinoEvent(String eventData) {
  Serial.println("Su kien tu Arduino: " + eventData);
  
  if(eventData.startsWith("ACCESS_GRANTED")) {
    // Lưu thông tin truy cập
    int commaPos = eventData.indexOf(",");
    if(commaPos > 0) {
      String method = eventData.substring(commaPos + 1);
      lastAccessUser = method;
      lastAccessTime = millis();
      
      // Lưu thông tin vào logs
      saveAccessLog(method);
    }
  } else if(eventData.startsWith("DOOR_LOCKED")) {
    doorIsOpen = false;
    Serial.println("Cua da duoc dong");
  }
}

// Lưu log truy cập
void saveAccessLog(String method) {
  preferences.begin("doorlogs", false);
  
  // Get current count with overflow handling
  int logCount = preferences.getInt("logCount", 0);
  if (logCount >= 50) logCount = 0;
  
  // Create timestamp with explicit detail
  unsigned long timestamp = millis();
  String timeStr = String(timestamp);
  
  // Create detailed log
  String logKey = "log" + String(logCount);
  String logValue = timeStr + "|" + method + "|" + "Door opened";
  
  // Debug log saving
  Serial.print("Saving log #");
  Serial.print(logCount);
  Serial.print(": ");
  Serial.println(logValue);
  
  // Store log with sync
  preferences.putString(logKey.c_str(), logValue.c_str());
  preferences.putInt("logCount", logCount + 1);
  preferences.end();
  
  Serial.println("Log saved successfully");
}


// ===== KET NOI WIFI =====
bool connectToWiFi(String ssid, String password, int timeout) {
  // Thoát khỏi chế độ AP nếu đang bật
  if(APMode) {
    WiFi.softAPdisconnect(true);
    delay(500);
  }
  
  // Thiết lập chế độ Station
  WiFi.mode(WIFI_STA);
  APMode = false;
  
  Serial.print("Đang kết nối WiFi với SSID: ");
  Serial.println(ssid);
  
  // Bắt đầu kết nối với mạng WiFi đã cấu hình
  WiFi.begin(ssid.c_str(), password.c_str());
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < timeout) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if(WiFi.status() == WL_CONNECTED) {
    Serial.println("\nĐã kết nối WiFi");
    Serial.println("Địa chỉ IP: " + WiFi.localIP().toString());
    
    // Thiết lập MDNS trong chế độ STA
    if(MDNS.begin("smartdoor")) {
      Serial.println("mDNS đã khởi động - Truy cập qua http://smartdoor.local");
    }
    
    return true;
  } else {
    Serial.println("\nKhông thể kết nối WiFi");
    return false;
  }
}

// ===== GIAO TIEP VOI ARDUINO =====
bool checkArduinoConnection() {
  return sendCommandToArduino("PING", 1000).startsWith("OK");
}

// Gui lenh den Arduino va doi phan h
String sendCommandToArduino(String command, long unsigned int timeout ) {
  // Reset biến trạng thái
  lastResponse = "";
  responseReceived = false;
  
  // Xóa bộ đệm trước khi gửi lệnh
  while (Serial2.available()) {
    Serial2.read();
  }
  
  // Đảm bảo Serial2 được khởi tạo
  if (!Serial2) {
    Serial.println("ERROR: Serial2 not initialized!");
    return "ERROR";
  }
  
  // Gửi lệnh với log rõ ràng
  Serial.println("Sending to Arduino: " + command);
  Serial2.println(command);
  Serial2.flush(); // Đảm bảo lệnh được gửi hoàn toàn
  
  // Đợi với xử lý timeout thích hợp
  unsigned long startTime = millis();
  while (!responseReceived && (millis() - startTime < timeout)) {
    if (Serial2.available()) {
      checkArduinoResponse();
    }
    delay(5);
  }
  
  if (responseReceived) {
    Serial.println("Received response: " + lastResponse);
    return lastResponse;
  } else {
    Serial.println("Timeout when sending command: " + command);
    return "TIMEOUT";
  }
}


// Kiem tra phan hoi tu Arduino
void checkArduinoResponse() {
  while(Serial2.available()) {
    char c = Serial2.read();
    
    // Debug xem ký tự nhận được
    Serial.print("Byte received: ");
    Serial.print((int)c);
    Serial.print(" (");
    Serial.print(c);
    Serial.println(")");
    
    serialBuffer += c;
    
    // Kiểm tra xem có dòng hoàn chỉnh không
    if(c == '\n' || c == '\r') {
serialBuffer.trim(); // Loại bỏ khoảng trắng
String line = serialBuffer; // Gán giá trị đã được xử lý/ Loại bỏ whitespace
      
      if(line.length() > 0) {
        lastResponse = line;
        responseReceived = true;
        Serial.println("Processed line from Arduino: [" + line + "]");
        
        // Xử lý các lệnh đặc biệt
        if(line.equals("OK")) {
          Serial.println("Received OK confirmation");
        } else if(line.startsWith("STATUS:")) {
          Serial.println("Received status update: " + line);
        }
        
        // Reset buffer sau khi xử lý dòng
        serialBuffer = "";
      }
    }
  }
}

// ===== QUAN LY NGUOI DUNG =====
void loadUsers() {
  preferences.begin("smartdoor", false);
  userCount = preferences.getInt("userCount", 0);
  
  // Neu khong co nguoi dung, tao tai khoan admin mac dinh
  if(userCount == 0) {
    users[0].username = "admin";
    users[0].password = "admin123";
    users[0].isAdmin = true;
    users[0].active = true;
    userCount = 1;
    saveUsers();
    Serial.println("Da tao tai khoan admin mac dinh");
  } else {
    // Doc du lieu nguoi dung
    for(int i = 0; i < userCount; i++) {
      String key = "user" + String(i);
      String userData = preferences.getString(key.c_str(), "");
      
      if(userData.length() > 0) {
        // Dinh dang: username|password|isAdmin|active
        int delimPos1 = userData.indexOf('|');
        int delimPos2 = userData.indexOf('|', delimPos1 + 1);
        int delimPos3 = userData.indexOf('|', delimPos2 + 1);
        
        if(delimPos1 > 0 && delimPos2 > delimPos1 && delimPos3 > delimPos2) {
          users[i].username = userData.substring(0, delimPos1);
          users[i].password = userData.substring(delimPos1 + 1, delimPos2);
          users[i].isAdmin = (userData.substring(delimPos2 + 1, delimPos3) == "1");
          users[i].active = (userData.substring(delimPos3 + 1) == "1");
        }
      }
    }
    Serial.println("Da tai " + String(userCount) + " nguoi dung");
  }
  
  preferences.end();
}

void saveUsers() {
  preferences.begin("smartdoor", false);
  preferences.putInt("userCount", userCount);
  
  for(int i = 0; i < userCount; i++) {
    String key = "user" + String(i);
    String value = users[i].username + "|" + 
                  users[i].password + "|" + 
                  (users[i].isAdmin ? "1" : "0") + "|" + 
                  (users[i].active ? "1" : "0");
    preferences.putString(key.c_str(), value.c_str());
  }
  
  preferences.end();
  Serial.println("Da luu " + String(userCount) + " nguoi dung");
}

bool authenticateUser(String username, String password) {
  for(int i = 0; i < userCount; i++) {
    if(users[i].username == username && users[i].password == password && users[i].active) {
      return true;
    }
  }
  return false;
}

bool isAdmin(String username) {
  for(int i = 0; i < userCount; i++) {
    if(users[i].username == username && users[i].isAdmin && users[i].active) {
      return true;
    }
  }
  return false;
}

bool addUser(String username, String password, bool isAdmin) {
  // Kiem tra nguoi dung da ton tai
  for(int i = 0; i < userCount; i++) {
    if(users[i].username == username) {
      return false;
    }
  }
  
  // Kiem tra so luong nguoi dung toi da
  if(userCount >= 20) {
    return false;
  }
  
  // Them nguoi dung moi
  users[userCount].username = username;
  users[userCount].password = password;
  users[userCount].isAdmin = isAdmin;
  users[userCount].active = true;
  userCount++;
  
  // Luu du lieu
  saveUsers();
  return true;
}

bool deleteUser(String username) {
  // Kiem tra xoa admin cuoi cung
  if(isAdmin(username)) {
    int adminCount = 0;
    for(int i = 0; i < userCount; i++) {
      if(users[i].isAdmin && users[i].active) {
        adminCount++;
      }
    }
    if(adminCount <= 1) {
      return false; // Khong the xoa admin cuoi cung
    }
  }
  
  // Tim va xoa nguoi dung
  for(int i = 0; i < userCount; i++) {
    if(users[i].username == username) {
      // Di chuyen tat ca nguoi dung tiep theo len mot vi tri
      for(int j = i; j < userCount - 1; j++) {
        users[j] = users[j + 1];
      }
      userCount--;
      saveUsers();
      return true;
    }
  }
  
  return false;
}

// ===== THIET LAP SERVER ROUTES =====
void setupServerRoutes() {
  // Trang chu va dang nhap
  server.on("/", HTTP_GET, handleRoot);
  server.on("/login", HTTP_POST, handleLogin);
  server.on("/api/arduino-status", HTTP_GET, handleArduinoStatus);

  // Trang dieu khien
  server.on("/admin", HTTP_GET, handleAdminPanel);
  server.on("/dashboard", HTTP_GET, handleUserDashboard);
  
  // API endpoints
  server.on("/api/door-status", HTTP_GET, handleDoorStatus);
  server.on("/api/open-door", HTTP_POST, handleOpenDoor);
  server.on("/api/users", HTTP_GET, handleGetUsers);
  server.on("/api/add-user", HTTP_POST, handleAddUser);
  server.on("/api/delete-user", HTTP_POST, handleDeleteUser);
  server.on("/api/logs", HTTP_GET, handleGetLogs);
  
  // 404 Not Found
  server.onNotFound([]() {
    server.send(404, "text/plain", "404: Khong tim thay trang");
  });
}

// Thiết lập các routes cho cấu hình WiFi
void setupWiFiConfigRoutes() {
  // Trang cấu hình WiFi
  server.on("/wifi-setup", HTTP_GET, handleWiFiSetup);
  
  // Quét mạng WiFi
  server.on("/api/scan-wifi", HTTP_GET, handleWiFiScan);
  
  // Lưu cấu hình WiFi mới
  server.on("/api/save-wifi", HTTP_POST, handleSaveWiFi);
  
  // Reset cấu hình WiFi
  server.on("/api/reset-wifi", HTTP_POST, handleResetWiFi);
}

// ===== CHỨC NĂNG WIFI AP =====
void setupAP() {
  APMode = true;
  
  // Ngắt kết nối WiFi hiện tại một cách triệt để
  WiFi.disconnect(true);
  delay(100);
  
  // Thiết lập chế độ AP với reset hoàn toàn
  WiFi.mode(WIFI_OFF);
  delay(100);
  WiFi.mode(WIFI_AP);
  delay(100);
  
  // Cấu hình IP tĩnh cho AP để tránh xung đột
  IPAddress local_IP(192,168,4,1);
  IPAddress gateway(192,168,4,1);
  IPAddress subnet(255,255,255,0);
  
  // Thiết lập cấu hình IP
  if(!WiFi.softAPConfig(local_IP, gateway, subnet)) {
    Serial.println("Cấu hình AP thất bại!");
  }
  
  // Khởi tạo AP với các tham số chi tiết
  // softAP(ssid, password, channel, hidden, max_connections)
  if(WiFi.softAP(AP_SSID, AP_PASSWORD, 1, 0, 4)) {
    Serial.println("WiFi AP đã được khởi tạo thành công!");
    Serial.print("Tên mạng: ");
    Serial.println(AP_SSID);
    Serial.print("Mật khẩu: ");
    Serial.println(AP_PASSWORD);
    Serial.print("Địa chỉ IP của AP: ");
    Serial.println(WiFi.softAPIP());
    
    // In thêm thông tin Debug
    Serial.print("MAC AP: ");
    Serial.println(WiFi.softAPmacAddress());
  } else {
    Serial.println("Lỗi khi khởi tạo WiFi AP!");
  }
  
  // Thiết lập MDNS trong chế độ AP
  if(MDNS.begin("smartdoor")) {
    Serial.println("mDNS đã khởi động - Truy cập qua http://smartdoor.local");
  }
}


// Lưu cấu hình WiFi
void saveWiFiConfig(String ssid, String password) {
  preferences.begin("wifi_config", false);
  preferences.putString("ssid", ssid);
  preferences.putString("password", password);
  preferences.putBool("configured", true);
  preferences.end();
  Serial.println("Đã lưu cấu hình WiFi: SSID=" + ssid);
}

// Tải cấu hình WiFi đã lưu
bool loadWiFiConfig() {
  preferences.begin("wifi_config", true);
  bool isConfigured = preferences.getBool("configured", false);
  
  if(isConfigured) {
    String savedSSID = preferences.getString("ssid", "");
    String savedPassword = preferences.getString("password", "");
    preferences.end();
    
    Serial.println("Đã tải cấu hình WiFi: SSID=" + savedSSID);
    return connectToWiFi(savedSSID, savedPassword);
  }
  
  preferences.end();
  return false;
}

// Xóa cấu hình WiFi
void clearWiFiConfig() {
  preferences.begin("wifi_config", false);
  preferences.clear();
  preferences.end();
  Serial.println("Đã xóa cấu hình WiFi");
}

// Giữ nguyên tất cả các handler hiện có
void handleRoot() {
  String html = "<!DOCTYPE html>"
    "<html lang=\"vi\">"
    "<head>"
    "  <meta charset=\"UTF-8\">"
    "  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">"
    "  <title>Hệ Thống Cửa Thông Minh</title>"
    "  <link rel=\"stylesheet\" href=\"https://cdnjs.cloudflare.com/ajax/libs/font-awesome/6.0.0-beta3/css/all.min.css\">"
    "  <style>"
    "    :root {"
    "      --primary: #2c3e50;"
    "      --accent: #3498db;"
    "      --success: #2ecc71;"
    "      --warning: #f39c12;"
    "      --danger: #e74c3c;"
    "      --light: #ecf0f1;"
    "      --dark: #34495e;"
    "      --border-radius: 8px;"
    "      --shadow: 0 8px 30px rgba(0, 0, 0, 0.12);"
    "      --input-shadow: 0 2px 5px rgba(0, 0, 0, 0.1);"
    "    }"
    "    * {"
    "      margin: 0;"
    "      padding: 0;"
    "      box-sizing: border-box;"
    "    }"
    "    body {"
    "      font-family: 'Segoe UI', Roboto, 'Helvetica Neue', sans-serif;"
    "      background: linear-gradient(135deg, #3498db, #8e44ad);"
    "      background-size: cover;"
    "      background-attachment: fixed;"
    "      color: var(--dark);"
    "      line-height: 1.6;"
    "      height: 100vh;"
    "      display: flex;"
    "      justify-content: center;"
    "      align-items: center;"
    "      padding: 20px;"
    "    }"
    "    .login-container {"
    "      width: 90%;"
    "      max-width: 400px;"
    "      background-color: rgba(255, 255, 255, 0.95);"
    "      border-radius: var(--border-radius);"
    "      box-shadow: var(--shadow);"
    "      overflow: hidden;"
    "      backdrop-filter: blur(10px);"
    "      -webkit-backdrop-filter: blur(10px);"
    "      transform: translateY(0);"
    "      animation: float 6s ease-in-out infinite;"
    "      transition: all 0.3s ease;"
    "    }"
    "    @keyframes float {"
    "      0% { transform: translateY(0); }"
    "      50% { transform: translateY(-10px); }"
    "      100% { transform: translateY(0); }"
    "    }"
    "    .login-header {"
    "      padding: 30px 20px 20px;"
    "      text-align: center;"
    "      position: relative;"
    "    }"
    "    .logo {"
    "      width: 80px;"
    "      height: 80px;"
    "      background-color: var(--accent);"
    "      color: white;"
    "      display: flex;"
    "      align-items: center;"
    "      justify-content: center;"
    "      border-radius: 50%;"
    "      margin: 0 auto 15px;"
    "      box-shadow: 0 4px 15px rgba(52, 152, 219, 0.4);"
    "      position: relative;"
    "      z-index: 2;"
    "    }"
    "    .logo-icon {"
    "      font-size: 36px;"
    "    }"
    "    .wave {"
    "      position: absolute;"
    "      bottom: 0;"
    "      left: 0;"
    "      width: 100%;"
    "      height: 20px;"
    "      background: url(\"data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 1200 120' preserveAspectRatio='none'%3E%3Cpath d='M321.39,56.44c58-10.79,114.16-30.13,172-41.86,82.39-16.72,168.19-17.73,250.45-.39C823.78,31,906.67,72,985.66,92.83c70.05,18.48,146.53,26.09,214.34,3V0H0V27.35A600.21,600.21,0,0,0,321.39,56.44Z' fill='%23ffffff'/%3E%3C/svg%3E\");"
    "      background-size: 1200px 100%;"
    "    }"
    "    .login-header h1 {"
    "      font-size: 24px;"
    "      color: var(--primary);"
    "      margin-bottom: 5px;"
    "      font-weight: 600;"
    "    }"
    "    .login-subtitle {"
    "      color: #7f8c8d;"
    "      font-size: 14px;"
    "      margin-bottom: 5px;"
    "    }"
    "    .login-form {"
    "      padding: 20px 30px 30px;"
    "    }"
    "    .form-group {"
    "      margin-bottom: 20px;"
    "      position: relative;"
    "    }"
    "    .form-group label {"
    "      display: block;"
    "      margin-bottom: 8px;"
    "      font-weight: 500;"
    "      color: var(--dark);"
    "      font-size: 14px;"
    "    }"
    "    .input-with-icon {"
    "      position: relative;"
    "    }"
    "    .input-icon {"
    "      position: absolute;"
    "      left: 15px;"
    "      top: 50%;"
    "      transform: translateY(-50%);"
    "      color: #95a5a6;"
    "      transition: all 0.3s ease;"
    "    }"
    "    .form-control {"
    "      width: 100%;"
    "      padding: 12px 15px 12px 45px;"
    "      border: 1px solid #ddd;"
    "      border-radius: var(--border-radius);"
    "      font-size: 16px;"
    "      transition: all 0.3s ease;"
    "      box-shadow: var(--input-shadow);"
    "      background-color: white;"
    "    }"
    "    .form-control:focus {"
    "      outline: none;"
    "      border-color: var(--accent);"
    "      box-shadow: 0 0 0 3px rgba(52, 152, 219, 0.25);"
    "    }"
    "    .form-control:focus + .input-icon {"
    "      color: var(--accent);"
    "    }"
    "    .remember-me {"
    "      display: flex;"
    "      align-items: center;"
    "      margin-bottom: 20px;"
    "      user-select: none;"
    "    }"
    "    .remember-me input {"
    "      margin-right: 8px;"
    "      accent-color: var(--accent);"
    "    }"
    "    .btn {"
    "      display: block;"
    "      width: 100%;"
    "      padding: 14px 15px;"
    "      background-color: var(--accent);"
    "      color: white;"
    "      border: none;"
    "      border-radius: var(--border-radius);"
    "      font-size: 16px;"
    "      font-weight: 600;"
    "      cursor: pointer;"
    "      transition: all 0.3s ease;"
    "      text-align: center;"
    "      position: relative;"
    "      overflow: hidden;"
    "      z-index: 1;"
    "    }"
    "    .btn:hover {"
    "      background-color: #2980b9;"
    "      transform: translateY(-2px);"
    "      box-shadow: 0 4px 10px rgba(52, 152, 219, 0.4);"
    "    }"
    "    .btn:active {"
    "      transform: translateY(0);"
    "    }"
    "    .btn::after {"
    "      content: '';"
    "      position: absolute;"
    "      top: 50%;"
    "      left: 50%;"
    "      width: 0;"
    "      height: 0;"
    "      background-color: rgba(255, 255, 255, 0.2);"
    "      border-radius: 50%;"
    "      transform: translate(-50%, -50%);"
    "      transition: width 0.5s, height 0.5s;"
    "      z-index: -1;"
    "    }"
    "    .btn:hover::after {"
    "      width: 300px;"
    "      height: 300px;"
    "    }"
    "    .login-footer {"
    "      padding: 15px 30px;"
    "      text-align: center;"
    "      color: #7f8c8d;"
    "      font-size: 14px;"
    "      border-top: 1px solid #f1f1f1;"
    "    }"
    "    .help-links {"
    "      display: flex;"
    "      justify-content: center;"
    "      gap: 15px;"
    "      margin-top: 10px;"
    "    }"
    "    .help-link {"
    "      color: var(--accent);"
    "      text-decoration: none;"
    "      transition: color 0.3s ease;"
    "      font-size: 13px;"
    "    }"
    "    .help-link:hover {"
    "      color: #2980b9;"
    "      text-decoration: underline;"
    "    }"
    "    .particles {"
    "      position: fixed;"
    "      top: 0;"
    "      left: 0;"
    "      width: 100%;"
    "      height: 100%;"
    "      z-index: -1;"
    "      pointer-events: none;"
    "    }"
    "    .particle {"
    "      position: absolute;"
    "      border-radius: 50%;"
    "      background-color: rgba(255, 255, 255, 0.5);"
    "      pointer-events: none;"
    "      z-index: -1;"
    "    }"
    "    @media (max-width: 500px) {"
    "      .login-container {"
    "        box-shadow: none;"
    "      }"
    "      .form-control, .btn {"
    "        padding: 12px 15px;"
    "      }"
    "      .login-header h1 {"
    "        font-size: 22px;"
    "      }"
    "    }"
    "    @keyframes fadeIn {"
    "      from { opacity: 0; transform: translateY(20px); }"
    "      to { opacity: 1; transform: translateY(0); }"
    "    }"
    "    .animate-fade-in {"
    "      animation: fadeIn 0.5s ease forwards;"
    "    }"
    "    .delay-1 { animation-delay: 0.1s; }"
    "    .delay-2 { animation-delay: 0.2s; }"
    "    .delay-3 { animation-delay: 0.3s; }"
    "    .delay-4 { animation-delay: 0.4s; }"
    "    .wifi-setup-link {"
    "      color: #3498db;"
    "      display: flex;"
    "      align-items: center;"
    "      gap: 5px;"
    "    }"
    "    .wifi-setup-link i {"
    "      font-size: 14px;"
    "    }"
    "  </style>"
    "</head>"
    "<body>"
    "  <div class=\"particles\" id=\"particles\"></div>"
    "  <div class=\"login-container\">"
    "    <div class=\"login-header\">"
    "      <div class=\"logo animate-fade-in\">"
    "        <i class=\"fas fa-shield-alt logo-icon\"></i>"
    "      </div>"
    "      <h1 class=\"animate-fade-in delay-1\">Hệ Thống Cửa Thông Minh</h1>"
    "      <p class=\"login-subtitle animate-fade-in delay-2\">Vui lòng đăng nhập để tiếp tục</p>"
    "      <div class=\"wave\"></div>"
    "    </div>"
    "    <form class=\"login-form\" action=\"/login\" method=\"post\">"
    "      <div class=\"form-group animate-fade-in delay-2\">"
    "        <label for=\"username\">Tên đăng nhập</label>"
    "        <div class=\"input-with-icon\">"
    "          <input type=\"text\" id=\"username\" name=\"username\" class=\"form-control\" placeholder=\"Nhập tên đăng nhập\" required autocomplete=\"username\">"
    "          <i class=\"fas fa-user input-icon\"></i>"
    "        </div>"
    "      </div>"
    "      <div class=\"form-group animate-fade-in delay-3\">"
    "        <label for=\"password\">Mật khẩu</label>"
    "        <div class=\"input-with-icon\">"
    "          <input type=\"password\" id=\"password\" name=\"password\" class=\"form-control\" placeholder=\"Nhập mật khẩu\" required autocomplete=\"current-password\">"
    "          <i class=\"fas fa-lock input-icon\"></i>"
    "        </div>"
    "      </div>"
    "      <div class=\"remember-me animate-fade-in delay-3\">"
    "        <input type=\"checkbox\" id=\"remember\" name=\"remember\">"
    "        <label for=\"remember\">Ghi nhớ đăng nhập</label>"
    "      </div>"
    "      <button type=\"submit\" class=\"btn animate-fade-in delay-4\">Đăng nhập <i class=\"fas fa-sign-in-alt\" style=\"margin-left: 8px;\"></i></button>"
    "    </form>"
    "    <div class=\"login-footer animate-fade-in delay-4\">"
    "      <p>&copy; 2025 Smart Door Control System</p>"
    "      <div class=\"help-links\">"
    "        <a href=\"/wifi-setup\" class=\"help-link wifi-setup-link\"><i class=\"fas fa-wifi\"></i> Cấu hình WiFi</a>"
    "        <a href=\"#\" onclick=\"forgotPassword(); return false;\" class=\"help-link\">Quên mật khẩu?</a>"
    "        <a href=\"#\" onclick=\"needHelp(); return false;\" class=\"help-link\">Cần trợ giúp?</a>"
    "      </div>"
    "    </div>"
    "  </div>"
    "  <script>"
    "    // Hiệu ứng hạt particles cho background"
    "    function createParticles() {"
    "      const container = document.getElementById('particles');"
    "      const particleCount = 50;"
    "      "
    "      for (let i = 0; i < particleCount; i++) {"
    "        const particle = document.createElement('div');"
    "        particle.className = 'particle';"
    "        "
    "        // Kích thước ngẫu nhiên"
    "        const size = Math.random() * 5 + 2;"
    "        particle.style.width = `${size}px`;"
    "        particle.style.height = `${size}px`;"
    "        "
    "        // Vị trí ngẫu nhiên"
    "        particle.style.left = `${Math.random() * 100}%`;"
    "        particle.style.top = `${Math.random() * 100}%`;"
    "        "
    "        // Thời gian di chuyển ngẫu nhiên"
    "        const duration = Math.random() * 20 + 10;"
    "        particle.style.animation = `float ${duration}s linear infinite`;"
    "        "
    "        // Độ mờ ngẫu nhiên"
    "        particle.style.opacity = Math.random() * 0.5 + 0.2;"
    "        "
    "        container.appendChild(particle);"
    "      }"
    "    }"
    "    "
    "    // Toggle hiện/ẩn mật khẩu"
    "    function setupPasswordToggle() {"
    "      const passwordInput = document.getElementById('password');"
    "      const icon = passwordInput.nextElementSibling;"
    "      "
    "      icon.addEventListener('click', function() {"
    "        const type = passwordInput.getAttribute('type') === 'password' ? 'text' : 'password';"
    "        passwordInput.setAttribute('type', type);"
    "        icon.classList.toggle('fa-lock');"
    "        icon.classList.toggle('fa-eye');"
    "      });"
    "    }"
    "    "
    "    function forgotPassword() {"
    "      alert('Vui lòng liên hệ quản trị viên để được cấp lại mật khẩu.');"
    "    }"
    "    "
    "    function needHelp() {"
    "      alert('Để được hỗ trợ, vui lòng liên hệ với quản trị viên hệ thống.');"
    "    }"
    "    "
    "    window.onload = function() {"
    "      createParticles();"
    "      // Đặt focus vào trường đăng nhập"
    "      document.getElementById('username').focus();"
    "    };"
    "  </script>"
    "</body>"
    "</html>";
  
  server.send(200, "text/html", html);
}


void handleLogin() {
  String username = server.arg("username");
  String password = server.arg("password");
  
  if(authenticateUser(username, password)) {
    if(isAdmin(username)) {
      server.sendHeader("Location", "/admin");
      server.send(302);
    } else {
      server.sendHeader("Location", "/dashboard?user=" + username);
      server.send(302);
    }
  } else {
    String html = "<!DOCTYPE html>"
      "<html lang=\"vi\">"
      "<head>"
      "  <meta charset=\"UTF-8\">"
      "  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">"
      "  <title>Đăng nhập thất bại</title>"
      "  <link rel=\"stylesheet\" href=\"https://cdnjs.cloudflare.com/ajax/libs/font-awesome/6.0.0-beta3/css/all.min.css\">"
      "  <style>"
      "    :root {"
      "      --primary-color: #3498db;"
      "      --error-color: #e74c3c;"
      "      --button-color: #2ecc71;"
      "      --hover-color: #27ae60;"
      "      --text-color: #333333;"
      "      --light-bg: #f5f7fa;"
      "      --shadow: 0 4px 12px rgba(0,0,0,0.1);"
      "      --border-radius: 8px;"
      "    }"
      "    * { margin: 0; padding: 0; box-sizing: border-box; }"
      "    body {"
      "      font-family: 'Segoe UI', Roboto, 'Helvetica Neue', sans-serif;"
      "      background-color: var(--light-bg);"
      "      color: var(--text-color);"
      "      line-height: 1.6;"
      "      height: 100vh;"
      "      display: flex;"
      "      justify-content: center;"
      "      align-items: center;"
      "    }"
      "    .container {"
      "      width: 90%;"
      "      max-width: 400px;"
      "      background-color: white;"
      "      border-radius: var(--border-radius);"
      "      box-shadow: var(--shadow);"
      "      padding: 30px;"
      "      text-align: center;"
      "      animation: fadeIn 0.5s ease-out;"
      "    }"
      "    @keyframes fadeIn {"
      "      0% { opacity: 0; transform: translateY(-20px); }"
      "      100% { opacity: 1; transform: translateY(0); }"
      "    }"
      "    .icon-container {"
      "      width: 80px;"
      "      height: 80px;"
      "      background-color: rgba(231, 76, 60, 0.1);"
      "      border-radius: 50%;"
      "      display: flex;"
      "      align-items: center;"
      "      justify-content: center;"
      "      margin: 0 auto 20px;"
      "    }"
      "    .icon {"
      "      font-size: 40px;"
      "      color: var(--error-color);"
      "    }"
      "    h1 {"
      "      font-size: 24px;"
      "      font-weight: 600;"
      "      margin-bottom: 10px;"
      "      color: var(--text-color);"
      "    }"
      "    .error-message {"
      "      background-color: rgba(231, 76, 60, 0.1);"
      "      border-left: 4px solid var(--error-color);"
      "      padding: 15px;"
      "      border-radius: 4px;"
      "      margin: 20px 0;"
      "      text-align: left;"
      "      color: var(--text-color);"
      "    }"
      "    .help-text {"
      "      font-size: 14px;"
      "      color: #7f8c8d;"
      "      margin-bottom: 20px;"
      "    }"
      "    .btn {"
      "      display: inline-block;"
      "      background-color: var(--button-color);"
      "      color: white;"
      "      padding: 12px 20px;"
      "      border: none;"
      "      border-radius: var(--border-radius);"
      "      cursor: pointer;"
      "      font-size: 16px;"
      "      font-weight: 500;"
      "      transition: all 0.3s ease;"
      "      text-decoration: none;"
      "    }"
      "    .btn:hover {"
      "      background-color: var(--hover-color);"
      "      transform: translateY(-2px);"
      "      box-shadow: 0 4px 8px rgba(0,0,0,0.1);"
      "    }"
      "    .btn-retry {"
      "      background-color: var(--primary-color);"
      "    }"
      "    .btn-retry:hover {"
      "      background-color: #2980b9;"
      "    }"
      "    .btn-icon {"
      "      margin-right: 8px;"
      "    }"
      "    .footer {"
      "      margin-top: 30px;"
      "      font-size: 13px;"
      "      color: #7f8c8d;"
      "    }"
      "    .or-divider {"
      "      display: flex;"
      "      align-items: center;"
      "      margin: 15px 0;"
      "    }"
      "    .or-divider:before, .or-divider:after {"
      "      content: '';"
      "      flex: 1;"
      "      border-bottom: 1px solid #ddd;"
      "    }"
      "    .or-divider span {"
      "      padding: 0 10px;"
      "      color: #7f8c8d;"
      "      font-size: 14px;"
      "    }"
      "  </style>"
      "</head>"
      "<body>"
      "  <div class=\"container\">"
      "    <div class=\"icon-container\">"
      "      <i class=\"fas fa-exclamation-triangle icon\"></i>"
      "    </div>"
      "    <h1>Đăng nhập thất bại</h1>"
      "    <div class=\"error-message\">"
      "      <i class=\"fas fa-info-circle\"></i> Tên đăng nhập hoặc mật khẩu không đúng."
      "    </div>"
      "    <p class=\"help-text\">Vui lòng kiểm tra lại thông tin đăng nhập hoặc liên hệ quản trị viên nếu bạn cần trợ giúp.</p>"
      "    <a href=\"/\" class=\"btn btn-retry\">"
      "      <i class=\"fas fa-redo btn-icon\"></i>Thử lại"
      "    </a>"
      "    <div class=\"or-divider\">"
      "      <span>HOẶC</span>"
      "    </div>"
      "    <div class=\"other-options\">"
      "      <button onclick=\"forgotPassword()\" class=\"btn\">"
      "        <i class=\"fas fa-key btn-icon\"></i>Quên mật khẩu"
      "      </button>"
      "    </div>"
      "    <div class=\"footer\">"
      "      &copy; 2025 Hệ thống Cửa thông minh"
      "    </div>"
      "  </div>"
      "  <script>"
      "    function forgotPassword() {"
      "      alert('Vui lòng liên hệ quản trị viên để được cấp lại mật khẩu.');"
      "    }"
      "    // Tự động chuyển hướng sau 10 giây"
      "    setTimeout(function() {"
      "      window.location.href = '/';"
      "    }, 10000);"
      "  </script>"
      "</body>"
      "</html>";
    
    server.send(200, "text/html", html);
  }
}

void handleAdminPanel() {
  String html = "<!DOCTYPE html>"
    "<html lang=\"vi\">"
    "<head>"
    "  <meta charset=\"UTF-8\">"
    "  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">"
    "  <title>Trang quản trị</title>"
    "  <link rel=\"stylesheet\" href=\"https://cdnjs.cloudflare.com/ajax/libs/font-awesome/6.1.1/css/all.min.css\">"
    "  <style>"
    "    :root {"
    "      --primary-color: #4361ee;"
    "      --danger-color: #ef233c;"
    "      --success-color: #38b000;"
    "      --text-color: #2b2d42;"
    "      --light-bg: #f8f9fa;"
    "      --border-color: #dee2e6;"
    "      --shadow: 0 4px 6px rgba(0, 0, 0, 0.1);"
    "    }"
    "    * { box-sizing: border-box; }"
    "    body {"
    "      font-family: 'Segoe UI', Roboto, Oxygen, Ubuntu, sans-serif;"
    "      margin: 0;"
    "      padding: 0;"
    "      color: var(--text-color);"
    "      background-color: #f0f2f5;"
    "    }"
    "    .container {"
    "      max-width: 1000px;"
    "      margin: 0 auto;"
    "      padding: 20px;"
    "    }"
    "    .header {"
    "      display: flex;"
    "      justify-content: space-between;"
    "      align-items: center;"
    "      padding: 15px 0;"
    "      margin-bottom: 25px;"
    "      border-bottom: 1px solid var(--border-color);"
    "    }"
    "    .header h1 {"
    "      margin: 0;"
    "      color: var(--primary-color);"
    "      font-weight: 600;"
    "    }"
    "    .header-controls {"
    "      display: flex;"
    "      gap: 10px;"
    "    }"
    "    .wifi-btn {"
    "      background-color: #3498db;"
    "      color: white;"
    "    }"
    "    .panel {"
    "      background-color: white;"
    "      border-radius: 10px;"
    "      padding: 25px;"
    "      margin-bottom: 25px;"
    "      box-shadow: var(--shadow);"
    "    }"
    "    .panel h2 {"
    "      margin-top: 0;"
    "      font-size: 20px;"
    "      color: var(--primary-color);"
    "      border-bottom: 2px solid var(--border-color);"
    "      padding-bottom: 10px;"
    "      margin-bottom: 20px;"
    "      display: flex;"
    "      align-items: center;"
    "    }"
    "    .panel h2 i {"
    "      margin-right: 10px;"
    "    }"
    "    .panel h3 {"
    "      font-size: 18px;"
    "      margin: 20px 0 15px 0;"
    "    }"
    "    button {"
    "      background-color: var(--primary-color);"
    "      color: white;"
    "      padding: 10px 16px;"
    "      border: none;"
    "      border-radius: 6px;"
    "      cursor: pointer;"
    "      font-weight: 500;"
    "      font-size: 14px;"
    "      transition: all 0.2s ease;"
    "      display: inline-flex;"
    "      align-items: center;"
    "      justify-content: center;"
    "      margin: 5px;"
    "    }"
    "    button i {"
    "      margin-right: 6px;"
    "    }"
    "    button:hover {"
    "      opacity: 0.9;"
    "      transform: translateY(-1px);"
    "    }"
    "    button:active {"
    "      transform: translateY(1px);"
    "    }"
    "    button.danger {"
    "      background-color: var(--danger-color);"
    "    }"
    "    button.success {"
    "      background-color: var(--success-color);"
    "    }"
    "    button:disabled {"
    "      background-color: #ccc;"
    "      cursor: not-allowed;"
    "      opacity: 0.7;"
    "    }"
    "    .door-controls {"
    "      display: flex;"
    "      align-items: center;"
    "      justify-content: space-between;"
    "    }"
    "    .status {"
    "      font-size: 16px;"
    "      padding: 8px 15px;"
    "      border-radius: 6px;"
    "      margin: 10px 0;"
    "      background-color: var(--light-bg);"
    "      display: inline-flex;"
    "      align-items: center;"
    "    }"
    "    .status i {"
    "      margin-right: 8px;"
    "    }"
    "    .status.open {"
    "      background-color: #d4edda;"
    "      color: #155724;"
    "    }"
    "    .status.closed {"
    "      background-color: #f8d7da;"
    "      color: #721c24;"
    "    }"
    "    table {"
    "      width: 100%;"
    "      border-collapse: collapse;"
    "      margin-top: 10px;"
    "      background-color: white;"
    "    }"
    "    th, td {"
    "      padding: 12px 15px;"
    "      text-align: left;"
    "      border-bottom: 1px solid var(--border-color);"
    "    }"
    "    th {"
    "      background-color: var(--light-bg);"
    "      font-weight: 600;"
    "      color: var(--primary-color);"
    "    }"
    "    tr:hover {"
    "      background-color: rgba(67, 97, 238, 0.05);"
    "    }"
    "    .hidden {"
    "      display: none;"
    "    }"
    "    .form-group {"
    "      margin-bottom: 15px;"
    "    }"
    "    .form-group label {"
    "      display: block;"
    "      margin-bottom: 5px;"
    "      font-weight: 500;"
    "    }"
    "    .form-actions {"
    "      display: flex;"
    "      justify-content: flex-start;"
    "      gap: 10px;"
    "      margin-top: 20px;"
    "    }"
    "    input, select {"
    "      padding: 10px 12px;"
    "      border: 1px solid var(--border-color);"
    "      border-radius: 6px;"
    "      width: 100%;"
    "      font-size: 14px;"
    "    }"
    "    input:focus, select:focus {"
    "      outline: none;"
    "      border-color: var(--primary-color);"
    "      box-shadow: 0 0 0 3px rgba(67, 97, 238, 0.2);"
    "    }"
    "    .checkbox-container {"
    "      display: flex;"
    "      align-items: center;"
    "      margin: 10px 0;"
    "    }"
    "    .checkbox-container input[type='checkbox'] {"
    "      width: auto;"
    "      margin-right: 10px;"
    "    }"
    "    .user-form {"
    "      background-color: var(--light-bg);"
    "      padding: 20px;"
    "      border-radius: 6px;"
    "      margin-top: 15px;"
    "      margin-bottom: 20px;"
    "      border-left: 4px solid var(--primary-color);"
    "    }"
    "    .badge {"
    "      padding: 4px 8px;"
    "      border-radius: 20px;"
    "      font-size: 12px;"
    "      font-weight: 500;"
    "    }"
    "    .badge-admin {"
    "      background-color: #4361ee;"
    "      color: white;"
    "    }"
    "    .badge-user {"
    "      background-color: #e9ecef;"
    "      color: #495057;"
    "    }"
    "    .log-time {"
    "      white-space: nowrap;"
    "      color: #666;"
    "    }"
    "    .empty-message {"
    "      text-align: center;"
    "      padding: 20px;"
    "      color: #6c757d;"
    "      font-style: italic;"
    "    }"
    "    @media screen and (max-width: 768px) {"
    "      .container {"
    "        padding: 10px;"
    "      }"
    "      .panel {"
    "        padding: 15px;"
    "      }"
    "      .door-controls {"
    "        flex-direction: column;"
    "        align-items: flex-start;"
    "      }"
    "      .form-actions {"
    "        flex-direction: column;"
    "      }"
    "      .form-actions button {"
    "        width: 100%;"
    "      }"
    "      table {"
    "        font-size: 14px;"
    "      }"
    "      .header-controls {"
    "        flex-direction: column;"
    "      }"
    "    }"
    "  </style>"
    "</head>"
    "<body>"
    "  <div class=\"container\">"
    "    <div class=\"header\">"
    "      <h1><i class=\"fas fa-shield-alt\"></i> Trang quản trị hệ thống</h1>"
    "      <div class=\"header-controls\">"
    "        <button onclick=\"window.location.href='/wifi-setup'\" class=\"wifi-btn\"><i class=\"fas fa-wifi\"></i> Cấu hình WiFi</button>"
    "        <button onclick=\"window.location.href='/'\"><i class=\"fas fa-sign-out-alt\"></i> Đăng xuất</button>"
    "      </div>"
    "    </div>"
    "    <div class=\"panel\">"
    "      <h2><i class=\"fas fa-door-open\"></i> Điều khiển cửa</h2>"
    "      <div class=\"door-controls\">"
    "        <div id=\"doorStatus\" class=\"status\">"
    "          <i class=\"fas fa-spinner fa-spin\"></i> Đang tải trạng thái cửa..."
    "        </div>"
    "        <button id=\"doorBtn\" class=\"success\" onclick=\"toggleDoor()\"><i class=\"fas fa-key\"></i> Mở cửa</button>"
    "      </div>"
    "    </div>"
    "    "
        "    <div class=\"panel\">"
    "      <h2><i class=\"fas fa-id-card\"></i> Quản lý thẻ RFID</h2>"
    "      <div class=\"info-box\" style=\"background-color: #e3f2fd; border-left: 4px solid #4361ee; padding: 15px; margin-bottom: 20px; border-radius: 4px;\">"
    "        <p style=\"margin: 0;\"><i class=\"fas fa-info-circle\"></i> <strong>Lưu ý quan trọng:</strong> Việc thêm, xóa và quản lý thẻ RFID, vân tay, và mã PIN phải được thực hiện trực tiếp thông qua giao diện vật lý của Arduino (bàn phím và màn hình LCD). Các chức năng này không có sẵn thông qua giao diện web.</p>"
    "      </div>"
    "      <div class=\"info-secondary\" style=\"background-color: #fff3cd; border-left: 4px solid #ffc107; padding: 15px; border-radius: 4px;\">"
    "        <p style=\"margin: 0;\"><i class=\"fas fa-lightbulb\"></i> <strong>Hướng dẫn:</strong> Để quản lý thẻ RFID, vui lòng sử dụng menu Admin trên thiết bị Arduino bằng cách nhập mã PIN quản trị viên trên bàn phím.</p>"
    "      </div>"
    "    </div>"
    "     "
    "    <div class=\"panel\">"
    "      <h2><i class=\"fas fa-users\"></i> Quản lý người dùng</h2>"
    "      <button onclick=\"toggleUserForm()\"><i class=\"fas fa-user-plus\"></i> Thêm người dùng</button>"
    "      "
    "      <div id=\"userForm\" class=\"user-form hidden\">"
    "        <h3><i class=\"fas fa-user-plus\"></i> Thêm người dùng mới</h3>"
    "        <div class=\"form-group\">"
    "          <label for=\"newUsername\">Tên đăng nhập</label>"
    "          <input type=\"text\" id=\"newUsername\" placeholder=\"Nhập tên đăng nhập\">"
    "        </div>"
    "        <div class=\"form-group\">"
    "          <label for=\"newPassword\">Mật khẩu</label>"
    "          <input type=\"password\" id=\"newPassword\" placeholder=\"Nhập mật khẩu\">"
    "        </div>"
    "        <div class=\"checkbox-container\">"
    "          <input type=\"checkbox\" id=\"isAdmin\">"
    "          <label for=\"isAdmin\">Quyền quản trị viên</label>"
    "        </div>"
    "        <div class=\"form-actions\">"
    "          <button onclick=\"addUser()\"><i class=\"fas fa-save\"></i> Lưu</button>"
    "          <button class=\"danger\" onclick=\"toggleUserForm()\"><i class=\"fas fa-times\"></i> Hủy</button>"
    "        </div>"
    "      </div>"
    "      "
    "      <h3><i class=\"fas fa-list\"></i> Danh sách người dùng</h3>"
    "      <div id=\"userTableContainer\">"
    "        <table id=\"userTable\">"
    "          <thead>"
    "            <tr>"
    "              <th>Tên đăng nhập</th>"
    "              <th>Quyền hạn</th>"
    "              <th>Thao tác</th>"
    "            </tr>"
    "          </thead>"
    "          <tbody>"
    "            <tr>"
    "              <td colspan=\"3\" class=\"empty-message\">Đang tải danh sách người dùng...</td>"
    "            </tr>"
    "          </tbody>"
    "        </table>"
    "      </div>"
    "    </div>"
    "    "
    "    <div class=\"panel\">"
    "      <h2><i class=\"fas fa-history\"></i> Nhật ký hoạt động</h2>"
    "      <div id=\"logTableContainer\">"
    "        <table id=\"logTable\">"
    "          <thead>"
    "            <tr>"
    "              <th>Thời gian</th>"
    "              <th>Người dùng</th>"
    "              <th>Hành động</th>"
    "            </tr>"
    "          </thead>"
    "          <tbody>"
    "            <tr>"
    "              <td colspan=\"3\" class=\"empty-message\">Đang tải nhật ký hoạt động...</td>"
    "            </tr>"
    "          </tbody>"
    "        </table>"
    "      </div>"
    "    </div>"
    "  </div>";

  // JavaScript riêng biệt
  html += "<script>"
    "let doorOpen = false;"
    "window.onload = function() {"
    "  updateDoorStatus();"
    "  loadUsers();"
    "  loadLogs();"
    "  setInterval(updateDoorStatus, 5000);"
    "  setInterval(loadLogs, 10000);"
    "};"
    
    "function updateDoorStatus() {"
    "  fetch('/api/door-status')"
    "    .then(response => response.json())"
    "    .then(data => {"
    "      doorOpen = data.isOpen;"
    "      const statusEl = document.getElementById('doorStatus');"
    "      const btnEl = document.getElementById('doorBtn');"
    "      "
    "      if (doorOpen) {"
    "        statusEl.className = 'status open';"
    "        statusEl.innerHTML = '<i class=\"fas fa-door-open\"></i> Trạng thái: Đang mở';"
    "        btnEl.innerHTML = '<i class=\"fas fa-door-closed\"></i> Đóng cửa';"
    "        btnEl.className = 'danger';"
    "      } else {"
    "        statusEl.className = 'status closed';"
    "        statusEl.innerHTML = '<i class=\"fas fa-door-closed\"></i> Trạng thái: Đã đóng';"
    "        btnEl.innerHTML = '<i class=\"fas fa-key\"></i> Mở cửa';"
    "        btnEl.className = 'success';"
    "      }"
    "      btnEl.disabled = doorOpen;"
    "    })"
    "    .catch(error => {"
    "      console.error('Error fetching door status:', error);"
    "      document.getElementById('doorStatus').innerHTML = '<i class=\"fas fa-exclamation-triangle\"></i> Lỗi kết nối';"
    "    });"
    "}"
    
    "function toggleDoor() {"
    "  const btnEl = document.getElementById('doorBtn');"
    "  const statusEl = document.getElementById('doorStatus');"
    "  "
    "  btnEl.disabled = true;"
    "  statusEl.innerHTML = '<i class=\"fas fa-spinner fa-spin\"></i> Đang xử lý...';"
    "  "
    "  fetch('/api/open-door', { method: 'POST' })"
    "    .then(response => response.json())"
    "    .then(data => {"
    "      if (data.success) {"
    "        updateDoorStatus();"
    "      } else {"
    "        showAlert('Lỗi: ' + data.message, 'danger');"
    "        updateDoorStatus();"
    "      }"
    "    })"
    "    .catch(error => {"
    "      console.error('Error toggling door:', error);"
    "      showAlert('Lỗi kết nối server', 'danger');"
    "      updateDoorStatus();"
    "    });"
    "}"
    
    "function toggleUserForm() {"
    "  const form = document.getElementById('userForm');"
    "  form.classList.toggle('hidden');"
    "}"
    
    "function loadUsers() {"
    "  const tableBody = document.querySelector('#userTable tbody');"
    "  tableBody.innerHTML = '<tr><td colspan=\"3\" class=\"empty-message\"><i class=\"fas fa-spinner fa-spin\"></i> Đang tải danh sách người dùng...</td></tr>';"
    "  "
    "  fetch('/api/users')"
    "    .then(response => response.json())"
    "    .then(data => {"
    "      tableBody.innerHTML = '';"
    "      "
    "      if (data.length === 0) {"
    "        tableBody.innerHTML = '<tr><td colspan=\"3\" class=\"empty-message\">Chưa có người dùng nào</td></tr>';"
    "        return;"
    "      }"
    "      "
    "      data.forEach(user => {"
    "        const row = document.createElement('tr');"
    "        "
    "        const usernameCell = document.createElement('td');"
    "        usernameCell.textContent = user.username;"
    "        row.appendChild(usernameCell);"
    "        "
    "        const roleCell = document.createElement('td');"
    "        const roleBadge = document.createElement('span');"
    "        roleBadge.className = user.isAdmin ? 'badge badge-admin' : 'badge badge-user';"
    "        roleBadge.textContent = user.isAdmin ? 'Quản trị viên' : 'Người dùng';"
    "        roleCell.appendChild(roleBadge);"
    "        row.appendChild(roleCell);"
    "        "
    "        const actionsCell = document.createElement('td');"
    "        const deleteBtn = document.createElement('button');"
    "        deleteBtn.className = 'danger';"
    "        deleteBtn.innerHTML = '<i class=\"fas fa-trash\"></i> Xóa';"
    "        deleteBtn.onclick = function() { deleteUser(user.username); };"
    "        actionsCell.appendChild(deleteBtn);"
    "        row.appendChild(actionsCell);"
    "        "
    "        tableBody.appendChild(row);"
    "      });"
    "    })"
    "    .catch(error => {"
    "      console.error('Error loading users:', error);"
    "      tableBody.innerHTML = '<tr><td colspan=\"3\" class=\"empty-message\"><i class=\"fas fa-exclamation-triangle\"></i> Lỗi khi tải danh sách người dùng</td></tr>';"
    "    });"
    "}"
    
    "function addUser() {"
    "  const username = document.getElementById('newUsername').value;"
    "  const password = document.getElementById('newPassword').value;"
    "  const isAdmin = document.getElementById('isAdmin').checked;"
    "  "
    "  if (!username || !password) {"
    "    showAlert('Vui lòng nhập đầy đủ thông tin', 'danger');"
    "    return;"
    "  }"
    "  "
    "  fetch('/api/add-user', {"
    "    method: 'POST',"
    "    headers: { 'Content-Type': 'application/json' },"
    "    body: JSON.stringify({ username, password, isAdmin })"
    "  })"
    "  .then(response => response.json())"
    "  .then(data => {"
    "    if (data.success) {"
    "      showAlert('Đã thêm người dùng thành công', 'success');"
    "      document.getElementById('newUsername').value = '';"
    "      document.getElementById('newPassword').value = '';"
    "      document.getElementById('isAdmin').checked = false;"
    "      toggleUserForm();"
    "      loadUsers();"
    "    } else {"
    "      showAlert('Lỗi: ' + data.message, 'danger');"
    "    }"
    "  })"
    "  .catch(error => {"
    "    console.error('Error adding user:', error);"
    "    showAlert('Lỗi kết nối server', 'danger');"
    "  });"
    "}"
    
    "function deleteUser(username) {"
    "  if (confirm('Bạn có chắc chắn muốn xóa người dùng ' + username + '?')) {"
    "    fetch('/api/delete-user', {"
    "      method: 'POST',"
    "      headers: { 'Content-Type': 'application/json' },"
    "      body: JSON.stringify({ username })"
    "    })"
    "    .then(response => response.json())"
    "    .then(data => {"
    "      if (data.success) {"
    "        showAlert('Đã xóa người dùng thành công', 'success');"
    "        loadUsers();"
    "      } else {"
    "        showAlert('Lỗi: ' + data.message, 'danger');"
    "      }"
    "    })"
    "    .catch(error => {"
    "      console.error('Error deleting user:', error);"
    "      showAlert('Lỗi kết nối server', 'danger');"
    "    });"
    "  }"
    "}"
    
    "function loadLogs() {"
    "  const tableBody = document.querySelector('#logTable tbody');"
    "  "
    "  fetch('/api/logs')"
    "    .then(response => response.json())"
    "    .then(data => {"
    "      tableBody.innerHTML = '';"
    "      "
    "      if (data.length === 0) {"
    "        tableBody.innerHTML = '<tr><td colspan=\"3\" class=\"empty-message\">Chưa có nhật ký hoạt động nào</td></tr>';"
    "        return;"
    "      }"
    "      "
    "      data.forEach(log => {"
    "        const row = document.createElement('tr');"
    "        "
    "        const timeCell = document.createElement('td');"
    "        timeCell.className = 'log-time';"
    "        timeCell.textContent = log.time;"
    "        row.appendChild(timeCell);"
    "        "
    "        const userCell = document.createElement('td');"
    "        userCell.textContent = log.user;"
    "        row.appendChild(userCell);"
    "        "
    "        const actionCell = document.createElement('td');"
    "        actionCell.textContent = log.action;"
    "        row.appendChild(actionCell);"
    "        "
    "        tableBody.appendChild(row);"
    "      });"
    "    })"
    "    .catch(error => {"
    "      console.error('Error loading logs:', error);"
    "      tableBody.innerHTML = '<tr><td colspan=\"3\" class=\"empty-message\"><i class=\"fas fa-exclamation-triangle\"></i> Lỗi khi tải nhật ký hoạt động</td></tr>';"
    "    });"
    "}"
    
    "function showAlert(message, type) {"
    "  const alertDiv = document.createElement('div');"
    "  alertDiv.style.position = 'fixed';"
    "  alertDiv.style.top = '20px';"
    "  alertDiv.style.right = '20px';"
    "  alertDiv.style.padding = '15px 20px';"
    "  alertDiv.style.borderRadius = '5px';"
    "  alertDiv.style.color = 'white';"
    "  alertDiv.style.zIndex = '1000';"
    "  alertDiv.style.boxShadow = '0 4px 6px rgba(0, 0, 0, 0.1)';"
    "  alertDiv.style.display = 'flex';"
    "  alertDiv.style.alignItems = 'center';"
    "  alertDiv.style.gap = '10px';"
    "  "
    "  if (type === 'success') {"
    "    alertDiv.style.backgroundColor = '#38b000';"
    "    alertDiv.innerHTML = '<i class=\"fas fa-check-circle\"></i> ' + message;"
    "  } else if (type === 'danger') {"
    "    alertDiv.style.backgroundColor = '#ef233c';"
    "    alertDiv.innerHTML = '<i class=\"fas fa-exclamation-circle\"></i> ' + message;"
    "  } else {"
    "    alertDiv.style.backgroundColor = '#4361ee';"
    "    alertDiv.innerHTML = '<i class=\"fas fa-info-circle\"></i> ' + message;"
    "  }"
    "  "
    "  document.body.appendChild(alertDiv);"
    "  "
    "  setTimeout(() => {"
    "    alertDiv.style.opacity = '0';"
    "    alertDiv.style.transition = 'opacity 0.5s ease';"
    "    setTimeout(() => {"
    "      document.body.removeChild(alertDiv);"
    "    }, 500);"
    "  }, 3000);"
    "}"
    "function checkArduinoStatus() {"
    "  fetch('/api/arduino-status')"
    "    .then(response => response.json())"
    "    .then(data => {"
    "      const statusEl = document.getElementById('arduinoStatus');"
    "      if (data.connected) {"
    "        statusEl.className = 'status open';"
    "        statusEl.innerHTML = '<i class=\"fas fa-check-circle\"></i> Arduino connected - Door controller online';"
    "      } else {"
    "        statusEl.className = 'status closed';"
    "        statusEl.innerHTML = '<i class=\"fas fa-exclamation-triangle\"></i> Arduino not connected - Door control unavailable';"
    "      }"
    "    })"
    "    .catch(error => {"
    "      console.error('Error checking Arduino status:', error);"
    "      document.getElementById('arduinoStatus').innerHTML = '<i class=\"fas fa-exclamation-triangle\"></i> Error checking connection';"
    "    });"
    "}"
    "</script>"
    "</body>"
    "</html>";
  
  server.send(200, "text/html", html);
}

void handleUserDashboard() {
  String username = server.arg("user");
  
  // Kiểm tra người dùng hợp lệ
  bool validUser = false;
  for(int i = 0; i < userCount; i++) {
    if(users[i].username == username && users[i].active) {
      validUser = true;
      break;
    }
  }
  
  if(!validUser) {
    server.sendHeader("Location", "/");
    server.send(302);
    return;
  }
  
  String html = "<!DOCTYPE html>"
    "<html lang=\"vi\">"
    "<head>"
    "  <meta charset=\"UTF-8\">"
    "  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">"
    "  <title>Điều khiển cửa thông minh</title>"
    "  <link href=\"https://fonts.googleapis.com/css2?family=Roboto:wght@300;400;500;700&display=swap\" rel=\"stylesheet\">"
    "  <link rel=\"stylesheet\" href=\"https://cdnjs.cloudflare.com/ajax/libs/font-awesome/6.0.0-beta3/css/all.min.css\">"
    "  <style>"
    "    :root {"
    "      --primary-color: #4361ee;"
    "      --primary-dark: #2c41c2;"
    "      --primary-light: #eef1ff;"
    "      --success-color: #2ecc71;"
    "      --success-light: #e3f9ed;"
    "      --warning-color: #f39c12;"
    "      --danger-color: #e74c3c;"
    "      --danger-light: #fdeeec;"
    "      --grey-color: #95a5a6;"
    "      --grey-light: #f8f9fa;"
    "      --dark-color: #2c3e50;"
    "      --light-color: #ecf0f1;"
    "      --shadow-sm: 0 2px 5px rgba(0,0,0,0.08);"
    "      --shadow-md: 0 5px 15px rgba(0,0,0,0.1);"
    "      --shadow-lg: 0 10px 25px rgba(0,0,0,0.15);"
    "      --transition: all 0.3s ease;"
    "    }"
    "    * { margin: 0; padding: 0; box-sizing: border-box; }"
    "    body {"
    "      font-family: 'Roboto', sans-serif;"
    "      background-color: #f0f2f5;"
    "      color: var(--dark-color);"
    "      min-height: 100vh;"
    "      padding: 0;"
    "      margin: 0;"
    "    }"
    "    .navbar {"
    "      background-color: white;"
    "      box-shadow: var(--shadow-sm);"
    "      padding: 16px 24px;"
    "      display: flex;"
    "      justify-content: space-between;"
    "      align-items: center;"
    "      position: sticky;"
    "      top: 0;"
    "      z-index: 1000;"
    "    }"
    "    .navbar h1 {"
    "      font-size: 1.3rem;"
    "      font-weight: 600;"
    "      color: var(--primary-color);"
    "      margin: 0;"
    "      display: flex;"
    "      align-items: center;"
    "      gap: 8px;"
    "    }"
    "    .navbar h1 i {"
    "      color: var(--primary-color);"
    "    }"
    "    .navbar .user {"
    "      display: flex;"
    "      align-items: center;"
    "      gap::12px;"
    "    }"
    "    .navbar .user-info {"
    "      display: flex;"
    "      align-items: center;"
    "      gap: 8px;"
    "      padding: 8px 12px;"
    "      background-color: var(--primary-light);"
    "      border-radius: 30px;"
    "    }"
    "    .navbar .user-icon {"
    "      background-color: var(--primary-color);"
    "      color: white;"
    "      width: 34px;"
    "      height: 34px;"
    "      border-radius: 50%;"
    "      display: flex;"
    "      align-items: center;"
    "      justify-content: center;"
    "      font-size: 14px;"
    "      font-weight: 500;"
    "      box-shadow: var(--shadow-sm);"
    "    }"
    "    .navbar .username {"
    "      font-weight: 500;"
    "      color: var(--primary-dark);"
    "    }"
    "    .logout-btn {"
    "      background-color: white;"
    "      color: var(--dark-color);"
    "      border: 1px solid #e0e0e0;"
    "      padding: 8px 15px;"
    "      border-radius: 30px;"
    "      cursor: pointer;"
    "      font-size: 14px;"
    "      font-weight: 500;"
    "      display: flex;"
    "      align-items: center;"
    "      gap: 6px;"
    "      transition: var(--transition);"
    "    }"
    "    .logout-btn:hover {"
    "      background-color: var(--danger-light);"
    "      color: var(--danger-color);"
    "      border-color: var(--danger-color);"
    "    }"
    "    .wifi-btn {"
    "      background-color: var(--primary-light);"
    "      color: var(--primary-dark);"
    "      border: 1px solid var(--primary-light);"
    "      padding: 8px 15px;"
    "      border-radius: 30px;"
    "      cursor: pointer;"
    "      font-size: 14px;"
    "      font-weight: 500;"
    "      display: flex;"
    "      align-items: center;"
    "      gap: 6px;"
    "      transition: var(--transition);"
    "      text-decoration: none;"
    "      margin-right: 10px;"
    "    }"
    "    .wifi-btn:hover {"
    "      background-color: var(--primary-color);"
    "      color: white;"
    "    }"
    "    .page-content {"
    "      display: flex;"
    "      justify-content: center;"
    "      align-items: center;"
    "      min-height: calc(100vh - 70px);"
    "      padding: 20px;"
    "    }"
    "    .container {"
    "      width: 100%;"
    "      max-width: 500px;"
    "      margin: 30px auto;"
    "      padding: 0;"
    "      background: transparent;"
    "    }"
    "    .card {"
    "      background: white;"
    "      border-radius: 16px;"
    "      box-shadow: var(--shadow-md);"
    "      overflow: hidden;"
    "      transition: var(--transition);"
    "    }"
    "    .card-header {"
    "      background-color: var(--primary-color);"
    "      color: white;"
    "      padding: 20px 24px;"
    "      text-align: center;"
    "      position: relative;"
    "    }"
    "    .card-title {"
    "      margin: 0;"
    "      font-size: 1.5rem;"
    "      font-weight: 500;"
    "      display: flex;"
    "      align-items: center;"
    "      justify-content: center;"
    "      gap: 10px;"
    "    }"
    "    .card-title i {"
    "      font-size: 1.6rem;"
    "    }"
    "    .card-body {"
    "      padding: 30px 24px;"
    "      display: flex;"
    "      flex-direction: column;"
    "      gap: 25px;"
    "      align-items: center;"
    "    }"
    "    .door-status {"
    "      background-color: var(--grey-light);"
    "      padding: 20px;"
    "      border-radius: 12px;"
    "      width: 100%;"
    "      display: flex;"
    "      align-items: center;"
    "      justify-content: center;"
    "      gap: 12px;"
    "      font-size: 18px;"
    "      transition: var(--transition);"
    "    }"
    "    .door-status i {"
    "      font-size: 24px;"
    "    }"
    "    .status-text {"
    "      font-weight: 500;"
    "    }"
    "    .status-open {"
    "      color: var(--success-color);"
    "    }"
    "    .status-open-bg {"
    "      background-color: var(--success-light);"
    "    }"
    "    .status-closed {"
    "      color: var(--dark-color);"
    "    }"
    "    .door-btn {"
    "      background-color: var(--primary-color);"
    "      color: white;"
    "      padding: 16px 30px;"
    "      font-size: 18px;"
    "      font-weight: 500;"
    "      border: none;"
    "      border-radius: 12px;"
    "      cursor: pointer;"
    "      width: 100%;"
    "      max-width: 280px;"
    "      transition: var(--transition);"
    "      display: flex;"
    "      align-items: center;"
    "      justify-content: center;"
    "      gap: 12px;"
    "      margin: 10px auto 0;"
    "      box-shadow: var(--shadow-sm);"
    "    }"
    "    .door-btn:hover {"
    "      background-color: var(--primary-dark);"
    "      transform: translateY(-3px);"
    "      box-shadow: var(--shadow-md);"
    "    }"
    "    .door-btn:active {"
    "      transform: translateY(0);"
    "    }"
    "    .door-btn:disabled {"
    "      background-color: var(--grey-color);"
    "      cursor: not-allowed;"
    "      transform: none;"
    "      box-shadow: var(--shadow-sm);"
    "      opacity: 0.8;"
    "    }"
    "    .door-btn i {"
    "      font-size: 20px;"
    "    }"
    "    .pulse {"
    "      animation: pulse 1.5s infinite;"
    "    }"
    "    .status-icon {"
    "      width: 46px;"
    "      height: 46px;"
    "      border-radius: 50%;"
    "      display: flex;"
    "      align-items: center;"
    "      justify-content: center;"
    "    }"
    "    .status-icon-open {"
    "      background-color: var(--success-light);"
    "      color: var(--success-color);"
    "    }"
    "    .status-icon-closed {"
    "      background-color: var(--grey-light);"
    "      color: var(--dark-color);"
    "    }"
    "    @keyframes pulse {"
    "      0% { opacity: 0.7; }"
    "      50% { opacity: 1; }"
    "      100% { opacity: 0.7; }"
    "    }"
    "    .footer {"
    "      text-align: center;"
    "      font-size: 14px;"
    "      color: #888;"
    "      margin-top: 30px;"
    "      padding: 20px;"
    "    }"
    "    @media (max-width: 600px) {"
    "      .container {"
    "        margin: 15px;"
    "      }"
    "      .page-content {"
    "        padding: 0;"
    "      }"
    "      .navbar {"
    "        padding: 12px 16px;"
    "      }"
    "      .card-body {"
    "        padding: 24px 16px;"
    "      }"
    "      .navbar .user {"
    "        gap: 6px;"
    "      }"
    "    }"
    "  </style>"
    "</head>"
    "<body>";
  
  // Navbar với thông tin người dùng
  html += "<div class='navbar'>"
    "  <h1><i class='fas fa-shield-alt'></i> Smart Door System</h1>"
    "  <div class='user'>"
    "    <a href='/wifi-setup' class='wifi-btn'>"
    "      <i class='fas fa-wifi'></i>"
    "      <span>WiFi</span>"
    "    </a>"
    "    <div class='user-info'>"
    "      <div class='user-icon'>" + username.substring(0, 1) + "</div>"
    "      <span class='username'>" + username + "</span>"
    "    </div>"
    "    <button onclick=\"window.location.href='/'\" class='logout-btn'>"
    "      <i class='fas fa-sign-out-alt'></i> Đăng xuất"
    "    </button>"
    "  </div>"
    "</div>";
  
  // Container chính
  html += "<div class='page-content'>"
    "  <div class='container'>"
    "    <div class='card'>"
    "      <div class='card-header'>"
    "        <h2 class='card-title'><i class='fas fa-door-open'></i> Điều khiển cửa thông minh</h2>"
    "      </div>"
    "      <div class='card-body'>"
    "        <div class='door-status' id='doorStatus'>"
    "          <div class='status-icon'>"
    "            <i class='fas fa-spinner fa-spin'></i>"
    "          </div>"
    "          <span class='status-text'>Đang tải trạng thái cửa...</span>"
    "        </div>"
    "        <button id='doorBtn' class='door-btn' onclick='toggleDoor()'>"
    "          <i class='fas fa-unlock'></i> Mở cửa"
    "        </button>"
    "      </div>"
    "    </div>"
    "    <div class='footer'>"
    "      <p>© 2025 Smart Door System. Bản quyền thuộc về tác giả.</p>"
    "    </div>"
    "  </div>"
    "</div>";
  
  html += "<script>"
    "    let doorOpen = false;"
    "    const username = \"" + username + "\";"
    "    "
    "window.onload = function() {"
    "  updateDoorStatus();"
    "  checkArduinoStatus();"
    "  loadUsers();"
    "  loadLogs();"
    "  setInterval(updateDoorStatus, 5000);"
    "  setInterval(checkArduinoStatus, 10000);"
    "  setInterval(loadLogs, 10000);"
    "};"
    "    function updateDoorStatus() {"
    "      fetch('/api/door-status')"
    "        .then(response => response.json())"
    "        .then(data => {"
    "          doorOpen = data.isOpen;"
    "          const statusEl = document.getElementById('doorStatus');"
    "          const btnEl = document.getElementById('doorBtn');"
    "          "
    "          if (doorOpen) {"
    "            statusEl.innerHTML = '<div class=\"status-icon status-icon-open\">"
    "                                  <i class=\"fas fa-door-open status-open\"></i></div>"
    "                                <span class=\"status-text status-open\">Trạng thái: Đang mở</span>';"
    "            statusEl.classList.add('pulse', 'status-open-bg');"
    "            btnEl.innerHTML = '<i class=\"fas fa-lock\"></i> Đóng cửa';"
    "            btnEl.disabled = true;"
    "          } else {"
    "            statusEl.innerHTML = '<div class=\"status-icon status-icon-closed\">"
    "                                  <i class=\"fas fa-door-closed status-closed\"></i></div>"
    "                                <span class=\"status-text status-closed\">Trạng thái: Đã đóng</span>';"
    "            statusEl.classList.remove('pulse', 'status-open-bg');"
    "            btnEl.innerHTML = '<i class=\"fas fa-unlock\"></i> Mở cửa';"
    "            btnEl.disabled = false;"
    "          }"
    "        })"
    "        .catch(error => {"
    "          console.error('Lỗi khi cập nhật trạng thái:', error);"
    "          const statusEl = document.getElementById('doorStatus');"
    "          statusEl.innerHTML = '<div class=\"status-icon\">"
    "                              <i class=\"fas fa-exclamation-triangle\" style=\"color:#f39c12\"></i></div>"
    "                            <span class=\"status-text\" style=\"color:#f39c12\">Lỗi kết nối</span>';"
    "        });"
    "    }"
    "    "
    "    function toggleDoor() {"
    "      const btnEl = document.getElementById('doorBtn');"
    "      btnEl.disabled = true;"
    "      btnEl.innerHTML = '<i class=\"fas fa-spinner fa-spin\"></i> Đang xử lý...';"
    "      "
    "      fetch('/api/open-door', { "
    "        method: 'POST',"
    "        headers: { 'Content-Type': 'application/json' },"
    "        body: JSON.stringify({ user: username })"
    "      })"
    "      .then(response => response.json())"
    "      .then(data => {"
    "        if (data.success) {"
    "          updateDoorStatus();"
    "        } else {"
    "          alert('Lỗi: ' + data.message);"
    "          btnEl.disabled = false;"
    "          btnEl.innerHTML = '<i class=\"fas fa-unlock\"></i> Mở cửa';"
    "        }"
    "      })"
    "      .catch(error => {"
    "        console.error('Lỗi khi gửi lệnh:', error);"
    "        alert('Có lỗi xảy ra khi kết nối với hệ thống');"
    "        btnEl.disabled = false;"
    "        btnEl.innerHTML = '<i class=\"fas fa-unlock\"></i> Mở cửa';"
    "      });"
    "    }"
    
    "  </script>"
    "</body>"
    "</html>";
  
  server.send(200, "text/html", html);
}

void handleDoorStatus() {
  // Lay trang thai cua tu Arduino
  String response = sendCommandToArduino("STATUS");
  
  if(response.startsWith("STATUS:")) {
    doorIsOpen = (response.substring(7) == "OPEN");
  }
  
  String json = "{\"isOpen\":" + String(doorIsOpen ? "true" : "false") + "}";
  server.send(200, "application/json", json);
}
void handleOpenDoor() {
  // Parse data from client
  String user = "web_user";
  if (server.hasArg("plain")) {
    DynamicJsonDocument doc(200);
    DeserializationError error = deserializeJson(doc, server.arg("plain"));
    
    if (!error && doc.containsKey("user")) {
      user = doc["user"].as<String>();
    }
  }
  
  // First check Arduino connection with longer timeout
  Serial.println("Checking Arduino connection before door operation...");
  String pingResponse = sendCommandToArduino("PING", 3000); // Tăng lên 3 giây
  Serial.println("Ping response: [" + pingResponse + "]");
  
  if (pingResponse != "OK") {
    // Try one more time with even longer timeout
    Serial.println("Retrying ping with longer timeout...");
    pingResponse = sendCommandToArduino("PING", 5000);
    Serial.println("Second ping response: [" + pingResponse + "]");
    
    if (pingResponse != "OK") {
      server.send(200, "application/json", "{\"success\":false,\"message\":\"Cannot reach door controller (Arduino)\"}");
      return;
    }
  }
  
  // Rest of the function remains the same...
}



void handleGetUsers() {
  String json = "[";
  
  for(int i = 0; i < userCount; i++) {
    if(i > 0) json += ",";
    json += "{\"username\":\"" + users[i].username + "\",";
    json += "\"isAdmin\":" + String(users[i].isAdmin ? "true" : "false") + "}";
  }
  
  json += "]";
  server.send(200, "application/json", json);
}

void handleAddUser() {
  if(server.hasArg("plain")) {
    DynamicJsonDocument doc(512);
    DeserializationError error = deserializeJson(doc, server.arg("plain"));
    
    if(!error) {
      String username = doc["username"];
      String password = doc["password"];
      bool isAdmin = doc["isAdmin"];
      
      if(username.length() == 0 || password.length() == 0) {
        server.send(200, "application/json", "{\"success\":false,\"message\":\"Du lieu khong hop le\"}");
        return;
      }
      
      if(addUser(username, password, isAdmin)) {
        server.send(200, "application/json", "{\"success\":true}");
      } else {
        server.send(200, "application/json", "{\"success\":false,\"message\":\"Nguoi dung da ton tai hoac vuot qua gioi han\"}");
      }
    } else {
      server.send(400, "application/json", "{\"success\":false,\"message\":\"Du lieu JSON khong hop le\"}");
    }
  } else {
    server.send(400, "application/json", "{\"success\":false,\"message\":\"Khong co du lieu\"}");
  }
}

void handleDeleteUser() {
  if(server.hasArg("plain")) {
    DynamicJsonDocument doc(200);
    DeserializationError error = deserializeJson(doc, server.arg("plain"));
    
    if(!error) {
      String username = doc["username"];
      
      if(username.length() == 0) {
        server.send(200, "application/json", "{\"success\":false,\"message\":\"Du lieu khong hop le\"}");
        return;
      }
      
      if(deleteUser(username)) {
        server.send(200, "application/json", "{\"success\":true}");
      } else {
        server.send(200, "application/json", "{\"success\":false,\"message\":\"Khong the xoa nguoi dung hoac khong tim thay\"}");
      }
    } else {
      server.send(400, "application/json", "{\"success\":false,\"message\":\"Du lieu JSON khong hop le\"}");
    }
  } else {
    server.send(400, "application/json", "{\"success\":false,\"message\":\"Khong co du lieu\"}");
  }
}

void handleGetLogs() {
  preferences.begin("doorlogs", true); // Read-only mode
  int logCount = preferences.getInt("logCount", 0);
  
  // Debug information
  Serial.print("Total logs: ");
  Serial.println(logCount);
  
  String json = "[";
  bool firstItem = true;
  
  // Get most recent logs first (up to 20)
  int startIdx = (logCount == 0) ? 49 : (logCount - 1); // Handle empty case
  int endIdx = startIdx - 20;
  if (endIdx < 0) endIdx = -1;
  
  for (int i = startIdx; i > endIdx; i--) {
    int idx = i % 50; // Handle circular buffer wraparound
    String logKey = "log" + String(idx);
    String logValue = preferences.getString(logKey.c_str(), "");
    
    // Debug
    Serial.print("Reading log ");
    Serial.print(logKey);
    Serial.print(": ");
    Serial.println(logValue);
    
    if (logValue.length() > 0) {
      // Process log entry
      int delimPos1 = logValue.indexOf('|');
      int delimPos2 = logValue.indexOf('|', delimPos1 + 1);
      
      if (delimPos1 > 0 && delimPos2 > delimPos1) {
        if (!firstItem) json += ",";
        
        // Parse log components
        String time = logValue.substring(0, delimPos1);
        String user = logValue.substring(delimPos1 + 1, delimPos2);
        String action = logValue.substring(delimPos2 + 1);
        
        // Format for display
        json += "{\"time\":\"" + formatTimeFromMillis(time.toInt()) + "\",";
        json += "\"user\":\"" + user + "\",";
        json += "\"action\":\"" + action + "\"}";
        
        firstItem = false;
      }
    }
  }
  
  json += "]";
  preferences.end();
  
  server.send(200, "application/json", json);
}

// Helper function to format time
String formatTimeFromMillis(unsigned long ms) {
  unsigned long seconds = ms / 1000;
  unsigned long minutes = seconds / 60;
  unsigned long hours = minutes / 60;
  
  // Format as HH:MM:SS
  return String(hours % 24) + ":" + 
         (minutes % 60 < 10 ? "0" : "") + String(minutes % 60) + ":" + 
         (seconds % 60 < 10 ? "0" : "") + String(seconds % 60);
}

// Handler trang cấu hình WiFi
void handleWiFiSetup() {
  // Trang cấu hình WiFi
  String html = "<!DOCTYPE html>"
    "<html lang=\"vi\">"
    "<head>"
    "  <meta charset=\"UTF-8\">"
    "  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">"
    "  <title>Cấu Hình WiFi</title>"
    "  <link rel=\"stylesheet\" href=\"https://cdnjs.cloudflare.com/ajax/libs/font-awesome/6.0.0-beta3/css/all.min.css\">"
    "  <style>"
    "    :root {"
    "      --primary: #4361ee;"
    "      --success: #2ecc71;"
    "      --warning: #f39c12;"
    "      --danger: #e74c3c;"
    "      --light: #f8f9fa;"
    "      --dark: #343a40;"
    "      --border-radius: 8px;"
    "      --shadow: 0 4px 6px rgba(0, 0, 0, 0.1);"
    "    }"
    "    * {"
    "      margin: 0;"
    "      padding: 0;"
    "      box-sizing: border-box;"
    "    }"
    "    body {"
    "      font-family: 'Segoe UI', Roboto, 'Helvetica Neue', sans-serif;"
    "      background-color: #f0f2f5;"
    "      color: var(--dark);"
    "      line-height: 1.6;"
    "      padding: 20px;"
    "    }"
    "    .container {"
    "      max-width: 600px;"
    "      margin: 0 auto;"
    "      padding: 20px;"
    "    }"
    "    .header {"
    "      text-align: center;"
    "      margin-bottom: 30px;"
    "    }"
    "    .header h1 {"
    "      font-size: 28px;"
    "      margin-bottom: 10px;"
    "      color: var(--primary);"
    "    }"
    "    .panel {"
    "      background-color: white;"
    "      border-radius: var(--border-radius);"
    "      box-shadow: var(--shadow);"
    "      padding: 25px;"
    "      margin-bottom: 20px;"
    "    }"
    "    .panel h2 {"
    "      font-size: 20px;"
    "      margin-bottom: 20px;"
    "      padding-bottom: 10px;"
    "      border-bottom: 1px solid #eee;"
    "      display: flex;"
    "      align-items: center;"
    "    }"
    "    .panel h2 i {"
    "      margin-right: 10px;"
    "      color: var(--primary);"
    "    }"
    "    .form-group {"
    "      margin-bottom: 20px;"
    "    }"
    "    .form-group label {"
    "      display: block;"
    "      margin-bottom: 8px;"
    "      font-weight: 500;"
    "    }"
    "    input, select {"
    "      width: 100%;"
    "      padding: 12px;"
    "      border: 1px solid #ddd;"
    "      border-radius: var(--border-radius);"
    "      font-size: 16px;"
    "    }"
    "    input:focus, select:focus {"
    "      outline: none;"
    "      border-color: var(--primary);"
    "      box-shadow: 0 0 0 3px rgba(67, 97, 238, 0.2);"
    "    }"
    "    button {"
    "      background-color: var(--primary);"
    "      color: white;"
    "      border: none;"
    "      padding: 12px 20px;"
    "      border-radius: var(--border-radius);"
    "      cursor: pointer;"
    "      font-size: 16px;"
    "      font-weight: 500;"
    "      display: inline-flex;"
    "      align-items: center;"
    "      justify-content: center;"
    "      transition: all 0.2s ease;"
    "    }"
    "    button i {"
    "      margin-right: 8px;"
    "    }"
    "    button:hover {"
    "      opacity: 0.9;"
    "      transform: translateY(-2px);"
    "    }"
    "    button:active {"
    "      transform: translateY(0);"
    "    }"
    "    button.secondary {"
    "      background-color: #6c757d;"
    "    }"
    "    button.refresh {"
    "      background-color: var(--success);"
    "    }"
    "    button.danger {"
    "      background-color: var(--danger);"
    "    }"
    "    .wifi-list {"
    "      margin-top: 15px;"
    "      max-height: 300px;"
    "      overflow-y: auto;"
    "      border: 1px solid #ddd;"
    "      border-radius: var(--border-radius);"
    "    }"
    "    .wifi-item {"
    "      padding: 12px 15px;"
    "      border-bottom: 1px solid #eee;"
    "      cursor: pointer;"
    "      display: flex;"
    "      align-items: center;"
    "      justify-content: space-between;"
    "    }"
    "    .wifi-item:last-child {"
    "      border-bottom: none;"
    "    }"
    "    .wifi-item:hover {"
    "      background-color: var(--light);"
    "    }"
    "    .wifi-name {"
    "      font-weight: 500;"
    "    }"
    "    .wifi-signal {"
    "      color: #6c757d;"
    "      font-size: 14px;"
    "    }"
    "    .wifi-secure i {"
    "      color: var(--success);"
    "    }"
    "    .wifi-open i {"
    "      color: var(--warning);"
    "    }"
    "    .button-group {"
    "      display: flex;"
    "      gap: 10px;"
    "      margin-top: 20px;"
    "    }"
    "    .status-badge {"
    "      display: inline-block;"
    "      padding: 5px 10px;"
    "      border-radius: 20px;"
    "      font-size: 14px;"
    "      margin-top: 10px;"
    "    }"
    "    .status-connected {"
    "      background-color: #d4edda;"
    "      color: #155724;"
    "    }"
    "    .status-ap {"
    "      background-color: #fff3cd;"
    "      color: #856404;"
    "    }"
    "    .loader {"
    "      display: inline-block;"
    "      width: 20px;"
    "      height: 20px;"
    "      border: 3px solid rgba(0, 0, 0, 0.1);"
    "      border-radius: 50%;"
    "      border-top-color: var(--primary);"
    "      animation: spin 1s ease-in-out infinite;"
    "      margin-right: 10px;"
    "    }"
    "    @keyframes spin {"
    "      to { transform: rotate(360deg); }"
    "    }"
    "    .hidden {"
    "      display: none;"
    "    }"
    "    .info-box {"
    "      background-color: #e3f2fd;"
    "      border-left: 4px solid var(--primary);"
    "      padding: 15px;"
    "      margin-bottom: 20px;"
    "      border-radius: 4px;"
    "    }"
    "    .info-box p {"
    "      margin: 0;"
    "      color: #0c5460;"
    "    }"
    "    @media (max-width: 768px) {"
    "      .container {"
    "        padding: 10px;"
    "      }"
    "      .button-group {"
    "        flex-direction: column;"
    "      }"
    "      button {"
    "        width: 100%;"
    "      }"
    "    }"
    "  </style>"
    "</head>"
    "<body>"
    "  <div class=\"container\">"
    "    <div class=\"header\">"
    "      <h1><i class=\"fas fa-wifi\"></i> Cấu Hình Kết Nối WiFi</h1>";

  // Hiển thị thông tin trạng thái kết nối
  if(APMode) {
    html += "<div class=\"status-badge status-ap\"><i class=\"fas fa-broadcast-tower\"></i> Đang ở chế độ phát WiFi (AP Mode)</div>";
  } else {
    html += "<div class=\"status-badge status-connected\"><i class=\"fas fa-check-circle\"></i> Đã kết nối WiFi: " + String(WiFi.SSID()) + "</div>";
  }
  
  html += "    </div>"
    "    <div class=\"info-box\">"
    "      <p><i class=\"fas fa-info-circle\"></i> Cấu hình WiFi để kết nối thiết bị với mạng Internet. Sau khi cấu hình thành công, thiết bị sẽ khởi động lại.</p>"
    "    </div>"
    "    <div class=\"panel\">"
    "      <h2><i class=\"fas fa-search\"></i> Quét Tìm Mạng WiFi</h2>"
    "      <button id=\"scanBtn\" class=\"refresh\" onclick=\"scanWiFi()\"><i class=\"fas fa-sync-alt\"></i> Quét Mạng WiFi</button>"
    "      <div id=\"scanLoader\" class=\"hidden\">"
    "        <div class=\"loader\"></div> Đang quét mạng..."
    "      </div>"
    "      <div id=\"wifiList\" class=\"wifi-list hidden\"></div>"
    "    </div>"
    "    <div class=\"panel\">"
    "      <h2><i class=\"fas fa-cog\"></i> Cấu Hình Kết Nối</h2>"
    "      <div class=\"form-group\">"
    "        <label for=\"ssid\">Tên mạng WiFi (SSID)</label>"
    "        <input type=\"text\" id=\"ssid\" placeholder=\"Nhập tên mạng WiFi\">"
    "      </div>"
    "      <div class=\"form-group\">"
    "        <label for=\"password\">Mật khẩu WiFi</label>"
    "        <input type=\"password\" id=\"password\" placeholder=\"Nhập mật khẩu WiFi\">"
    "      </div>"
    "      <div class=\"button-group\">"
    "        <button id=\"saveBtn\" onclick=\"saveWiFi()\"><i class=\"fas fa-save\"></i> Lưu & Kết Nối</button>"
    "        <button class=\"secondary\" onclick=\"window.location.href='/'\"><i class=\"fas fa-arrow-left\"></i> Quay Lại</button>";

  // Chỉ hiển thị nút reset nếu đã có cấu hình WiFi
  if(!APMode) {
    html += "        <button class=\"danger\" onclick=\"resetWiFi()\"><i class=\"fas fa-trash\"></i> Xóa Cấu Hình</button>";
  }
  
  html += "      </div>"
    "    </div>"
    "  </div>"
    "  <script>"
    "    function scanWiFi() {"
    "      document.getElementById('scanBtn').disabled = true;"
    "      document.getElementById('scanLoader').classList.remove('hidden');"
    "      document.getElementById('wifiList').classList.add('hidden');"
    "      "
    "      fetch('/api/scan-wifi')"
    "        .then(response => response.json())"
    "        .then(data => {"
    "          const wifiList = document.getElementById('wifiList');"
    "          wifiList.innerHTML = '';"
    "          "
    "          if(data.length === 0) {"
    "            wifiList.innerHTML = '<div class=\"wifi-item\">Không tìm thấy mạng WiFi nào</div>';"
    "          } else {"
    "            data.forEach(network => {"
    "              const wifiItem = document.createElement('div');"
    "              wifiItem.className = 'wifi-item';"
    "              wifiItem.onclick = function() { selectWiFi(network.ssid); };"
    "              "
    "              const signalStrength = Math.min(Math.max(2 * (network.rssi + 100), 0), 100);"
    "              let signalIcon = 'fa-wifi';"
    "              if(signalStrength < 30) signalIcon = 'fa-signal-weak';"
    "              else if(signalStrength < 70) signalIcon = 'fa-signal';"
    "              "
    "              wifiItem.innerHTML = `"
    "                <div class=\"wifi-name\">${network.ssid}</div>"
    "                <div class=\"wifi-info\">"
    "                  <span class=\"wifi-signal\"><i class=\"fas ${signalIcon}\"></i> ${signalStrength}%</span>"
    "                  <span class=\"${network.secure ? 'wifi-secure' : 'wifi-open'}\"><i class=\"fas ${network.secure ? 'fa-lock' : 'fa-lock-open'}\"></i></span>"
    "                </div>"
    "              `;"
    "              wifiList.appendChild(wifiItem);"
    "            });"
    "          }"
    "          "
    "          wifiList.classList.remove('hidden');"
    "          document.getElementById('scanLoader').classList.add('hidden');"
    "          document.getElementById('scanBtn').disabled = false;"
    "        })"
    "        .catch(error => {"
    "          console.error('Error scanning WiFi:', error);"
    "          document.getElementById('wifiList').innerHTML = '<div class=\"wifi-item\">Lỗi khi quét mạng WiFi</div>';"
    "          document.getElementById('wifiList').classList.remove('hidden');"
    "          document.getElementById('scanLoader').classList.add('hidden');"
    "          document.getElementById('scanBtn').disabled = false;"
    "        });"
    "    }"
    "    "
    "    function selectWiFi(ssid) {"
    "      document.getElementById('ssid').value = ssid;"
    "      document.getElementById('password').focus();"
    "    }"
    "    "
    "    function saveWiFi() {"
    "      const ssid = document.getElementById('ssid').value;"
    "      const password = document.getElementById('password').value;"
    "      "
    "      if(!ssid) {"
    "        alert('Vui lòng nhập tên mạng WiFi');"
    "        return;"
    "      }"
    "      "
    "      document.getElementById('saveBtn').disabled = true;"
    "      document.getElementById('saveBtn').innerHTML = '<div class=\"loader\"></div> Đang lưu...';"
    "      "
    "      fetch('/api/save-wifi', {"
    "        method: 'POST',"
    "        headers: { 'Content-Type': 'application/json' },"
    "        body: JSON.stringify({ ssid, password })"
    "      })"
    "      .then(response => response.json())"
    "      .then(data => {"
    "        if(data.success) {"
    "          alert('Cấu hình WiFi đã được lưu. Thiết bị sẽ khởi động lại để áp dụng cấu hình mới.');"
    "          setTimeout(() => { window.location.href = '/'; }, 5000);"
    "        } else {"
    "          alert('Lỗi: ' + data.message);"
    "          document.getElementById('saveBtn').disabled = false;"
    "          document.getElementById('saveBtn').innerHTML = '<i class=\"fas fa-save\"></i> Lưu & Kết Nối';"
    "        }"
    "      })"
    "      .catch(error => {"
    "        console.error('Error saving WiFi config:', error);"
    "        alert('Có lỗi xảy ra khi lưu cấu hình');"
    "        document.getElementById('saveBtn').disabled = false;"
    "        document.getElementById('saveBtn').innerHTML = '<i class=\"fas fa-save\"></i> Lưu & Kết Nối';"
    "      });"
    "    }"
    "    "
    "    function resetWiFi() {"
    "      if(confirm('Bạn có chắc chắn muốn xóa cấu hình WiFi hiện tại? Thiết bị sẽ chuyển sang chế độ phát WiFi.')) {"
    "        fetch('/api/reset-wifi', { method: 'POST' })"
    "          .then(response => response.json())"
    "          .then(data => {"
    "            if(data.success) {"
    "              alert('Đã xóa cấu hình WiFi. Thiết bị sẽ khởi động lại.');"
    "              setTimeout(() => { window.location.href = '/'; }, 5000);"
    "            } else {"
    "              alert('Lỗi: ' + data.message);"
    "            }"
    "          })"
    "          .catch(error => {"
    "            console.error('Error resetting WiFi config:', error);"
    "            alert('Có lỗi xảy ra khi xóa cấu hình');"
    "          });"
    "      }"
    "    }"
    "    "
    "    // Quét WiFi tự động khi tải trang"
    "    window.onload = function() {"
    "      scanWiFi();"
    "    };"
    "  </script>"
    "</body>"
    "</html>";
  
  server.send(200, "text/html", html);
}
// Handler for scanning WiFi networks
void handleWiFiScan() {
  Serial.println("Scanning WiFi networks...");
  
  // Scan for networks
  int networkCount = WiFi.scanNetworks();
  Serial.println("Scan completed, found " + String(networkCount) + " networks");
  
  // Create JSON response
  String json = "[";
  for(int i = 0; i < networkCount; i++) {
    if(i > 0) json += ",";
    
    // Format: { "ssid": "Network name", "rssi": -50, "secure": true }
    json += "{\"ssid\":\"" + WiFi.SSID(i) + "\",";
    json += "\"rssi\":" + String(WiFi.RSSI(i)) + ",";
    json += "\"secure\":" + String(WiFi.encryptionType(i) != WIFI_AUTH_OPEN ? "true" : "false") + "}";
  }
  json += "]";
  
  server.send(200, "application/json", json);
  
  // Clean up scan results
  WiFi.scanDelete();
}

// Handler for saving WiFi configuration
void handleSaveWiFi() {
  if(server.hasArg("plain")) {
    DynamicJsonDocument doc(512);
    DeserializationError error = deserializeJson(doc, server.arg("plain"));
    
    if(!error) {
      String ssid = doc["ssid"];
      String password = doc["password"];
      
      if(ssid.length() == 0) {
        server.send(200, "application/json", "{\"success\":false,\"message\":\"SSID không được để trống\"}");
        return;
      }
      
      // Lưu cấu hình WiFi mới
      saveWiFiConfig(ssid, password);
      
      // Gửi phản hồi thành công
      server.send(200, "application/json", "{\"success\":true,\"message\":\"Cấu hình WiFi đã được lưu. Thiết bị sẽ khởi động lại.\"}");
      
      // Lên lịch khởi động lại sau 2 giây để đảm bảo phản hồi HTTP được gửi
      delay(2000);
      
      // Thử kết nối với mạng WiFi mới
      if (connectToWiFi(ssid, password, 10)) {
        Serial.println("Kết nối thành công với mạng mới: " + ssid);
      } else {
        Serial.println("Không thể kết nối với mạng mới. Chuyển sang chế độ AP.");
        setupAP(); // Quay lại chế độ AP nếu không thể kết nối
      }
      
      // Khởi động lại ESP32
      ESP.restart();
    } else {
      server.send(400, "application/json", "{\"success\":false,\"message\":\"Dữ liệu JSON không hợp lệ\"}");
    }
  } else {
    server.send(400, "application/json", "{\"success\":false,\"message\":\"Không có dữ liệu\"}");
  }
}

// Handler for resetting WiFi configuration
void handleResetWiFi() {
  // Xóa cấu hình WiFi
  clearWiFiConfig();
  
  // Gửi phản hồi thành công
  server.send(200, "application/json", "{\"success\":true,\"message\":\"Đã xóa cấu hình WiFi. Thiết bị sẽ khởi động lại.\"}");
  
  // Đợi để đảm bảo phản hồi HTTP được gửi
  delay(1000);
  
  // Chuyển sang chế độ AP
  setupAP();
  
  // Khởi động lại ESP32
  ESP.restart();
}
void handleArduinoStatus() {
  bool connected = checkArduinoConnection();
  String json = "{\"connected\":" + String(connected ? "true" : "false") + "}";
  server.send(200, "application/json", json);
}
void processRFID() {
  // Kiểm tra thẻ mới
  if (!mfrc522.PICC_IsNewCardPresent()) return;
  if (!mfrc522.PICC_ReadCardSerial()) return;
  
  // Đọc UID của thẻ - Thêm dòng này để khai báo biến uidStr
  String uidStr = getUIDFromRFIDTag();
  
  Serial.println("Phát hiện thẻ RFID: " + uidStr);
  // Trong processRFID() của ESP32
Serial.println("Checking RFID: Present=" + 
               String(mfrc522.PICC_IsNewCardPresent()) + 
               ", Read=" + 
               String(mfrc522.PICC_ReadCardSerial()));

  Serial.println("Processing RFID in mode: " + 
               String(adminCardMode ? "ADMIN" : "NORMAL"));
  // Nếu đang ở chế độ admin, gửi thẻ cho Arduino để thêm
  if (adminCardMode) {
    Serial2.println("RFID:" + uidStr);
    Serial.println("Đã gửi UID thẻ mới cho Arduino: " + uidStr);
    return;
  }
  
  // Xử lý thẻ RFID thông thường
  Serial2.println("RFID:" + uidStr);
  
  // Kết thúc thẻ hiện tại
  mfrc522.PICC_HaltA();
  mfrc522.PCD_StopCrypto1();
  
  delay(50);
}


String getUIDFromRFIDTag() {
  String uidString = "";
  for (byte i = 0; i < mfrc522.uid.size; i++) {
    if (mfrc522.uid.uidByte[i] < 0x10) {
      uidString += "0";
    }
    uidString += String(mfrc522.uid.uidByte[i], HEX);
  }
  uidString.toUpperCase();
  return uidString;
}

