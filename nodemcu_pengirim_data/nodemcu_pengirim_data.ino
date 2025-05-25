#include <ESP8266WiFi.h>

// WiFi credentials
const char* ssid = "Kos Babeh E";
const char* password = "Japemethe14";

// ThingSpeak settings
const char* server = "api.thingspeak.com";
String apiKey = "05S71HGMFNCTGRHP"; // Verify this is your write API key

// Variables for data buffering and validation
String inputBuffer = "";
bool dataComplete = false;
unsigned long lastDataTime = 0;
const unsigned long TIMEOUT = 10000; // 10 seconds timeout

WiFiClient client;

void setup() {
  Serial.begin(115200);
  delay(100);
  
  // Connect to WiFi
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
  
  // Read data from Arduino
  while (Serial.available() > 0) {
    char inChar = (char)Serial.read();
    
    if (inChar == '\n') {
      dataComplete = true;
    } else {
      inputBuffer += inChar;
    }
  }
  
  // Process complete data
  if (dataComplete) {
    Serial.println("Data mentah diterima: " + inputBuffer);
    
    // Process and validate the data
    if (validateAndSendData(inputBuffer)) {
      Serial.println("Data valid dan dikirim ke ThingSpeak");
    } else {
      Serial.println("Data tidak valid atau gagal dikirim");
    }
    
    // Reset for next data
    inputBuffer = "";
    dataComplete = false;
    lastDataTime = millis();
  }
  
  // Check for timeout (if data is partially received)
  if (!dataComplete && inputBuffer.length() > 0 && (millis() - lastDataTime > TIMEOUT)) {
    Serial.println("Timeout! Data tidak lengkap: " + inputBuffer);
    inputBuffer = "";
    lastDataTime = millis();
  }
  
  // Small delay to prevent CPU hogging
  delay(10);
}

bool validateAndSendData(String data) {
  // First clean up the data - trim whitespace
  data.trim();
  
  // Check basic formatting - should have 3 commas for 4 values
  int commaCount = 0;
  for (int i = 0; i < data.length(); i++) {
    if (data.charAt(i) == ',') commaCount++;
  }
  
  if (commaCount != 3) {
    Serial.println("Format data salah: seharusnya ada 3 koma untuk 4 nilai sensor");
    Serial.println("Jumlah koma terdeteksi: " + String(commaCount));
    return false;
  }
  
  // Parse the data
  float ph, tds, ntu, temperature;
  int itemsRead = sscanf(data.c_str(), "%f,%f,%f,%f", &ph, &tds, &ntu, &temperature);
  
  if (itemsRead != 4) {
    Serial.println("Gagal membaca 4 nilai sensor!");
    Serial.println("Jumlah nilai yang berhasil dibaca: " + String(itemsRead));
    return false;
  }
  
  // Validate ranges - basic sanity check
  if (ph < 0 || ph > 14 || tds < 0 || tds > 5000 || 
      ntu < 0 || ntu > 5000 || temperature < 0 || temperature > 100) {
    Serial.println("Nilai sensor di luar rentang normal!");
    Serial.println("pH: " + String(ph) + ", TDS: " + String(tds) + 
                   ", Turbidity: " + String(ntu) + ", Temp: " + String(temperature));
    // Uncomment line below if you want to allow sending even if ranges are suspicious
    // return false;
  }
  
  // Print validated data
  Serial.println("Data valid:");
  Serial.println("pH: " + String(ph, 2));
  Serial.println("TDS: " + String(tds, 0) + " ppm");
  Serial.println("Turbidity: " + String(ntu, 2) + " NTU");
  Serial.println("Temperature: " + String(temperature, 2) + " °C");
  
  // Send to ThingSpeak
  return sendToThingSpeak(ph, tds, ntu, temperature);
}

bool sendToThingSpeak(float ph, float tds, float ntu, float temperature) {
  Serial.println("Mengirim data ke ThingSpeak...");
  
  // Enforce valid range - prevent sending extreme values
  ph = constrain(ph, 0, 14);
  tds = constrain(tds, 0, 3000);
  ntu = constrain(ntu, 0, 3000);
  temperature = constrain(temperature, -10, 100);
  
  // Debug values being sent
  Serial.println("Nilai yang akan dikirim:");
  Serial.println("pH: " + String(ph, 2));
  Serial.println("TDS: " + String(tds, 0));
  Serial.println("Turbidity: " + String(ntu, 2));
  Serial.println("Temp: " + String(temperature, 2));
  
  // Connect to ThingSpeak
  Serial.print("Menghubungi ThingSpeak...");
  if (!client.connect(server, 80)) {
    Serial.println("\n❌ Gagal terhubung ke ThingSpeak!");
    return false;
  }
  Serial.println(" tersambung!");
  
  // Use String.c_str() for more reliable string handling
  String getData = "GET /update?api_key=" + apiKey;
  getData += "&field1=" + String(ph, 2);
  getData += "&field2=" + String((int)tds); // Convert to integer - ThingSpeak prefers simpler values
  getData += "&field3=" + String(ntu, 2);
  getData += "&field4=" + String(temperature, 2);
  
  Serial.println("URL Request: " + getData);
  
  // Send HTTP request with revised headers
  client.println(getData + " HTTP/1.1");
  client.println("Host: " + String(server));
  client.println("Connection: close");
  client.println();
  
  Serial.println("Waiting for response...");
  
  // Wait for server response with improved timeout handling
  unsigned long startTime = millis();
  while (client.connected() && millis() - startTime < 10000) {
    if (client.available()) {
      // Capture full response for better debugging
      String response = "";
      while (client.available()) {
        char c = client.read();
        response += c;
      }
      
      Serial.println("Full ThingSpeak Response:");
      Serial.println(response);
      
      // Check for success in response
      if (response.indexOf("200 OK") > 0) {
        Serial.println("✅ Data berhasil terkirim ke ThingSpeak!");
        client.stop();
        return true;
      } else {
        Serial.println("❌ Error dari ThingSpeak, response code bukan 200 OK");
        client.stop();
        return false;
      }
    }
    delay(10);
  }
  
  // If we got here, we timed out waiting for a response
  Serial.println("⚠️ Timeout waiting for ThingSpeak response");
  client.stop();
  
  // ThingSpeak requires 15 seconds between updates
  Serial.println("Menunggu 15 detik sebelum pengiriman berikutnya...");
  delay(15000);
  
  return false;
}

void reconnectWiFi() {
  Serial.println("WiFi terputus! Menghubungkan ulang...");
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