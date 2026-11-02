#include <WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <Adafruit_AHTX0.h>

// ===== KONFIGURASI JARINGAN =====
const char* WIFI_SSID = "Supercell";
const char* WIFI_PASS = "MyDearest";
const char* BROKER_IP = "broker.hivemq.com"; 

// ===== IDENTIFIKASI RUANGAN =====
const char* ROOM_NAME = "ruang2";

// ===== PEMETAAN PIN HARDWARE (ESP32 DevKit V1) =====
// Catatan: Sensor AHT10 menggunakan I2C (SDA = GPIO 21, SCL = GPIO 22)
const int L1_PIN      = 2;  // LED Lampu 1
const int L2_PIN      = 18; // LED Lampu 2

WiFiClient net;
PubSubClient mqtt(net);
Adafruit_AHTX0 aht; // Objek Sensor AHT10
String clientId;

// Topic Buffers (Mengikuti pola pens/klp1/<ruang>/...)
char tTemp[50], tHum[50], tL1[50], tL2[50], tStatus[50], tNotif[50];

unsigned long lastPublish = 0;
unsigned long lastWiFiRetry = 0;

// ===== CALLBACK HANDLING (SUBSCRIBE) =====
void callback(char* topic, byte* payload, unsigned int length) {
  String message = "";
  for (unsigned int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  message.trim(); // Hapus spasi atau karakter tersembunyi

  Serial.print("Pesan masuk [Topic: ");
  Serial.print(topic);
  Serial.print("] Value: ");
  Serial.println(message);

  // Tentukan state (HIGH/LOW)
  int state = (message == "1") ? HIGH : LOW;

  // Cek dan kontrol Lampu 1
  if (strcmp(topic, tL1) == 0) {
    if (message == "1" || message == "0") {
      digitalWrite(L1_PIN, state);
      Serial.println(state == HIGH ? "Lampu 1 MENYALA" : "Lampu 1 MATI");
    }
  } 
  // Cek dan kontrol Lampu 2
  else if (strcmp(topic, tL2) == 0) {
    if (message == "1" || message == "0") {
      digitalWrite(L2_PIN, state);
      Serial.println(state == HIGH ? "Lampu 2 MENYALA" : "Lampu 2 MATI");
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000); 

  pinMode(L1_PIN, OUTPUT);
  pinMode(L2_PIN, OUTPUT);
  digitalWrite(L1_PIN, LOW);
  digitalWrite(L2_PIN, LOW);

  // Inisialisasi Sensor AHT10
  Wire.begin();
  if (!aht.begin()) {
    Serial.println("Gagal mendeteksi sensor AHT10! Periksa kembali kabel I2C.");
    while (1) {
      delay(10);
    }
  }
  Serial.println("Sensor AHT10 berhasil diinisialisasi.");

  // Pembentukan Topik MQTT Sesuai Standar pens/klp1/<ruang>/...
  snprintf(tTemp, sizeof(tTemp), "pens/klp1/%s/temp", ROOM_NAME);
  snprintf(tHum, sizeof(tHum), "pens/klp1/%s/humidity", ROOM_NAME);
  snprintf(tL1, sizeof(tL1), "pens/klp1/%s/lamp1", ROOM_NAME);
  snprintf(tL2, sizeof(tL2), "pens/klp1/%s/lamp2", ROOM_NAME);
  snprintf(tStatus, sizeof(tStatus), "pens/klp1/%s/status", ROOM_NAME);
  snprintf(tNotif, sizeof(tNotif), "pens/klp1/%s/notif", ROOM_NAME); // Topik khusus notifikasi

  // Cetak topik di Serial Monitor untuk verifikasi
  Serial.println("\n--- DAFTAR TOPIK MQTT ---");
  Serial.println(tTemp);
  Serial.println(tHum);
  Serial.println(tL1);
  Serial.println(tL2);
  Serial.println(tStatus);
  Serial.println(tNotif);
  Serial.println("--------------------------");

  WiFi.mode(WIFI_STA);
  Serial.print("Connecting to WiFi: ");
  Serial.println(WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  clientId = "esp32-devkit-pens-" + WiFi.macAddress();

  mqtt.setServer(BROKER_IP, 1883);
  mqtt.setCallback(callback);
  mqtt.setKeepAlive(15);
}

void connectMQTT() {
  if (WiFi.status() != WL_CONNECTED) return;

  static unsigned long lastConnectAttempt = 0;
  if (millis() - lastConnectAttempt >= 5000) { 
    lastConnectAttempt = millis();
    
    Serial.print("Attempting MQTT connection...");
    if (mqtt.connect(clientId.c_str(), tStatus, 0, true, "offline")) {
      Serial.println("connected!");
      
      // Subscribe topik kontrol
      mqtt.subscribe(tL1, 1);
      mqtt.subscribe(tL2, 1);
      
      // Kirim status online
      mqtt.publish(tStatus, "online", true);

      // --- FITUR NOTIFIKASI KETIKA MQTT TERSAMBUNG ---
      String notifMsg = "ESP32 " + String(ROOM_NAME) + " berhasil terhubung ke broker MQTT.";
      mqtt.publish(tNotif, notifMsg.c_str());
      Serial.println("[MQTT] Notifikasi terhubung dikirim.");
      
    } else {
      Serial.print("failed, rc=");
      Serial.print(mqtt.state());
      Serial.println(" try again in 5 seconds");
    }
  }
}

void loop() {
  // Penanganan Koneksi WiFi yang Aman dari Error "sta is connecting"
  if (WiFi.status() != WL_CONNECTED) {
    if (millis() - lastWiFiRetry >= 10000) { 
      lastWiFiRetry = millis();
      Serial.println("WiFi disconnected. Reconnecting...");
      WiFi.disconnect();
      WiFi.begin(WIFI_SSID, WIFI_PASS);
    }
    return;
  }

  // Penanganan Koneksi MQTT
  if (!mqtt.connected()) {
    connectMQTT();
    return;
  }

  mqtt.loop();

  // Kirim data sensor setiap 5 detik
  if (millis() - lastPublish >= 5000) {
    lastPublish = millis();

    // Membaca data suhu dan kelembaban riil dari sensor AHT10
    sensors_event_t humidity, temp;
    aht.getEvent(&humidity, &temp);

    char tempBuf[8];
    char humBuf[8];
    
    snprintf(tempBuf, sizeof(tempBuf), "%.1f", temp.temperature);
    snprintf(humBuf, sizeof(humBuf), "%.1f", humidity.relative_humidity);

    // Publikasi ke broker MQTT
    mqtt.publish(tTemp, tempBuf);
    mqtt.publish(tHum, humBuf);
    
    // Cetak ke Serial Monitor
    Serial.print("[Publish] Suhu AHT10: ");
    Serial.print(tempBuf);
    Serial.print(" °C | Kelembapan AHT10: ");
    Serial.print(humBuf);
    Serial.println(" %");
  }
}
