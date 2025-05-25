#include <ESP8266WiFi.h>

// WiFi credentials
const char* ssid = "Kos Babeh E";
const char* password = "Japemethe14";

// ThingSpeak settings
const char* server = "api.thingspeak.com";
String apiKey = "05S71HGMFNCTGRHP";

String inputBuffer = "";
WiFiClient client;

void setup() {
  Serial.begin(115200);
  delay(100);
  
  // Connect WiFi
  WiFi.begin(ssid, password);
  Serial.print("Menghubungkan ke WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  
  Serial.println("\nWiFi Terhubung!");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());
  Serial.println("Menunggu data dari Arduino...");
}

void loop() {
  // Check WiFi connection
  if (WiFi.status() != WL_CONNECTED) {
    reconnectWiFi();
  }
  
  // Read data dari Arduino
  while (Serial.available() > 0) {
    char inChar = (char)Serial.read();
    if (inChar == '\n') {
      processData();
      inputBuffer = "";
    } else {
      inputBuffer += inChar;
    }
  }
  
  delay(10);
}

void processData() {
  inputBuffer.trim();
  Serial.println("Data diterima: " + inputBuffer);
  
  // Parse data: ph,tds,ntu,temperature
  float ph, tds, ntu, temperature;
  int itemsRead = sscanf(inputBuffer.c_str(), "%f,%f,%f,%f", &ph, &tds, &ntu, &temperature);
  
  if (itemsRead == 4) {
    Serial.println("pH: " + String(ph, 2) + ", TDS: " + String(tds, 0) + 
                   ", Turbidity: " + String(ntu, 2) + ", Temp: " + String(temperature, 2));
    sendToThingSpeak(ph, tds, ntu, temperature);
  } else {
    Serial.println("Format data salah!");
  }
}

void sendToThingSpeak(float ph, float tds, float ntu, float temperature) {
  Serial.println("Mengirim ke ThingSpeak...");
  
  // Constrain values
  ph = constrain(ph, 0, 14);
  tds = constrain(tds, 0, 3000);
  ntu = constrain(ntu, 0, 3000);
  temperature = constrain(temperature, -10, 100);
  
  if (client.connect(server, 80)) {
    String getData = "GET /update?api_key=" + apiKey;
    getData += "&field1=" + String(ph, 2);
    getData += "&field2=" + String((int)tds);
    getData += "&field3=" + String(ntu, 2);
    getData += "&field4=" + String(temperature, 2);
    
    client.println(getData + " HTTP/1.1");
    client.println("Host: " + String(server));
    client.println("Connection: close");
    client.println();
    
    // Wait for response
    unsigned long startTime = millis();
    while (client.connected() && millis() - startTime < 5000) {
      if (client.available()) {
        String response = client.readString();
        if (response.indexOf("200 OK") > 0) {
          Serial.println("✅ Data terkirim!");
        } else {
          Serial.println("❌ Error response");
        }
        break;
      }
      delay(10);
    }
    client.stop();
    
    // Delay 15 detik
    delay(15000);
  } else {
    Serial.println("❌ Gagal connect ThingSpeak");
  }
}

void reconnectWiFi() {
  Serial.println("WiFi terputus! Reconnecting...");
  WiFi.disconnect();
  WiFi.begin(ssid, password);
  
  int retry = 0;
  while (WiFi.status() != WL_CONNECTED && retry < 20) {
    delay(500);
    Serial.print(".");
    retry++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi tersambung kembali!");
  } else {
    Serial.println("\nGagal menyambungkan WiFi!");
  }
}