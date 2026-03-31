#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_BMP085.h>
#include <EEPROM.h>
#include <math.h>   // sinf()

// Define Pins
#define PIN_SDA          21
#define PIN_SCL          22
#define PIN_POT          34     // ADC1_CH6 – independent potentiometer
#define PIN_PIR          27     // PIR digital output (HIGH = motion)
#define PIN_BUTTON       14     // Push button (active LOW, INPUT_PULLUP)
#define PIN_LED_ONBOARD   2     // Onboard LED (mode indicator)
#define PIN_LED_SPI       4     // SPI simulation LED

// Scheduler Timings
#define SENSOR_INTERVAL        5000UL
#define SPI_HEARTBEAT_INTERVAL 2000UL
#define DEBOUNCE_MS              10UL
#define SLEEP_TIMEOUT          30000UL

// EEPROM management
#define EEPROM_SIZE       4
#define EEPROM_SLEEP_ADDR 0

// Emulate Humidity using a relation weakly relating it to temperature for realism

#define HUM_DRIFT_AMP_PCT   8.0f    // ± % drift amplitude
#define HUM_PERIOD_MS    60000UL    // full sine period (60 s)
#define HUM_NOISE_AMP_PCT   1.0f    // ± % per-read jitter
#define HUM_MIN             10.0f   // lower clamp
#define HUM_MAX             95.0f   // upper clamp

// ─── BMP180 ───────────────────────────────────────────────────
Adafruit_BMP085 bmp;

// Modes of Operation
typedef enum {
    MODE_ACTIVE = 0,
    MODE_STANDBY,
    MODE_SLEEP
} SystemMode;

volatile SystemMode systemMode = MODE_ACTIVE;

// Booleans to define states
volatile bool     motionDetected = false;
volatile bool     buttonEvent    = false;
volatile bool     ledState       = false;
volatile uint32_t lastButtonTime = 0;

// Timers
uint32_t lastSensorTime = 0;
uint32_t lastSpiTime    = 0;
uint32_t lastMotionTime = 0;

// Sleep Counter
uint32_t sleepCount = 0;

// ════════════════════════════════════════════════════════════════
//  ISRs
// ════════════════════════════════════════════════════════════════

void IRAM_ATTR onMotion() {
    motionDetected = true;
    lastMotionTime = millis();
}

void IRAM_ATTR onButton() {
    uint32_t now = millis();
    if ((now - lastButtonTime) < DEBOUNCE_MS) return;
    lastButtonTime = now;

    bool pinHigh = (digitalRead(PIN_BUTTON) == HIGH);
    if (!pinHigh) {
        ledState = !ledState;
        digitalWrite(PIN_LED_ONBOARD, ledState ? HIGH : LOW);
        systemMode = MODE_STANDBY;
    } else {
        systemMode = MODE_ACTIVE;
    }
    buttonEvent = true;
}

// EEPROM emulation functions

uint32_t eepromReadU32(int addr) {
    uint32_t v = 0;
    EEPROM.get(addr, v);
    return v;
}

void eepromWriteU32(int addr, uint32_t v) {
    EEPROM.put(addr, v);
    EEPROM.commit();
}

// HTML Logger

void htmlLog(const char* level, const char* msg) {
    if      (strcmp(level, "INFO") == 0)
        Serial.printf("<span style='color:cyan'>[INFO] </span>%s\r\n",   msg);
    else if (strcmp(level, "WARN") == 0)
        Serial.printf("<span style='color:orange'>[WARN] </span>%s\r\n", msg);
    else if (strcmp(level, "ERR")  == 0)
        Serial.printf("<span style='color:red'>[ERR]  </span>%s\r\n",    msg);
    else
        Serial.printf("[%s] %s\r\n", level, msg);
}

// Software Humidity Logger (sourced externally)
/**
 * Produces a humidity value that looks like it comes from a real
 * sensor co-located with the BMP180:
 *
 *  1. Baseline: inverse relationship with temperature.
 *     Warmer air holds more moisture but relative humidity drops
 *     → base_RH = 75 - (tempC - 20) * 1.2
 *     (gives ~75 % at 20 °C, ~63 % at 30 °C — realistic indoors)
 *
 *  2. Slow drift: sinusoidal variation over HUM_PERIOD_MS to
 *     simulate gradual environmental changes.
 *
 *  3. Per-read noise: tiny jitter using the low bits of millis()
 *     XOR'd with the raw temperature integer — deterministic but
 *     looks random across readings.
 *
 *  4. Clamped to [HUM_MIN, HUM_MAX].
 *
 * @param tempC  Current temperature reading from BMP180 (°C)
 * @return       Emulated relative humidity (%)
 */
float emulateHumidity(float tempC) {
    // 1. Temperature-correlated baseline
    float base = 75.0f - (tempC - 20.0f) * 1.2f;

    // 2. Sinusoidal drift
    float phase = (float)(millis() % HUM_PERIOD_MS) / (float)HUM_PERIOD_MS;
    float drift = HUM_DRIFT_AMP_PCT * sinf(2.0f * M_PI * phase);

    // 3. Pseudo-random noise  (no stdlib rand() needed)
    uint32_t seed = millis() ^ (uint32_t)(tempC * 100);
    seed ^= (seed << 13); seed ^= (seed >> 17); seed ^= (seed << 5);
    float noise = HUM_NOISE_AMP_PCT * (((float)(seed & 0xFF) / 127.5f) - 1.0f);

    float rh = base + drift + noise;

    // 4. Clamp to physical limits
    if (rh < HUM_MIN) rh = HUM_MIN;
    if (rh > HUM_MAX) rh = HUM_MAX;

    return rh;
}

// Read Potentiometer
float readPotPercent() {
    return (analogRead(PIN_POT) / 4095.0f) * 100.0f;
}

// JSON Print
void printSensorJSON() {
    float   tempC    = bmp.readTemperature();
    int32_t pressPA  = bmp.readPressure();
    float   pressHpa = pressPA / 100.0f;
    float   altM     = bmp.readAltitude();
    float   humPct   = emulateHumidity(tempC);   // software-emulated
    float   potPct   = readPotPercent();          // independent ADC
    bool    motion   = (digitalRead(PIN_PIR) == HIGH);

    const char* modeStr =
        (systemMode == MODE_ACTIVE)  ? "ACTIVE"  :
        (systemMode == MODE_STANDBY) ? "STANDBY" : "SLEEP";

    Serial.printf(
        "{"
        "\"mode\":\"%s\","
        "\"temperature_c\":%.2f,"
        "\"humidity_pct\":%.1f,"
        "\"pressure_hpa\":%.2f,"
        "\"altitude_m\":%.1f,"
        "\"pot_pct\":%.1f,"
        "\"motion\":%s,"
        "\"uptime_ms\":%lu,"
        "\"sleep_count\":%lu"
        "}\r\n",
        modeStr,
        tempC,
        humPct,
        pressHpa,
        altM,
        potPct,
        motion ? "true" : "false",
        millis(),
        sleepCount
    );
}

// Simulated SPI heartbeat
void simulateSPIHeartbeat() {
    digitalWrite(PIN_LED_SPI, HIGH);
    delayMicroseconds(50);
    digitalWrite(PIN_LED_SPI, LOW);

    Serial.printf(
        "<span style='color:magenta'>[SPI-HB]</span> "
        "frame={cmd:0xA5,seq:%lu,ts:%lu}\r\n",
        (millis() / SPI_HEARTBEAT_INTERVAL),
        millis()
    );
}

// Mode Switching 

void enterSleepMode() {
    if (systemMode == MODE_SLEEP) return;
    systemMode = MODE_SLEEP;

    sleepCount++;
    eepromWriteU32(EEPROM_SLEEP_ADDR, sleepCount);

    htmlLog("WARN", "No motion for 30 s -> entering SLEEP mode");
    Serial.printf("[SLEEP] Cumulative sleep entries: %lu\r\n", sleepCount);

    for (int i = 0; i < 3; i++) {
        digitalWrite(PIN_LED_ONBOARD, HIGH); delay(150);
        digitalWrite(PIN_LED_ONBOARD, LOW);  delay(150);
    }
    ledState = false;
}

void wakeFromSleep() {
    systemMode     = MODE_ACTIVE;
    lastMotionTime = millis();

    htmlLog("INFO", "PIR motion detected -> waking SLEEP to ACTIVE");

    for (int i = 0; i < 2; i++) {
        digitalWrite(PIN_LED_ONBOARD, HIGH); delay(80);
        digitalWrite(PIN_LED_ONBOARD, LOW);  delay(80);
    }
}


void setup() {
    Serial.begin(115200);
    while (!Serial) delay(10);

    // GPIO
    pinMode(PIN_LED_ONBOARD, OUTPUT);
    pinMode(PIN_LED_SPI,     OUTPUT);
    pinMode(PIN_PIR,         INPUT);
    pinMode(PIN_BUTTON,      INPUT_PULLUP);
    analogSetAttenuation(ADC_11db);

    digitalWrite(PIN_LED_ONBOARD, LOW);
    digitalWrite(PIN_LED_SPI,     LOW);

    // BMP-180 (to emulate BME280 functionality)
    Wire.begin(PIN_SDA, PIN_SCL);

    if (!bmp.begin()) {
        htmlLog("ERR", "BMP180 not found – check SDA/SCL wiring");
        while (true) {
            digitalWrite(PIN_LED_ONBOARD, !digitalRead(PIN_LED_ONBOARD));
            delay(200);
        }
    }
    htmlLog("INFO", "BMP180 initialised OK (I2C 0x77)");
    htmlLog("INFO", "Humidity: software-emulated (temperature-correlated + drift)");

    // EEPROM
    EEPROM.begin(EEPROM_SIZE);
    sleepCount = eepromReadU32(EEPROM_SLEEP_ADDR);
    if (sleepCount == 0xFFFFFFFF) {
        sleepCount = 0;
        eepromWriteU32(EEPROM_SLEEP_ADDR, 0);
    }

    // Interrupts
    attachInterrupt(digitalPinToInterrupt(PIN_PIR),    onMotion, RISING);
    attachInterrupt(digitalPinToInterrupt(PIN_BUTTON), onButton, CHANGE);

    // ── Seed timers ─────────────────────────────────────────
    uint32_t now   = millis();
    lastSensorTime = now;
    lastSpiTime    = now;
    lastMotionTime = now;

    // Setup Log
    Serial.println("============================================");
    htmlLog("INFO", "ESP32 Environmental Monitor booted");
    Serial.printf("[BOOT] Stored sleep count  : %lu\r\n", sleepCount);
    Serial.println("[BOOT] Temp + Pressure     : BMP180 (I2C 0x77)");
    Serial.println("[BOOT] Humidity            : software-emulated");
    Serial.println("[BOOT] Potentiometer       : independent ADC (GPIO34)");
    Serial.println("[BOOT] System mode         : ACTIVE");
    Serial.println("============================================");
}

// ════════════════════════════════════════════════════════════════
//  loop()
// ════════════════════════════════════════════════════════════════
void loop() {
    uint32_t now = millis();

   // Detecting motion
    if (motionDetected) {
        motionDetected = false;
        if (systemMode == MODE_SLEEP) wakeFromSleep();
        else                          lastMotionTime = now;
    }

    // Button interrupt
    if (buttonEvent) {
        buttonEvent = false;
        const char* ms =
            (systemMode == MODE_ACTIVE)  ? "ACTIVE"  :
            (systemMode == MODE_STANDBY) ? "STANDBY" : "SLEEP";
        char buf[80];
        snprintf(buf, sizeof(buf), "Button edge -> mode: %s | LED: %s",
                 ms, ledState ? "ON" : "OFF");
        htmlLog("INFO", buf);
    }

    // Sleep Timeout
    if (systemMode == MODE_ACTIVE) {
        if ((now - lastMotionTime) >= SLEEP_TIMEOUT) enterSleepMode();
    }

    // Read data and print to json
    if (systemMode == MODE_ACTIVE) {
        if ((now - lastSensorTime) >= SENSOR_INTERVAL) {
            lastSensorTime = now;
            printSensorJSON();
        }
    }

    // SPI Heartbeat
    if (systemMode == MODE_ACTIVE) {
        if ((now - lastSpiTime) >= SPI_HEARTBEAT_INTERVAL) {
            lastSpiTime = now;
            simulateSPIHeartbeat();
        }
    }

    // Standby mode led blink
    if (systemMode == MODE_STANDBY) {
        static uint32_t lastBlink = 0;
        if ((now - lastBlink) >= 800) {
            lastBlink = now;
            ledState = !ledState;
            digitalWrite(PIN_LED_ONBOARD, ledState ? HIGH : LOW);
        }
    }

    // sleep setting
    if (systemMode == MODE_SLEEP) {
        static uint32_t lastPulse = 0;
        if ((now - lastPulse) >= 2000) {
            lastPulse = now;
            digitalWrite(PIN_LED_ONBOARD, HIGH);
            delay(20);
            digitalWrite(PIN_LED_ONBOARD, LOW);
        }
    }

    delay(1);
}