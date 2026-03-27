# Smart Dam — IoT System for River Hydrometric Monitoring and Dam Control

Smart Dam is a distributed IoT system implementing a simplified version of an intelligent platform for monitoring the hydrometric level of a river and controlling a dam. The system is composed of five main components that communicate with each other over different protocols: HTTP, serial, and Bluetooth.

---

## System Architecture

```
┌──────────────────────┐         HTTP          ┌──────────────────────────┐
│  Remote Hydrometer   │ ───────────────────►  │                          │
│    (ESP32 – C++)     │                        │       Dam Service        │
└──────────────────────┘                        │   (Backend Java/Vert.x)  │
                                                │                          │
┌──────────────────────┐        Serial          │                          │
│   Dam Controller     │ ◄────────────────────► │                          │
│  (Arduino UNO – C++) │                        └──────────────────────────┘
│                      │                                    ▲
│                      │ ◄──── Bluetooth ────► Dam App      │  HTTP
│                      │       (Android)                    ▼
└──────────────────────┘                        ┌──────────────────────────┐
                                                │      Dam Dashboard       │
                                                │   (Desktop Java/JavaFX)  │
                                                └──────────────────────────┘
```

---

## Components

### 1. Remote Hydrometer — `dam_remotehyd` (RH)
**Platform:** ESP32 (AZ-Delivery DevKit V4)  
**Language:** C++ (Arduino framework, via PlatformIO)  
**Main libraries:** `ArduinoJson`, `WiFi`, `HTTPClient`, `Ticker`

The Remote Hydrometer is the field sensor of the system. Deployed near the river, it periodically measures the hydrometric level using an **ultrasonic sonar sensor** and reports data to the Dam Service via **HTTP REST**.

#### State Logic

The firmware implements a three-state machine:

| State | Condition (sonar distance) | LED Behaviour |
|---|---|---|
| `NORMAL` | distance > 20 cm | Off |
| `PRE_ALARM` | 12 cm < distance ≤ 20 cm | Pulsing |
| `ALARM` | distance ≤ 12 cm | Solid on |

Internal timers (based on `Ticker`) control the message sending frequency: more frequent during alarm, less frequent in normal conditions.

#### Communication

On startup, the ESP32 connects to the WiFi network and sends its timing parameters (`prealarm_time`, `alarm_time`) to the Dam Service. Afterwards, depending on the detected state, it sends JSON payloads via `POST /api/data` containing:
- `type`: `"data"` (with distance and state) or `"state"` (state only)
- `distance`: value measured by the sonar
- `state`: 0 (Normal), 1 (PreAlarm), 2 (Alarm)

---

### 2. Dam Service — `dam_service` (DS)
**Platform:** PC / Server  
**Language:** Java  
**Framework:** [Vert.x 4](https://vertx.io/) (reactive Verticle architecture)  
**Build tool:** Gradle (Kotlin DSL)  
**Main libraries:** `vertx-web`, `vertx-mysql-client`, `jssc` (serial comm), `Gson`

The Dam Service is the brain of the system: it acts as the central control unit coordinating all other components. It follows an MVC pattern built on top of a reactive Vert.x Verticle architecture.

#### Internal Structure

- **`MainController`** — application entry point; instantiates and starts all Verticles.
- **`InternetVerticle`** — HTTP server on port `8080`, handles incoming requests from the RH and the Dashboard.
- **`SerialVerticle`** — manages serial communication with the Dam Controller (Arduino) via the `jssc` library.
- **`ModelImpl`** — business logic: receives data from the RH, computes the dam opening based on the measured distance, updates the global state, and notifies the Arduino over serial.
- **`DatabaseConnection`** — connects to a MySQL database (via `vertx-mysql-client`) for hydrometric data persistence.

#### REST API

| Method | Endpoint | Description |
|---|---|---|
| `POST` | `/api/data` | Receives data from the Remote Hydrometer (state, distance, timing params) |
| `GET` | `/api/data` | Returns the current system state (state, level, dam opening, manual mode flag) |
| `GET` | `/api/data/times` | Returns the update timing parameters |
| `GET` | `/api/data/levels` | Returns the last 20 hydrometric readings from the DB |

#### Dam Opening Computation

The model automatically computes the dam opening percentage based on the measured distance (cm):

| Distance (cm) | Dam Opening |
|---|---|
| 10–12 | 20% |
| 8–10 | 40% |
| 6–8 | 60% |
| 4–6 | 80% |
| ≤ 4 | 100% |

---

### 3. Dam Controller — `dam-controller` (DC)
**Platform:** Arduino UNO  
**Language:** C++ (Arduino framework, via PlatformIO)  
**Main libraries:** `ArduinoJson`, `SoftwareSerial`, `ServoTimer2`, `TimerOne`

The Dam Controller is the embedded system that physically operates the dam. It receives commands from the Dam Service over **serial** and from the Dam App over **Bluetooth** (HC-05 module or compatible).

#### Firmware Structure

The firmware is built around an **asynchronous state machine** (`MyAsyncFSM`) that reacts to events coming from two communication channels:

- **`MsgSerialService`** — listens to messages from the Dam Service on the serial port. Produces `AlarmEvent`, `PreAlarmEvent`, `NormalEvent`.
- **`MsgBtService`** — listens to commands from the mobile app over Bluetooth. Produces `BtManualMsgEvent`, `BtNoManualMsgEvent`, `DamOpenMsgEvent`.

#### Operating Modes

- **Automatic** (default): the Dam Controller receives the dam opening value from the Service and drives the **servo motor** accordingly. An LED blinks during alarm state.
- **Manual**: activatable by an operator via the Dam App. In this mode, the Arduino ignores automatic commands from the Service and lets the operator control the opening directly from the app. A solid LED indicates that manual mode is active.

On every state change, the Controller sends an update to the Dam App over Bluetooth as a JSON message containing `state`, `damOpening`, and `distance`.

---

### 4. Dam App — `dam_mobileapp` (DM)
**Platform:** Android  
**Language:** Java (Android SDK)  
**Build tool:** Gradle  
**Structure:** two Android modules — `bt-lib` (reusable Bluetooth library) and `bt-client` (the app itself)

The Dam App is designed for field operators physically near the dam. It allows real-time monitoring of the system state and direct manual control of the dam.

#### Features

- **Bluetooth connection** to the Dam Controller (by searching for a paired device by name).
- **Live state display**: `NORMAL`, `PREALARM`, `ALARM`.
- **Hydrometric level display** as last detected by the sonar.
- **Manual control**: a button toggles manual mode on/off (only available when state is `ALARM`). A `SeekBar` (0–100%) lets the operator set the dam servo opening in real time.

#### Bluetooth Library (`bt-lib`)

The `bt-lib` module abstracts Bluetooth communication behind clean interfaces (`BluetoothChannel`, `CommChannel`) and async tasks (`ConnectToBluetoothServerTask`, `RealBluetoothChannel`), preventing the UI thread from blocking during connection setup and message reception.

---

### 5. Dam Dashboard — `dam_dashboard` (DD)
**Platform:** Desktop (Windows / macOS / Linux)  
**Language:** Java  
**UI Framework:** JavaFX 13 (FXML for layout)  
**Async framework:** Vert.x 4 (non-blocking HTTP client towards the Service)  
**Build tool:** Gradle (Kotlin DSL)

The Dam Dashboard is a desktop application for centralised system monitoring. It periodically polls the Dam Service via HTTP and displays data in a graphical interface.

#### GUI

The UI layout is defined in FXML (`MainScene.fxml`) and the JavaFX controller (`MainSceneController`) updates the interface safely from the UI thread via `Platform.runLater`. The interface shows:

- **Current system state** (NORMAL / PRE_ALARM / ALARM).
- **Current hydrometric level**.
- **Operating mode** (automatic or manual) and dam opening percentage.
- **Line chart** with a sliding window of the last 20 samples. When the state transitions from NORMAL to PRE_ALARM, the dashboard fetches the last 10 historical data points from the Service database to provide chart context from before the alert began.

---

## Communication Protocols

| Link | Protocol | Details |
|---|---|---|
| RH → DS | HTTP REST | `POST /api/data` with JSON payload |
| DS → DC | Serial (UART) | JSON messages over COM port |
| DC → DS | Serial (UART) | State feedback (manual mode acknowledgement) |
| DC ↔ DM | Bluetooth SPP | JSON for state/opening updates; plain strings for commands |
| DD → DS | HTTP REST | `GET /api/data`, `GET /api/data/levels` |
