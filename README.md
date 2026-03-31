# ESP32 Environmental Monitor

A multi-sensor environmental monitoring system built on the ESP32, simulated in **[Wokwi](https://wokwi.com)**. Features real-time temperature, pressure, altitude, and emulated humidity readings, alongside potentiometer input, PIR-based motion detection, and a three-mode power management system.

---

## Features

- **BMP180 sensor** — temperature, barometric pressure, and altitude via I²C
- **Software-emulated humidity** — temperature-correlated baseline with sinusoidal drift and per-read noise for realistic RH values
- **Potentiometer input** — independent ADC reading expressed as a percentage
- **PIR motion detection** — interrupt-driven, used for wakeup and activity tracking
- **Push button** — debounced interrupt for manual mode switching and LED toggle
- **Three operating modes** — `ACTIVE`, `STANDBY`, and `SLEEP` with automatic sleep timeout
- **Simulated SPI heartbeat** — periodic framed output on a dedicated LED pin
- **Persistent sleep counter** — stored in EEPROM across power cycles
- **HTML-tagged serial logging** — colour-coded `INFO`, `WARN`, and `ERR` messages
- **JSON sensor output** — structured telemetry printed on a configurable interval

---

## Simulation Environment

This project runs on the **Wokwi ESP32 simulator**. No physical hardware is required.

Open the project at [wokwi.com](https://wokwi.com), create a new ESP32 project, paste in the zip file content like the .ino file and the diagram.json

### Wokwi-specific notes

- **BMP180** is natively supported in Wokwi — the I²C connection works out of the box.
- **Potentiometer** — connect Wokwi's built-in potentiometer component; the wiper connects directly to GPIO 34.
- **EEPROM persistence** — the sleep counter may reset between simulation runs depending on your Wokwi project settings. This is expected behaviour in simulation.
- **HTML-tagged serial output** — Wokwi's serial monitor displays raw text, so `<span style='color:cyan'>` tags will appear as-is rather than rendering as colour. This is cosmetic only and does not affect functionality.

### diagram.json

Make the following connections to your Wokwi `diagram.json`:

## Connections
 
### Serial Monitor
| From | To | Notes |
|---|---|---|
| `esp:TX` | `$serialMonitor:RX` | UART output to Wokwi serial terminal |
| `esp:RX` | `$serialMonitor:TX` | UART input from Wokwi serial terminal |
 
### SPI Heartbeat LED (GPIO 4)
| From | To | Colour | Notes |
|---|---|---|---|
| `esp:4` | `r1:1` | Green | GPIO 4 → 1 kΩ resistor |
| `r1:2` | `led1:A` | Green | Resistor → LED anode |
| `led1:C` | `esp:GND.2` | Black | LED cathode → GND |
 
### BMP180 (I²C — GPIO 21/22)
| From | To | Colour | Notes |
|---|---|---|---|
| `bmp1:SDA` | `esp:21` | Green | I²C data |
| `bmp1:SCL` | `esp:22` | Green | I²C clock |
| `bmp1:3.3V` | `esp:3V3` | Red | Power |
| `bmp1:GND` | `esp:GND.2` | Black | Ground |
 
### Potentiometer (ADC — GPIO 34)
| From | To | Colour | Notes |
|---|---|---|---|
| `pot1:SIG` | `esp:34` | Green | Wiper → ADC1 CH6 |
| `pot1:VCC` | `esp:3V3` | Red | Power |
| `pot1:GND` | `esp:GND.2` | Black | Ground |
 
### PIR Motion Sensor (GPIO 27)
| From | To | Colour | Notes |
|---|---|---|---|
| `pir1:OUT` | `esp:27` | Green | Digital motion signal |
| `pir1:VCC` | `esp:3V3` | Red | Power |
| `pir1:GND` | `esp:GND.2` | Black | Ground |
 
### Push Button (GPIO 14)
| From | To | Colour | Notes |
|---|---|---|---|
| `btn1:2.r` | `esp:14` | Green | Button → GPIO 14 (INPUT_PULLUP) |
| `btn1:1.l` | `pir1:GND` | Black | Shared GND via PIR ground rail |
 
---

> **Note:** Wokwi uses the `wokwi-bmp280` part to simulate the BMP180 — the Adafruit BMP085 library is compatible with both.

---

## Pin Assignments

| Signal | GPIO |
|---|---|
| I²C SDA | 21 |
| I²C SCL | 22 |
| Potentiometer (ADC) | 34 |
| PIR output (simulated via button) | 27 |
| Push button | 14 |
| Onboard LED | 2 |
| SPI heartbeat LED | 4 |

---

## Software Dependencies

Add via the Wokwi library manager (`libraries.txt`) or PlatformIO:

| Library | Purpose |
|---|---|
| `Adafruit BMP085 Library` | BMP180 driver (BMP085-compatible API) |
| `Wire` | I²C (built-in) |
| `EEPROM` | Persistent storage (built-in for ESP32) |

**Wokwi `libraries.txt`:**

```
Adafruit BMP085 Library
```

**PlatformIO `platformio.ini`:**

```ini
[env:esp32dev]
platform  = espressif32
board     = esp32dev
framework = arduino
lib_deps  =
    adafruit/Adafruit BMP085 Library
```

---

## Operating Modes

| Mode | Description | LED behaviour |
|---|---|---|
| `ACTIVE` | Full operation — sensors polled, SPI heartbeat running | Controlled by button toggle |
| `STANDBY` | Sensors and SPI paused; entered on button press | Slow blink every 800 ms |
| `SLEEP` | Entered after 30 s of no PIR activity | Short 20 ms pulse every 2 s |

### Mode Transitions

```
         Button press (falling edge)
ACTIVE ──────────────────────────────► STANDBY
  ▲                                       │
  │   Button release (rising edge)        │
  └───────────────────────────────────────┘

         No motion for 30 s
ACTIVE ──────────────────────────────► SLEEP
  ▲                                       │
  │   PIR Trigered                        │
  └───────────────────────────────────────┘
```

In Wokwi, trigger the **PIR Sensor** (GPIO 27) to simulate a motion event and wake the system from `SLEEP`.

---

## Serial Output

Monitor at **115 200 baud** using Wokwi's built-in serial terminal. Two output formats are produced:

### JSON Telemetry (every 5 s in ACTIVE mode)

```json
{
  "mode": "ACTIVE",
  "temperature_c": 27.40,
  "humidity_pct": 68.3,
  "pressure_hpa": 1012.34,
  "altitude_m": 12.5,
  "pot_pct": 45.2,
  "motion": false,
  "uptime_ms": 15000,
  "sleep_count": 2
}
```

### Log Messages

```
<span style='color:cyan'>[INFO] </span>BMP180 initialised OK (I2C 0x77)
<span style='color:orange'>[WARN] </span>No motion for 30 s -> entering SLEEP mode
<span style='color:magenta'>[SPI-HB]</span> frame={cmd:0xA5,seq:7,ts:14000}
```

HTML tags appear as raw text in Wokwi's serial monitor — this is expected. The tags are intended for browser-based serial dashboards outside of Wokwi.

---

## Humidity Emulation

Because the BMP180 does not measure humidity, a software model is used:

1. **Temperature-correlated baseline** — RH decreases as temperature rises, approximating indoor conditions (~75 % at 20 °C, ~63 % at 30 °C).
2. **Sinusoidal drift** — ±8 % amplitude over a 60-second period, simulating gradual environmental change.
3. **Per-read noise** — ±1 % jitter derived from a deterministic XOR-shift hash of `millis()` and the current temperature integer.
4. **Clamped** to the range [10 %, 95 %].

This is clearly labelled in all log output and the boot message.

---

## Configuration

All key timings and limits are defined as macros at the top of the source file:

| Macro | Default | Description |
|---|---|---|
| `SENSOR_INTERVAL` | `5000` ms | Sensor read and JSON print period |
| `SPI_HEARTBEAT_INTERVAL` | `2000` ms | Simulated SPI frame period |
| `DEBOUNCE_MS` | `10` ms | Button debounce window |
| `SLEEP_TIMEOUT` | `30000` ms | Inactivity period before entering SLEEP |
| `HUM_DRIFT_AMP_PCT` | `8.0` % | Humidity sine-drift amplitude |
| `HUM_PERIOD_MS` | `60000` ms | Humidity drift period |
| `HUM_NOISE_AMP_PCT` | `1.0` % | Per-read humidity jitter amplitude |
| `HUM_MIN` / `HUM_MAX` | `10` / `95` % | Humidity clamp limits |

---

## EEPROM

A single `uint32_t` sleep counter is stored at address `0` (4 bytes). It increments each time the system enters `SLEEP` mode. On first boot with a blank EEPROM (`0xFFFFFFFF`), the counter is initialised to `0`.

In Wokwi, EEPROM state may not persist between simulation runs — the counter resetting to `0` on restart is normal.

---

## Troubleshooting

| Symptom | Likely cause | Fix |
|---|---|---|
| Onboard LED blinks rapidly at boot | BMP180 not found | Check SDA/SCL connections in `diagram.json`; verify I²C address is `0x77` |
| No JSON output | Not in `ACTIVE` mode | Press and release the mode button (GPIO 14) to return to `ACTIVE` |
| System never enters `SLEEP` | Motion being detected | Ensure the PIR button (GPIO 27) is not held down |
| `humidity_pct` looks stable | Expected — software model | Values drift slowly over the 60 s sine period |
| `sleep_count` resets to `0` on restart | Wokwi EEPROM not persistent | Expected in simulation; persists on real hardware |
| ADC reading always `0` | Potentiometer not wired | Check `pot:SIG → esp:34` connection in `diagram.json` |
| HTML tags visible in serial output | Wokwi renders plain text | Expected — tags are for external dashboards only |

---

## License

MIT — see `LICENSE` for details.
