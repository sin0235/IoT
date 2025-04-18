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
#include <Update.h>

// ===== CAU HINH WIFI =====
const char* ssid = "Wi-MESH 2.4G";     
const char* password = "25032005"; 

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
String sendCommandToArduino(String command, unsigned long timeout = COMMAND_TIMEOUT);
void checkArduinoResponse();
bool checkArduinoConnection();
void loadUsers();
void saveUsers();
bool authenticateUser(String username, String password);
bool isAdmin(String username);
bool addUser(String username, String password, bool isAdmin);
bool deleteUser(String username);
void connectToWiFi();
void setupServerRoutes();
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

// ===== KHOI TAO HE THONG =====
void setup() {
  // Khởi tạo cổng Serial để debug
  Serial.begin(115200);
  Serial.println("\n\n=== He Thong Quan Ly Cua Thong Minh ===");
  
  // Khởi tạo cổng Serial2 để giao tiếp với Arduino
  Serial2.begin(ARDUINO_BAUD_RATE, SERIAL_8N1, 16, 17);  // RX=GPIO16, TX=GPIO17
  Serial.println("Da khoi tao ket noi voi Arduino qua Serial2");
  
  // Khởi tạo SPIFFS
  if(!SPIFFS.begin(true)) {
    Serial.println("Loi khoi tao SPIFFS");
  } else {
    Serial.println("SPIFFS da khoi tao thanh cong");
  }
  
  // Ket nối WiFi
  connectToWiFi();
  
  // Thiết lập mDNS
  if(MDNS.begin("smartdoor")) {
    Serial.println("mDNS da khoi dong - Truy cap qua http://smartdoor.local");
  } else {
    Serial.println("Khong the khoi dong mDNS");
  }
  
  // Tải danh sách người dùng
  loadUsers();
  Serial.println("Da tai danh sach nguoi dung");
  
  // Thiết lập các routes cho web server
  setupServerRoutes();
  Serial.println("Da thiet lap cac routes cho web server");
  
  // Khởi động server
  server.begin();
  Serial.println("May chu HTTP da khoi dong");
  
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
  int maxAttempts = 5;
  
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
  
  // Khởi tạo bảng điều khiển cửa
  Serial.println("He thong Quan ly Cua Thong Minh da san sang!");
  Serial.println("Su dung trinh duyet de truy cap: http://" + WiFi.localIP().toString());
}

// ===== VONG LAP CHINH =====
void loop() {
  // Xử lý các yêu cầu HTTP
  server.handleClient();
  
  // Đọc dữ liệu từ Arduino nếu có
  if(Serial2.available()) {
    checkArduinoResponse();
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
      
      // Đây là nơi bạn có thể lưu log vào bộ nhớ nếu cần
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
  
  // Giảm tải CPU
  delay(10);
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
  
  int logCount = preferences.getInt("logCount", 0);
  if(logCount >= 50) logCount = 0; // Giới hạn 50 bản ghi
  
  // Tạo timestamp
  String timeStr = String(millis());
  
  // Lưu log
  String logKey = "log" + String(logCount);
  String logValue = timeStr + "|" + method + "|" + "Mo cua";
  preferences.putString(logKey.c_str(), logValue.c_str());
  preferences.putInt("logCount", logCount + 1);
  
  preferences.end();
}


// ===== KET NOI WIFI =====
void connectToWiFi() {
  Serial.print("Dang ket noi WiFi...");
  WiFi.begin(ssid);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if(WiFi.status() == WL_CONNECTED) {
    Serial.println("\nDa ket noi WiFi");
    Serial.println("Dia chi IP: " + WiFi.localIP().toString());


  } else {
    Serial.println("\nKhong the ket noi WiFi");
    Serial.println(ssid);
    Serial.println(password);
    // Van tiep tuc hoat dong ngay ca khi khong co WiFi
  }
}

// ===== GIAO TIEP VOI ARDUINO =====
bool checkArduinoConnection() {
  return sendCommandToArduino("PING", 1000).startsWith("OK");
}

// Gui lenh den Arduino va doi phan hoi
String sendCommandToArduino(String command, unsigned long timeout) {
  // Reset các biến trạng thái
  lastResponse = "";
  responseReceived = false;
  
  // Xóa bộ đệm Serial2 trước khi gửi lệnh
  while (Serial2.available()) {
    Serial2.read();
  }
  
  // Gửi lệnh
  Serial.println("Gui den Arduino: " + command);
  Serial2.println(command);
  
  // Đợi phản hồi với timeout
  unsigned long startTime = millis();
  while (!responseReceived && (millis() - startTime < timeout)) {
    checkArduinoResponse();
    delay(10);
  }
  
  if (responseReceived) {
    return lastResponse;
  } else {
    Serial.println("Timeout khi gửi lệnh: " + command);
    return "TIMEOUT";
  }
}


// Kiem tra phan hoi tu Arduino
void checkArduinoResponse() {
  while (Serial2.available()) {
    char c = Serial2.read();
    serialBuffer += c;
    
    // Kiểm tra xem có dòng hoàn chỉnh không
    if (c == '\n') {
      int newlinePos = serialBuffer.indexOf('\n');
      if (newlinePos >= 0) {
        String line = serialBuffer.substring(0, newlinePos);
        serialBuffer = serialBuffer.substring(newlinePos + 1);
        
        // Xử lý phản hồi
        if (line.length() > 0) {
          lastResponse = line;
          responseReceived = true;
          Serial.println("Nhan tu Arduino: " + line);
          
          // Xử lý ngay lập tức cập nhật trạng thái cửa
          if (line.startsWith("STATUS:")) {
            String status = line.substring(7);
            doorIsOpen = (status == "OPEN");
            Serial.println("Cap nhat trang thai cua: " + status);
          }
          else if (line.startsWith("EVENT:")) {
            // Xử lý sự kiện từ Arduino
            String event = line.substring(6);
            handleArduinoEvent(event);
          }
        }
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

// ===== XU LY HTTP ENDPOINTS =====
void handleRoot() {
  String html = "<!DOCTYPE html>"
    "<html>"
    "<head>"
    "  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">"
    "  <title>He Thong Quan Ly Cua</title>"
    "  <style>"
    "    body { font-family: Arial, sans-serif; margin: 0; padding: 20px; text-align: center; }"
    "    .container { max-width: 400px; margin: 0 auto; padding: 20px; border: 1px solid #ccc; border-radius: 5px; }"
    "    input { width: 100%; padding: 10px; margin: 10px 0; box-sizing: border-box; }"
    "    button { background-color: #4CAF50; color: white; padding: 10px 15px; border: none; border-radius: 5px; cursor: pointer; width: 100%; }"
    "    button:hover { opacity: 0.8; }"
    "    h1 { color: #333; }"
    "  </style>"
    "</head>"
    "<body>"
    "  <h1>He Thong Quan Ly Cua</h1>"
    "  <div class=\"container\">"
    "    <h2>Dang nhap</h2>"
    "    <form action=\"/login\" method=\"post\">"
    "      <input type=\"text\" name=\"username\" placeholder=\"Ten dang nhap\" required>"
    "      <input type=\"password\" name=\"password\" placeholder=\"Mat khau\" required>"
    "      <button type=\"submit\">Dang nhap</button>"
    "    </form>"
    "  </div>"
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
      "<html>"
      "<head>"
      "  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">"
      "  <title>Dang nhap that bai</title>"
      "  <style>"
      "    body { font-family: Arial, sans-serif; margin: 0; padding: 20px; text-align: center; }"
      "    .error { color: red; }"
      "    button { background-color: #4CAF50; color: white; padding: 10px 15px; border: none; border-radius: 5px; cursor: pointer; }"
      "  </style>"
      "</head>"
      "<body>"
      "  <h1>Dang nhap that bai</h1>"
      "  <p class=\"error\">Ten dang nhap hoac mat khau khong dung!</p>"
      "  <button onclick=\"window.location.href='/'\">Thu lai</button>"
      "</body>"
      "</html>";
    
    server.send(200, "text/html", html);
  }
}

void handleAdminPanel() {
  String html = "<!DOCTYPE html>"
    "<html>"
    "<head>"
    "  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">"
    "  <title>Trang quan tri</title>"
    "  <style>"
    "    body { font-family: Arial, sans-serif; margin: 0; padding: 20px; }"
    "    .container { max-width: 800px; margin: 0 auto; }"
    "    .header { display: flex; justify-content: space-between; align-items: center; }"
    "    .panel { border: 1px solid #ccc; border-radius: 5px; padding: 15px; margin-bottom: 20px; }"
    "    button { background-color: #4CAF50; color: white; padding: 8px 15px; border: none; border-radius: 5px; cursor: pointer; margin: 5px; }"
    "    button.danger { background-color: #f44336; }"
    "    table { width: 100%; border-collapse: collapse; margin-top: 10px; }"
    "    th, td { border: 1px solid #ddd; padding: 8px; text-align: left; }"
    "    th { background-color: #f2f2f2; }"
    "    .hidden { display: none; }"
    "    input, select { padding: 8px; margin: 5px 0; width: 100%; box-sizing: border-box; }"
    "    .status { font-size: 18px; margin: 10px 0; }"
    "  </style>"
    "</head>"
    "<body>"
    "  <div class=\"container\">"
    "    <div class=\"header\">"
    "      <h1>Trang quan tri</h1>"
    "      <button onclick=\"window.location.href='/'\">Dang xuat</button>"
    "    </div>"
    "    "
    "    <div class=\"panel\">"
    "      <h2>Dieu khien cua</h2>"
    "      <p class=\"status\" id=\"doorStatus\">Trang thai: Dang tai...</p>"
    "      <button id=\"doorBtn\" onclick=\"toggleDoor()\">Mo cua</button>"
    "    </div>"
    "    "
    "    <div class=\"panel\">"
    "      <h2>Quan ly nguoi dung</h2>"
    "      <button onclick=\"toggleUserForm()\">Them nguoi dung</button>"
    "      "
    "      <div id=\"userForm\" class=\"hidden\">"
    "        <h3>Them nguoi dung moi</h3>"
    "        <input type=\"text\" id=\"newUsername\" placeholder=\"Ten dang nhap\">"
    "        <input type=\"password\" id=\"newPassword\" placeholder=\"Mat khau\">"
    "        <label>"
    "          <input type=\"checkbox\" id=\"isAdmin\"> Quyen quan tri"
    "        </label>"
    "        <div>"
    "          <button onclick=\"addUser()\">Luu</button>"
    "          <button class=\"danger\" onclick=\"toggleUserForm()\">Huy</button>"
    "        </div>"
    "      </div>"
    "      "
    "      <h3>Danh sach nguoi dung</h3>"
    "      <table id=\"userTable\">"
    "        <tr>"
    "          <th>Ten dang nhap</th>"
    "          <th>Quyen han</th>"
    "          <th>Thao tac</th>"
    "        </tr>"
    "      </table>"
    "    </div>"
    "    "
    "    <div class=\"panel\">"
    "      <h2>Nhat ky hoat dong</h2>"
    "      <table id=\"logTable\">"
    "        <tr>"
    "          <th>Thoi gian</th>"
    "          <th>Nguoi dung</th>"
    "          <th>Hanh dong</th>"
    "        </tr>"
    "      </table>"
    "    </div>"
    "  </div>";

  // Thêm JavaScript riêng biệt
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
    "      document.getElementById('doorStatus').innerText = 'Trang thai: ' + (doorOpen ? 'Dang mo' : 'Da dong');"
    "      document.getElementById('doorBtn').innerText = doorOpen ? 'Dong cua' : 'Mo cua';"
    "      document.getElementById('doorBtn').disabled = doorOpen;"
    "    });"
    "}"
    
    "function toggleDoor() {"
    "  fetch('/api/open-door', { method: 'POST' })"
    "    .then(response => response.json())"
    "    .then(data => {"
    "      if (data.success) {"
    "        updateDoorStatus();"
    "      } else {"
    "        alert('Loi: ' + data.message);"
    "      }"
    "    });"
    "}"
    
    "function toggleUserForm() {"
    "  const form = document.getElementById('userForm');"
    "  form.classList.toggle('hidden');"
    "}"
    
    "function loadUsers() {"
    "  fetch('/api/users')"
    "    .then(response => response.json())"
    "    .then(data => {"
    "      const table = document.getElementById('userTable');"
    "      while (table.rows.length > 1) {"
    "        table.deleteRow(1);"
    "      }"
    "      data.forEach(user => {"
    "        const row = table.insertRow();"
    "        row.insertCell(0).innerText = user.username;"
    "        row.insertCell(1).innerText = user.isAdmin ? 'Quan tri vien' : 'Nguoi dung';"
    "        const actionsCell = row.insertCell(2);"
    "        const deleteBtn = document.createElement('button');"
    "        deleteBtn.className = 'danger';"
    "        deleteBtn.innerText = 'Xoa';"
    "        deleteBtn.onclick = function() { deleteUser(user.username); };"
    "        actionsCell.appendChild(deleteBtn);"
    "      });"
    "    });"
    "}"
    
    "function addUser() {"
    "  const username = document.getElementById('newUsername').value;"
    "  const password = document.getElementById('newPassword').value;"
    "  const isAdmin = document.getElementById('isAdmin').checked;"
    "  if (!username || !password) {"
    "    alert('Vui long nhap day du thong tin');"
    "    return;"
    "  }"
    "  fetch('/api/add-user', {"
    "    method: 'POST',"
    "    headers: { 'Content-Type': 'application/json' },"
    "    body: JSON.stringify({ username, password, isAdmin })"
    "  })"
    "  .then(response => response.json())"
    "  .then(data => {"
    "    if (data.success) {"
    "      alert('Da them nguoi dung thanh cong');"
    "      document.getElementById('newUsername').value = \"\";"
    "      document.getElementById('newPassword').value = \"\";"
    "      document.getElementById('isAdmin').checked = false;"
    "      toggleUserForm();"
    "      loadUsers();"
    "    } else {"
    "      alert('Loi: ' + data.message);"
    "    }"
    "  });"
    "}"
    
    "function deleteUser(username) {"
    "  if (confirm('Ban co chac chan muon xoa nguoi dung ' + username + '?')) {"
    "    fetch('/api/delete-user', {"
    "      method: 'POST',"
    "      headers: { 'Content-Type': 'application/json' },"
    "      body: JSON.stringify({ username })"
    "    })"
    "    .then(response => response.json())"
    "    .then(data => {"
    "      if (data.success) {"
    "        alert('Da xoa nguoi dung thanh cong');"
    "        loadUsers();"
    "      } else {"
    "        alert('Loi: ' + data.message);"
    "      }"
    "    });"
    "  }"
    "}"
    
    "function loadLogs() {"
    "  fetch('/api/logs')"
    "    .then(response => response.json())"
    "    .then(data => {"
    "      const table = document.getElementById('logTable');"
    "      while (table.rows.length > 1) {"
    "        table.deleteRow(1);"
    "      }"
    "      data.forEach(log => {"
    "        const row = table.insertRow();"
    "        row.insertCell(0).innerText = log.time;"
    "        row.insertCell(1).innerText = log.user;"
    "        row.insertCell(2).innerText = log.action;"
    "      });"
    "    });"
    "}"
    "</script>"
    "</body>"
    "</html>";
  
  server.send(200, "text/html", html);
}

void handleUserDashboard() {
  String username = server.arg("user");
  
  // Kiem tra nguoi dung hop le
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
    "<html>"
    "<head>"
    "  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">"
    "  <title>Dieu khien cua</title>"
    "  <style>"
    "    body { font-family: Arial, sans-serif; margin: 0; padding: 20px; text-align: center; }"
    "    .container { max-width: 500px; margin: 0 auto; padding: 20px; border: 1px solid #ccc; border-radius: 5px; }"
    "    .header { display: flex; justify-content: space-between; align-items: center; max-width: 500px; margin: 0 auto; }"
    "    button { background-color: #4CAF50; color: white; padding: 15px 30px; font-size: 18px; border: none; border-radius: 5px; cursor: pointer; margin: 20px 0; }"
    "    button:disabled { background-color: #cccccc; }"
    "    .status { font-size: 18px; margin: 20px 0; }"
    "  </style>"
    "</head>"
    "<body>";
  
  html += "<div class='header'><h1>Xin chao, " + username + "</h1>";
  html += "<button onclick=\"window.location.href='/'\" style='padding: 8px; font-size: 14px;'>Dang xuat</button></div>";
  
  html += "<div class=\"container\">"
    "    <h2>Dieu khien cua</h2>"
    "    <p class=\"status\" id=\"doorStatus\">Trang thai: Dang tai...</p>"
    "    <button id=\"doorBtn\" onclick=\"toggleDoor()\">Mo cua</button>"
    "  </div>";
  
  html += "<script>"
    "    let doorOpen = false;"
    "    const username = \"" + username + "\";"
    "    "
    "    window.onload = function() {"
    "      updateDoorStatus();"
    "      setInterval(updateDoorStatus, 5000);"
    "    };"
    "    "
    "    function updateDoorStatus() {"
    "      fetch('/api/door-status')"
    "        .then(response => response.json())"
    "        .then(data => {"
    "          doorOpen = data.isOpen;"
    "          document.getElementById('doorStatus').innerText = 'Trang thai: ' + (doorOpen ? 'Dang mo' : 'Da dong');"
    "          document.getElementById('doorBtn').innerText = doorOpen ? 'Dong cua' : 'Mo cua';"
    "          document.getElementById('doorBtn').disabled = doorOpen;"
    "        });"
    "    }"
    "    "
    "    function toggleDoor() {"
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
    "          alert('Loi: ' + data.message);"
    "        }"
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
  // Parse dữ liệu từ client
  String user = "web_user";
  if (server.hasArg("plain")) {
    DynamicJsonDocument doc(200);
    DeserializationError error = deserializeJson(doc, server.arg("plain"));
    
    if (!error && doc.containsKey("user")) {
      user = doc["user"].as<String>();
    }
  }
  
  // Gửi lệnh mở cửa đến Arduino với timeout dài hơn
  String response = sendCommandToArduino("DOOR:OPEN", 5000);  // 5 giây timeout
  
  if (response.startsWith("OK:")) {
    doorIsOpen = true;
    lastAccessUser = user;
    lastAccessTime = millis();
    
    // Lưu log
    preferences.begin("doorlogs", false);
    int logCount = preferences.getInt("logCount", 0);
    if (logCount >= 50) logCount = 0;
    
    String timeStr = String(millis());
    String logKey = "log" + String(logCount);
    String logValue = timeStr + "|" + user + "|Mo cua";
    preferences.putString(logKey.c_str(), logValue.c_str());
    preferences.putInt("logCount", logCount + 1);
    preferences.end();
    
    server.send(200, "application/json", "{\"success\":true,\"message\":\"" + response + "\"}");
  } else {
    server.send(200, "application/json", "{\"success\":false,\"message\":\"" + response + "\"}");
  }
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
  preferences.begin("doorlogs", true);
  int logCount = preferences.getInt("logCount", 0);
  
  String json = "[";
  bool firstItem = true;
  
  // Lay 20 ban ghi moi nhat
  for(int i = logCount - 1; i >= 0 && i >= logCount - 20; i--) {
    int index = i;
    if(i < 0) index += 50; // Quay vong cho log
    
    String logKey = "log" + String(index);
    String logValue = preferences.getString(logKey.c_str(), "");
    
    if(logValue.length() > 0) {
      // Phan tich dinh dang: time|user|action
      int delimPos1 = logValue.indexOf('|');
      int delimPos2 = logValue.indexOf('|', delimPos1 + 1);
      
      if(delimPos1 > 0 && delimPos2 > delimPos1) {
        if(!firstItem) json += ",";
        String time = logValue.substring(0, delimPos1);
        String user = logValue.substring(delimPos1 + 1, delimPos2);
        String action = logValue.substring(delimPos2 + 1);
        
        json += "{\"time\":\"" + time + "\",";
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
