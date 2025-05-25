#include <OneWire.h>
#include <DallasTemperature.h>
#include <SoftwareSerial.h>

// === Pin Definitions ===
#define PH_PIN A0
#define TDS_PIN A1
#define TURBIDITY_PIN A2
#define ONE_WIRE_BUS 2

// === DS18B20 Setup ===
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);
float temperature = 25.0;

// === TDS Setup Baru ===
float analogValue = 0;
float voltage = 0;
float tdsRaw = 0;
float tdsCalibrated = 0;

// === Parameter Tegangan ===
const float VREF = 5.0;
const float ADC_RES = 1024.0;

// === Parameter Kalibrasi Dua Titik ===
const float CAL_TDS_LOW = 226.0;   
float CAL_RAW_LOW = 62.0;        

// Titik kalibrasi 2: Larutan kalibrasi 500 ppm
const float CAL_TDS_HIGH = 500.0; 
float CAL_RAW_HIGH = 72.0;        

// Koefisien kalibrasi dihitung secara dinamis
float slope = (CAL_TDS_HIGH - CAL_TDS_LOW) / (CAL_RAW_HIGH - CAL_RAW_LOW);
float intercept = CAL_TDS_LOW - (slope * CAL_RAW_LOW);

// Variabel untuk filter pembacaan
const int FILTER_SAMPLES = 30;
float filterArray[FILTER_SAMPLES];
int filterIndex = 0;
boolean filterFilled = false;

// === PH Setup ===
float ph = 0;
float PH_step;
double TeganganPh;
float PH4 = 3.69;
float PH7 = 3.13;

// === Turbidity Calibration Parameters ===
// Titik-titik kalibrasi untuk turbidity (awal yang dapat disesuaikan)
const float TURB_VOLTAGE_0NTU = 4.2;    // Tegangan saat air jernih (0 NTU) - sesuaikan
const float TURB_VOLTAGE_10NTU = 1.0;   // Tegangan saat 10 NTU - sesuaikan
const float TURB_VOLTAGE_1000NTU = 0.7; // Estimasi tegangan untuk air sangat keruh (1000 NTU)
const float TURB_MAX_NTU = 3000.0;      // Nilai maksimum NTU yang diizinkan

SoftwareSerial espSerial(10, 11);  // TX ke RX ESP8266

void setup() {
  Serial.begin(115200);
  espSerial.begin(115200);
  sensors.begin();
  pinMode(PH_PIN, INPUT);
  pinMode(TDS_PIN, INPUT);
  pinMode(TURBIDITY_PIN, INPUT);
  
  // Inisialisasi array filter untuk TDS
  for (int i = 0; i < FILTER_SAMPLES; i++) {
    filterArray[i] = 0;
  }
  
  Serial.println("=== Sistem Water Quality Monitoring ===");
  Serial.println("Memulai sensor...");
}

void loop() {
  // === Baca Suhu ===
  sensors.requestTemperatures();
  temperature = sensors.getTempCByIndex(0);

  // === Baca pH ===
  int nilai_analog_PH = analogRead(PH_PIN);
  TeganganPh = 5.0 / 1024.0 * nilai_analog_PH;
  PH_step = (PH4 - PH7) / 3.0;
  ph = 7.00 + ((PH7 - TeganganPh) / PH_step);

  // === Baca Turbidity (Versi Baru dengan Multi-segmen) ===
  long turbiditySum = 0;
  for (int i = 0; i < 10; i++) {
    turbiditySum += analogRead(TURBIDITY_PIN);  // Ambil nilai dari sensor turbidity
    delay(10);  // Jeda antar pembacaan untuk mengurangi noise
  }

  int sensorTurbidity = turbiditySum / 10;  // Hitung rata-rata dari 10 pembacaan
  float turbidityVoltage = sensorTurbidity * (5.0 / 1023.0);
  
  float ntu = 0.0;
  
  // Metode Kalibrasi Multi-segmen untuk memperluas range pengukuran
  if (turbidityVoltage >= TURB_VOLTAGE_10NTU && turbidityVoltage <= TURB_VOLTAGE_0NTU) {
    // Rentang 0-10 NTU (menggunakan kalibrasi standar)
    ntu = mapFloat(turbidityVoltage, TURB_VOLTAGE_10NTU, TURB_VOLTAGE_0NTU, 10.0, 0.0);
  } 
  else if (turbidityVoltage < TURB_VOLTAGE_10NTU && turbidityVoltage >= TURB_VOLTAGE_1000NTU) {
    // Rentang 10-1000 NTU (kalibrasi tinggi)
    ntu = mapFloat(turbidityVoltage, TURB_VOLTAGE_1000NTU, TURB_VOLTAGE_10NTU, 1000.0, 10.0);
  }
  else if (turbidityVoltage < TURB_VOLTAGE_1000NTU) {
    // Lebih dari 1000 NTU
    // Menggunakan kurva eksponensial sederhana untuk tegangan yang sangat rendah
    float voltDiff = TURB_VOLTAGE_1000NTU - turbidityVoltage;
    float extraNTU = voltDiff * 8000.0; // Faktor skala untuk memperbesar range
    ntu = 1000.0 + extraNTU;
  }
  
  // Batasi nilai NTU pada rentang yang masuk akal
  if (ntu < 0) ntu = 0;
  if (ntu > TURB_MAX_NTU) ntu = TURB_MAX_NTU;  // Batas atas yang wajar

  // // Untuk debug nilai tegangan turbidity
  // Serial.print("Analog Turbidity: ");
  // Serial.print(sensorTurbidity);
  // Serial.print(", Tegangan: ");
  // Serial.print(turbidityVoltage, 3);
  // Serial.print("V, NTU: ");
  // Serial.println(ntu, 1);

  // === Baca TDS (Kode Baru) ===
  // Baca nilai analog dan konversi ke tegangan
  analogValue = getMedianReading(TDS_PIN, 10);
  voltage = analogValue * VREF / ADC_RES;

  // Hitung TDS mentah (rumus dari DFRobot)
  tdsRaw = (133.42 * voltage * voltage * voltage
          - 255.86 * voltage * voltage
          + 857.39 * voltage) * 0.5;

  // Aplikasikan kalibrasi linear dua titik
  tdsCalibrated = slope * tdsRaw + intercept;

  // Filter pembacaan untuk stabilitas
  tdsCalibrated = updateFilter(tdsCalibrated);

  // Koreksi untuk nilai negatif (jika ada)
  if (tdsCalibrated < 0) tdsCalibrated = 0;

  // Kirim ke NodeMCU (gunakan separator koma)
  String data = String(ph, 2) + "," + String(tdsCalibrated, 0) + "," + String(ntu, 2) + "," + String(temperature, 2);
  espSerial.println(data);

  // Debug lengkap
  Serial.print("pH: ");
  Serial.print(ph, 2);
  Serial.print(", TDS: ");
  Serial.print(tdsCalibrated, 0);
  Serial.print(" ppm, Turbidity: ");
  Serial.print(ntu, 2);
  Serial.print(" NTU, Suhu: ");
  Serial.print(temperature, 2);
  Serial.println(" °C");

  delay(2000);
}

// === Fungsi pemetaan float (seperti map() tetapi untuk float) ===
float mapFloat(float x, float in_min, float in_max, float out_min, float out_max) {
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

// === Fungsi TDS Baru ===
// Fungsi untuk mendapatkan nilai median dari beberapa pembacaan
float getMedianReading(int pin, int samples) {
  float readings[samples];
  
  // Ambil sampel pembacaan
  for (int i = 0; i < samples; i++) {
    readings[i] = analogRead(pin);
    delay(10);
  }
  
  // Urutkan pembacaan (simple bubble sort)
  for (int i = 0; i < samples - 1; i++) {
    for (int j = i + 1; j < samples; j++) {
      if (readings[i] > readings[j]) {
        float temp = readings[i];
        readings[i] = readings[j];
        readings[j] = temp;
      }
    }
  }
  
  // Kembalikan nilai median
  if (samples % 2 == 0) {
    // Jika jumlah sampel genap, ambil rata-rata dari dua nilai tengah
    return (readings[samples/2] + readings[samples/2 - 1]) / 2.0;
  } else {
    // Jika jumlah sampel ganjil, ambil nilai tengah
    return readings[samples/2];
  }
}

// Fungsi filter untuk stabilisasi pembacaan
float updateFilter(float newValue) {
  // Tambahkan nilai baru ke array
  filterArray[filterIndex] = newValue;
  filterIndex = (filterIndex + 1) % FILTER_SAMPLES;
  
  if (filterIndex == 0) {
    filterFilled = true;
  }
  
  // Hitung rata-rata dari semua sampel
  float sum = 0;
  int count = filterFilled ? FILTER_SAMPLES : filterIndex;
  
  for (int i = 0; i < count; i++) {
    sum += filterArray[i];
  }
  
  return sum / count;
}