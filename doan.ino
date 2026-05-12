#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <DHT.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ESP_Mail_Client.h>
#include "WebSocketMCP.h"

// ===== CẤU HÌNH PHẦN CỨNG =====
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

#define LED_BUILTIN 5
#define WARNING_LED 4
#define GAS_LED     16
#define BUZZER_PIN  17
#define DHTPIN      23
#define GAS_PIN     34
#define DHTTYPE     DHT11

// ===== THÔNG TIN KẾT NỐI =====
const char* ssid     = "Thai Meo";
const char* password = "08032017";

// MQTT
const char* mqtt_server   = "broker.hivemq.com";
const char* topic_monitor = "iot/duong123/monitor";
const char* topic_control = "iot/duong123/control";

// XIAOZHI MCP
const char* mcpEndpoint = "wss://api.xiaozhi.me/mcp/?token=eyJhbGciOiJFUzI1NiIsInR5cCI6IkpXVCJ9.eyJ1c2VySWQiOjg4NTY1NSwiYWdlbnRJZCI6MTcxODA0NCwiZW5kcG9pbnRJZCI6ImFnZW50XzE3MTgwNDQiLCJwdXJwb3NlIjoibWNwLWVuZHBvaW50IiwiaWF0IjoxNzc3OTE0ODE2LCJleHAiOjE4MDk0NzI0MTZ9.quv_v6X2BNvdaNF_5Y573hYDzeiDqHM3sVi3yQWSaqB2h3SsSsIRSHhRV5BuGAXZXK-rIBY02DtvNbmfTg_9EA";

// EMAIL CONFIG
#define SMTP_HOST "smtp.gmail.com"
#define SMTP_PORT 465
#define AUTHOR_EMAIL "doantotnghiepd@gmail.com"
#define AUTHOR_PASSWORD "zrwtmuopjoigrzrl" // 16 ký tự App Password
#define RECIPIENT_EMAIL "duongche77@gmail.com"

// ===== BIẾN HỆ THỐNG =====
float temperature = 0, gasPPM = 0;
float warningTemp = 40.0;
float gasCleanThreshold = 300.0; 
float gasLightThreshold = 1000.0;

String ledMode = "auto";
bool blinkState = false;
bool emailSent  = false;
unsigned long lastPublish = 0;

WiFiClient espClient;
PubSubClient mqttClient(espClient);
WebSocketMCP mcpClient;
DHT dht(DHTPIN, DHTTYPE);
SMTPSession smtp;

// ========================================================================================
// HÀM HỖ TRỢ (SENSORS & DISPLAY)
// ========================================================================================
float calculateGasPPM(int rawADC) {
    float voltage = (float)rawADC * 3.3 / 4095.0;
    if (voltage < 0.01) return 0.0;
    float RS = (3.3 / voltage - 1.0) * 10.0;
    return 1000.0 * pow(RS / 9.8, -1.5);
}

String getGasStatus() {
    if (gasPPM < gasCleanThreshold) return "SACH";
    if (gasPPM < gasLightThreshold) return "KHI NHE";
    return "NGUY HIEM";
}

void updateOLED() {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    
    display.setCursor(0, 0);
    display.printf("T: %.1f C | Lim: %.0f", temperature, warningTemp);
    
    display.setCursor(0, 16);
    display.printf("Gas: %.0f PPM", gasPPM);
    
    display.setCursor(0, 32);
    display.print("Status: "); display.print(getGasStatus());
    
    display.setCursor(0, 48);
    display.print("LED: "); display.print(ledMode);
    display.print(mqttClient.connected() ? " | M:OK" : " | M:ER");

    display.drawLine(0, 12, 128, 12, SSD1306_WHITE);
    display.display();
}
// ========================================================================================
// TRUYỀN DỮ LIỆU & EMAIL (Đã tối ưu bằng FreeRTOS)
// ========================================================================================
void sendEmailTask(void *pvParameters) {
    Serial.println("--- Bat dau tien trinh gui email ---");

    ESP_Mail_Session session;
    session.server.host_name = SMTP_HOST;
    session.server.port = SMTP_PORT;
    session.login.email = AUTHOR_EMAIL;
    session.login.password = AUTHOR_PASSWORD;

    SMTP_Message message;
    message.sender.name = "SMART GUARD SYSTEM";
    message.sender.email = AUTHOR_EMAIL;
    message.subject = "🚨 CANH BAO CHAY NO NGUY HIEM!";
    message.addRecipient("Admin", RECIPIENT_EMAIL);

    String htmlMsg = "<div style='font-family:sans-serif; border:2px solid red; padding:20px;'>";
    htmlMsg += "<h2 style='color:red;'>CANH BAO NGUY HIEM KEP!</h2>";
    htmlMsg += "<p>Phat hien nhiet do cao va khi gas o muc bao dong.</p>";
    htmlMsg += "<b>Nhiet do:</b> " + String(temperature, 1) + " C<br>";
    htmlMsg += "<b>Nong do Gas:</b> " + String((int)gasPPM) + " PPM<br>";
    htmlMsg += "<p>Kiem tra ngay lap tuc!</p></div>";
    message.html.content = htmlMsg.c_str();

    // Thực hiện kết nối
    Serial.println("Dang ket noi den SMTP Server...");
    if (smtp.connect(&session)) {
        Serial.println("Ket noi thanh cong! Dang gui mail...");
        
        // Gửi mail
        if (MailClient.sendMail(&smtp, &message)) {
            Serial.println(">> GUI MAIL THANH CONG!");
        } else {
            Serial.print(">> LOI GUI MAIL: ");
            Serial.println(smtp.errorReason()); // In ra lý do lỗi nếu gửi thất bại
        }
    } else {
        Serial.println(">> LOI: Khong the ket noi den SMTP Server.");
    }

    Serial.println("--- Ket thuc tien trinh ---");
    vTaskDelete(NULL);
}

void publishData() {
    StaticJsonDocument<512> doc;
    doc["temperature"] = temperature;
    doc["gasValue"] = (int)gasPPM;
    doc["warningTemp"] = warningTemp;
    doc["gasCleanThreshold"] = gasCleanThreshold;
    doc["gasLightThreshold"] = gasLightThreshold;
    doc["ledMode"] = ledMode;
    
    String gStat = getGasStatus();
    doc["gasStatus"] = gStat;
    doc["overTemp"] = (temperature > warningTemp);
    doc["gasDanger"] = (gStat == "NGUY HIEM");
    doc["fireDanger"] = (temperature > warningTemp && gStat == "NGUY HIEM");

    char buffer[512];
    serializeJson(doc, buffer);
    mqttClient.publish(topic_monitor, buffer);
    updateOLED();
}

// ========================================================================================
// XIAOZHI MCP TOOLS
// ========================================================================================
void registerMcpTools() {
    mcpClient.registerTool("temperature_values", "Bao nhiet do", "{}", [](const String& a){
        return WebSocketMCP::ToolResponse("{\"temperature\":" + String(temperature) + "}");});
    mcpClient.registerTool("gas_value", "Gia tri gas PPM", "{}", [](const String& a){
        return WebSocketMCP::ToolResponse("{\"gas\":" + String(gasPPM) + "}");});
    mcpClient.registerTool("set_gas_clean", "Set nguong SACH", R"({"type":"object","properties":{"clean_ppm":{"type":"number"}}})", 
    [](const String& args){
        DynamicJsonDocument d(128); deserializeJson(d, args);
        gasCleanThreshold = d["clean_ppm"]; publishData();
        return WebSocketMCP::ToolResponse("{\"success\":true}");});
    mcpClient.registerTool("set_gas_light", "Set nguong KHI NHE", R"({"type":"object","properties":{"light_ppm":{"type":"number"}}})", 
    [](const String& args){
        DynamicJsonDocument d(128); deserializeJson(d, args);
        gasLightThreshold = d["light_ppm"]; publishData();
        return WebSocketMCP::ToolResponse("{\"success\":true}");});
    mcpClient.registerTool("set_temperature_warning", "Set nhiet do canh bao", R"({"type":"object","properties":{"max_temperature":{"type":"number"}}})", 
    [](const String& args){
        DynamicJsonDocument d(128); deserializeJson(d, args);
        warningTemp = d["max_temperature"]; publishData();
        return WebSocketMCP::ToolResponse("{\"success\":true}");});
    mcpClient.registerTool("led_control", "Chinh LED (on/off/blink/auto)", R"({"type":"object","properties":{"state":{"type":"string"}}})", 
    [](const String& args){
        DynamicJsonDocument d(128); deserializeJson(d, args);
        ledMode = d["state"].as<String>(); publishData();
        return WebSocketMCP::ToolResponse("{\"success\":true}");});
}

// ========================================================================================
// MQTT CALLBACK & RECONNECT
// ========================================================================================
void mqttCallback(char* topic, byte* payload, unsigned int length) {
    String msg = "";
    for (int i = 0; i < length; i++) msg += (char)payload[i];
    StaticJsonDocument<256> doc;
    deserializeJson(doc, msg);
    
    if (doc.containsKey("led")) ledMode = doc["led"].as<String>();
    if (doc.containsKey("warnT")) warningTemp = doc["warnT"].as<float>();
    if (doc.containsKey("gClean")) gasCleanThreshold = doc["gClean"].as<float>();
    if (doc.containsKey("gLight")) gasLightThreshold = doc["gLight"].as<float>();
    publishData();
}

void reconnectMQTT() {
    while (!mqttClient.connected()) {
        if (mqttClient.connect("ESP32_SmartGuard_Duong")) {
            mqttClient.subscribe(topic_control);
        } else {
            delay(5000);
        }
    }
}

// ========================================================================================
// SETUP & LOOP
// ========================================================================================
void setup() {
    Serial.begin(115200);
    pinMode(LED_BUILTIN, OUTPUT); pinMode(WARNING_LED, OUTPUT);
    pinMode(GAS_LED, OUTPUT); pinMode(BUZZER_PIN, OUTPUT);
    
    dht.begin();
    if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) Serial.println("OLED Fail");
    display.clearDisplay(); display.display();

    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) delay(500);

    mqttClient.setServer(mqtt_server, 1883);
    mqttClient.setCallback(mqttCallback);

    mcpClient.begin(mcpEndpoint, [](bool connected){
        if (connected) registerMcpTools();
    });
}

void loop() {
    mcpClient.loop();
    if (!mqttClient.connected()) reconnectMQTT();
    mqttClient.loop();

    static unsigned long lastBlink = 0;
    if (millis() - lastBlink > 400) { blinkState = !blinkState; lastBlink = millis(); }

    if (millis() - lastPublish > 2000) {
        temperature = dht.readTemperature();
        gasPPM = calculateGasPPM(analogRead(GAS_PIN));
        publishData();
        lastPublish = millis();
    }

    bool isOverTemp = (temperature > warningTemp);
    String gStat = getGasStatus();

    // Logic Email & Buzzer
    if (isOverTemp && gStat == "NGUY HIEM") {
        tone(BUZZER_PIN, blinkState ? 2000 : 1000);
        if (!emailSent) {
            // TẠO TASK CHẠY NGẦM ĐỂ GỬI MAIL THAY VÌ GỌI HÀM TRỰC TIẾP
            xTaskCreate(
                sendEmailTask,      // Tên hàm thực thi
                "SendEmailTask",    // Tên task (để debug)
                8192,               // Kích thước Stack bộ nhớ (Gửi mail cần nhiều RAM cho mã hóa TLS)
                NULL,               // Tham số truyền vào
                1,                  // Mức độ ưu tiên
                NULL                // Task handle
            );
            emailSent = true;
        }
    } else {
        if (gStat == "NGUY HIEM" || isOverTemp) {
            if(blinkState) tone(BUZZER_PIN, 800); else noTone(BUZZER_PIN);
        } else if (gStat == "KHI NHE") {
            if(blinkState) tone(BUZZER_PIN, 400); else noTone(BUZZER_PIN);
        } else {
            noTone(BUZZER_PIN);
        }
        
        // Reset email trigger khi an toan
        if (temperature < (warningTemp - 2) && gStat == "SACH") emailSent = false;
    }

    // LED Hardware
    digitalWrite(WARNING_LED, isOverTemp ? blinkState : LOW);
    digitalWrite(GAS_LED, (gStat != "SACH") ? blinkState : LOW);

    // Built-in LED mode
    if (isOverTemp || gStat != "SACH") {

        // Có cảnh báo -> tắt LED
        digitalWrite(LED_BUILTIN, LOW);

    }
    else {

        // Hoạt động bình thường theo mode
        if (ledMode == "on") {
            digitalWrite(LED_BUILTIN, HIGH);
        }
        else if (ledMode == "off") {
            digitalWrite(LED_BUILTIN, LOW);
        }
        else if (ledMode == "blink") {
            digitalWrite(LED_BUILTIN, blinkState);
        }
        else {
            digitalWrite(LED_BUILTIN, LOW);
        }

    }
}