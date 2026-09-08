# ESP32-S3 High-Precision pH, DO, & Temperature Monitoring System

This project is an industrial-grade, multi-parameter water quality monitoring system built on the **ESP32-S3** microcontroller. It offers high-precision readings of **pH**, **Dissolved Oxygen (DO)**, and **Temperature** through advanced analog and digital interfaces, local graphic display rendering, and cloud connectivity.

---

## 1. Hardware Architecture & GPIO Pin Mapping

The system integrates several high-precision ICs and interfaces. Below is the complete physical GPIO routing table for the ESP32-S3:

| Peripheral / Interface | Function | ESP32-S3 Pin | Signal Direction & Characteristics |
| :--- | :--- | :---: | :--- |
| **CS1237 (pH ADC)** | **SCLK** | `GPIO 18` | Output (Software bit-bang clock) |
| | **DATA** | `GPIO 17` | Input (Software bit-bang data / DRDY detection) |
| **CS1237 (Temp ADC)** | **SCLK** | `GPIO 19` | Output (Software bit-bang clock) |
| | **DATA** | `GPIO 8` | Input (Software bit-bang data / DRDY detection) |
| **DS3231 RTC** | **SDA** | `GPIO 3` | Bidirectional I2C Data (with external pull-ups) |
| | **SCL** | `GPIO 20` | Output I2C Clock (with external pull-ups) |
| **Modbus Port 1 (Auxiliary)** | **TX** | `GPIO 40` | Output UART TX |
| | **RX** | `GPIO 38` | Input UART RX |
| | **DE** | `GPIO 39` | Output RS485 Driver Enable (active HIGH for Transmit) |
| **Modbus Port 2 (DO Sensor)** | **TX** | `GPIO 42` | Output UART TX |
| | **RX** | `GPIO 41` | Input UART RX |
| | **DE** | `GPIO 45` | Output RS485 Driver Enable (active HIGH for Transmit) |
| **ST7565R LCD (SPI)** | **CS** | `GPIO 12` | Output Chip Select (active LOW, software-controlled) |
| | **A0 / DC** | `GPIO 9` | Output Register Select (LOW: Command \| HIGH: Data) |
| | **SCL / CLK** | `GPIO 11` | Output Hardware SPI Clock (configured at 500 kHz) |
| | **SDA / MOSI** | `GPIO 10` | Output Hardware SPI MOSI (Data line to LCD) |
| | **RST / RES** | *Hardwired* | Connected to an external hardware RC reset circuit |
| **User Pushbuttons** | **ESC** | `GPIO 13` | Input (Active LOW, internal pull-up) - Back / Exit |
| | **DOWN** | `GPIO 14` | Input (Active LOW, internal pull-up) - Scroll Down / Decrease |
| | **UP** | `GPIO 21` | Input (Active LOW, internal pull-up) - Scroll Up / Increase |
| | **RIGHT** | `GPIO 47` | Input (Active LOW, internal pull-up) - Secondary Adjust |
| | **ENTER** | `GPIO 48` | Input (Active LOW, internal pull-up) - Confirm / Enter Menu |
| **LED Indicators** | **LED Ext** | `GPIO 41` | Output external board indicator LED |
| | **LED Status** | `GPIO 42` | Output onboard status LED |

---

## 2. Sensor Interfacing & Communication Protocols

### 2.1 pH & Temperature Acquisition via CS1237 24-bit ADC
The analog signals from the pH electrode and the temperature probe are digitized using two dedicated **CS1237** Sigma-Delta ADCs:
- **Communication Protocol**: Software-implemented bit-bang protocol (due to CS1237's custom clock/data sync interface). 
- **Data Conversion**: A 24-bit 2's complement raw signed integer is shifted out MSB-first.
- **Ready State (DRDY)**: The `DATA` pin is driven LOW by the CS1237 when conversion data is ready. The MCU polls this state before pulsing `SCLK`.
- **ADC Settings**: Configured via registers to **40Hz output rate**, **PGA Gain = 1**, and **Channel A** (differential input).
- **Physical Sensing Details**:
  - **pH Measurement**: Measures the differential voltage ($V_{probe}$) from the pH glass electrode relative to a stable reference point ($V_{pH\_ref\_actual} \approx 2.201\text{V}$). The signal is converted to mV using the ADC reference ($V_{ref} \approx 3.302\text{V}$) and then translated into pH units via temperature-compensated calibration curves.
  - **Temperature Measurement**: Utilizes a high-stability **PT1000** RTD sensor in a bridge configuration, powered by $V_{bridge} \approx 3.302\text{V}$ and balanced with a precision reference resistor ($R_{calib} = 725.0\,\Omega$). 

### 2.2 Dissolved Oxygen (DO) Modbus RTU Interface
- **Sensor Type**: Compatible with industrial Modbus DO sensors (such as the **KOG206** optical DO probe).
- **Communication Bus**: Modbus RTU over RS485 half-duplex. 
- **Transceiver Driver Control**: Uses an external RS485 transceiver. The `DE` (Driver Enable) pin is manually asserted HIGH before transmitting Modbus packets and de-asserted to LOW immediately after transmission is complete to return to receive mode.
- **Parameters Read**: Dissolved Oxygen concentration in $\text{mg/L}$ (or $\text{ppm}$), oxygen saturation percentage ($\%$), and internal sensor temperature ($\text{°C}$).
- **Modbus Port Parameters**: The baudrate (default: 9600), parity (none, even, odd), stop bits (1 or 2), and Modbus address (default: 5) are stored in NVS and can be adjusted through the local screen menu.

### 2.3 Real-Time Clock (DS3231)
- **Interface**: Hardware I2C.
- **Function**: Keeps track of time offline, providing real-time scheduling and datestamps for logs and screen displays. During startup, the system retrieves time from the DS3231 and synchronizes the ESP-IDF internal system epoch time. If Wi-Fi is available, NTP is utilized to automatically update the RTC.

---

## 3. Screen Rendering & UI Layouts

The system is equipped with a **GMG12864-06D** monochrome graphic LCD (128x64 pixels) controlled by the **ST7565R** driver:
- **SPI host**: Configured on the ESP32-S3 `SPI2_HOST` (FSPI) bus, running at 500 kHz to ensure signal integrity across cable lengths.
- **Framebuffer Architecture**: Built around a 1024-byte local RAM framebuffer (`fb[8][128]`). Drawing primitives (lines, rectangles, custom fonts) update the local buffer, and a complete display refresh occurs via an optimized DMA transfer of 132 columns (128 columns + 4-pixel column offset) across 8 pages.

The measurement screen features three visual layouts controlled by the user configuration:

1. **pH Screen Layout**:
   - **Main Display**: pH value rendered in a large scale (3x4 ratio).
   - **Sub-Info**: Measured temperature in $\text{°C}$ or $\text{°F}$, system date/time, and a "pH Electrode" label.
2. **Dissolved Oxygen (DO) Screen Layout**:
   - **Main Display**: DO concentration value in $\text{mg/L}$ rendered in a large scale (3x4 ratio).
   - **Sub-Info**: Dissolved Oxygen saturation percentage (e.g., `Sat: 98.2%`), sensor temperature, and system date/time.
3. **DUAL Screen Layout (pH + DO)**:
   - **Main Display**: Parallel display of pH and DO values stacked vertically in a medium scale (2x2 ratio).
   - **Sub-Info**: DO saturation percentage, temperature, and system date/time.

---

## 4. Hierarchical On-Screen Menu System

Pressing the `ENTER` button on the main measurement screen activates the hierarchical setup menu. Users navigate using `UP`/`DOWN` buttons, confirm with `ENTER`, and exit/go back with the `ESC` button.

The menu tree is structured as follows:

```
[Measurement Screen] ── ENTER ──► Main Menu
                                   ├── 1. System Settings
                                   │    ├── 1.1 Language (English / Vietnamese)
                                   │    ├── 1.2 Date & Time
                                   │    │    ├── Date Format (YYYY-MM-DD / DD-MM-YYYY / MM-DD-YYYY)
                                   │    │    └── Adjust Date & Time Settings
                                   │    └── 1.3 Screen Settings
                                   │         ├── Contrast Adjustment (0 to 63)
                                   │         └── Resistor Ratio Configuration (0 to 7)
                                   ├── 2. Sensor Settings
                                   │    ├── 2.1 Display Mode (pH / DO / DUAL)
                                   │    ├── 2.2 Calibration
                                   │    │    ├── pH Calibrate (2-point / 3-point options)
                                   │    │    ├── DO Calibrate (Zero / Slope options)
                                   │    │    └── Temperature Calibrate (Offset offset adjustment)
                                   │    ├── 2.3 Digital Filter (Low / Medium / High filter lengths)
                                   │    ├── 2.4 Temperature Mode (ATC °C / MTC °C / ATC °F / MTC °F)
                                   │    ├── 2.5 Temp Settings (Manual value & Temp offset)
                                   │    └── 2.6 Temperature Linear Compensation (Coeff. adjustment)
                                   └── 3. Modbus Settings
                                        ├── 3.1 Modbus Port 1 (Addr, Baud, Parity, Stopbits)
                                        └── 3.2 Modbus Port 2 (Addr, Baud, Parity, Stopbits)
```

### 4.1 Calibration Implementation
- **pH Calibration**: Supports 2-point or 3-point calibration using standard buffer solutions (pH 4.00, 6.86/7.00, and 9.18/10.00). It employs Automatic Temperature Compensation (ATC) to calculate calibration slopes based on standard solution characteristics at current temperatures.
- **DO Calibration**: Supports offline calibration of the KOG206 probe:
  - **Zero Calibration**: Calibrates the sensor output in oxygen-free water (0% DO).
  - **Slope Calibration**: Calibrates the sensor in air-saturated water or ambient air (100% DO).
- **Temperature Offset**: Allows manual compensation by entering an offset value to align the RTD readings with a reference thermometer.

---

## 5. Web Config Portal & Cloud Integration

### 5.1 Local Web Portal
Upon startup, the ESP32-S3 starts a Wi-Fi Access Point and runs a local HTTP Web Server. By connecting to the device AP, users can open a comprehensive Web Config Portal:
- **Wi-Fi Manager**: Scan for local networks, input Wi-Fi credentials, and monitor connection status.
- **Real-Time Data**: View live sensor telemetry (pH, DO, temperature, electrode voltage).
- **Remote Display & Key Emulation**: An interactive virtual LCD screen and virtual navigation keypad, allowing users to control the device's physical menu remotely.
- **Calibration Settings**: Perform calibration and view raw coefficients.
- **Modbus Configurations**: Set parameters for both serial ports.

### 5.2 Azure IoT Hub Integration
Once connected to the internet, a dedicated telemetry task connects to **Azure IoT Hub** using the SAS token authentication protocol:
- **Telemetry Upload**: Publishes real-time water metrics (pH, DO, temperature, error codes) as JSON payloads.
- **Direct Methods & Desired Properties**: Allows remote control of system configurations, time syncing, and remote calibration triggers.
- **OTA Updates**: Supports firmware Over-The-Air (OTA) updates pushed securely from the cloud.

### 5.3 Storage Architecture
- **NVS (Non-Volatile Storage)**: Stores volatile Wi-Fi credentials, serial port settings (baud, parity, slave IDs), temperature mode configurations, and calibration curves.
- **FRAM (Ferroelectric RAM)**: Integrates an external I2C FRAM chip for fast, cyclic, non-volatile storage of schedules, alarm triggers, and runtime diagnostics without flash wearing.
