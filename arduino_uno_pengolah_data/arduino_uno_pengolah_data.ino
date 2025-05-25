#include <OneWire.h>
#include <DallasTemperature.h>
#include <SoftwareSerial.h>

// Pin Definitions
#define PH_PIN A0
#define TDS_PIN A1
#define TURBIDITY_PIN A2
#define ONE_WIRE_BUS 2

// Sensor Setup
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);
SoftwareSerial espSerial(10, 11);

// TDS Calibration
const float CAL_TDS_LOW = 226.0, CAL_TDS_HIGH = 500.0;
const float CAL_RAW_LOW = 57.0, CAL_RAW_HIGH = 72.0;
const float TDS_SLOPE = (CAL_TDS_HIGH - CAL_TDS_LOW) / (CAL_RAW_HIGH - CAL_RAW_LOW);
const float TDS_INTERCEPT = CAL_TDS_LOW - (TDS_SLOPE * CAL_RAW_LOW);

// pH Calibration
const float PH4 = 3.69, PH7 = 3.13, PH_STEP = (PH4 - PH7) / 3.0;

// Turbidity Calibration
const float TURB_V_0NTU = 4.2, TURB_V_10NTU = 1.0, TURB_V_1000NTU = 0.7;

// Filter for TDS
float tdsFilter[10];
int filterIdx = 0;

void setup() {
  Serial.begin(115200);
  espSerial.begin(115200);
  sensors.begin();
  Serial.println("=== Water Quality Monitor Started ===");
}

void loop() {
  // Read Temperature
  sensors.requestTemperatures();
  float temp = sensors.getTempCByIndex(0);

  // Read pH
  float phVoltage = analogRead(PH_PIN) * 5.0 / 1024.0;
  float ph = 7.0 + ((PH7 - phVoltage) / PH_STEP);

  // Read TDS
  float tdsAnalog = getAverage(TDS_PIN, 5);
  float tdsVoltage = tdsAnalog * 5.0 / 1024.0;
  float tdsRaw = (133.42 * pow(tdsVoltage, 3) - 255.86 * pow(tdsVoltage, 2) + 857.39 * tdsVoltage) * 0.5;
  float tds = max(0.0f, TDS_SLOPE * tdsRaw + TDS_INTERCEPT);
  
  // Apply simple filter
  tdsFilter[filterIdx] = tds;
  filterIdx = (filterIdx + 1) % 10;
  float filteredTDS = 0;
  for(int i = 0; i < 10; i++) filteredTDS += tdsFilter[i];
  filteredTDS /= 10;

  // Read Turbidity
  float turbVoltage = getAverage(TURBIDITY_PIN, 5) * 5.0 / 1023.0;
  float ntu = 0;
  
  if (turbVoltage >= TURB_V_10NTU && turbVoltage <= TURB_V_0NTU) {
    ntu = map(turbVoltage * 100, TURB_V_10NTU * 100, TURB_V_0NTU * 100, 1000, 0) / 100.0;
  } else if (turbVoltage < TURB_V_10NTU && turbVoltage >= TURB_V_1000NTU) {
    ntu = map(turbVoltage * 100, TURB_V_1000NTU * 100, TURB_V_10NTU * 100, 100000, 1000) / 100.0;
  } else if (turbVoltage < TURB_V_1000NTU) {
    ntu = 1000 + (TURB_V_1000NTU - turbVoltage) * 8000;
  }
  ntu = constrain(ntu, 0, 3000);

  // Send data to NodeMCU
  String data = String(ph, 2) + "," + String(filteredTDS, 0) + "," + String(ntu, 2) + "," + String(temp, 2);
  espSerial.println(data);

  // Debug output
  Serial.print("pH: "); Serial.print(ph, 2);
  Serial.print(", TDS: "); Serial.print(filteredTDS, 0);
  Serial.print(" ppm, Turbidity: "); Serial.print(ntu, 2);
  Serial.print(" NTU, Temp: "); Serial.print(temp, 2); Serial.println(" °C");

  delay(2000);
}

float getAverage(int pin, int samples) {
  long sum = 0;
  for (int i = 0; i < samples; i++) {
    sum += analogRead(pin);
    delay(10);
  }
  return sum / (float)samples;
}