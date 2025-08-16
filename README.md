# Touch-to-Pixel Latency Profiler (Linux, C++)

A lightweight Linux tool to **measure input event latency** from kernel timestamps to user-space receipt time.
This project is designed to analyze **touch-to-pixel latency**, a critical performance factor in interactive systems such as **keyboards, touchpads, and touchscreens**.

It demonstrates **Linux systems programming**, low-level input event handling, and latency optimization — directly applicable to **embedded systems and performance engineering** roles.

---

## ✨ Features

* Directly reads raw input events from `/dev/input/event*` (Linux evdev).
* Measures **end-to-end latency**:

  * Kernel event timestamp → user-space receipt time.
* Reports key performance metrics:

  * **Average latency**
  * **Median (p50), p95, and p99 latency**
* Works with all input devices:

  * Keyboard
  * Mouse
  * Touchpad
  * Touchscreen
* Written in **modern C++17**, dependency-free.
* Provides **rolling statistics** during execution and a **final summary** on exit.

---

## 📂 Project Structure

```
.
├── input_latency.cpp    # Source code
├── README.md            # Documentation
└── .gitignore           # Ignore build artifacts
```

---

## 📦 Build Instructions

Ensure you have a C++17-capable compiler installed (e.g., g++ 9+).

```bash
g++ -O2 -std=c++17 -Wall -Wextra -o input_latency input_latency.cpp
```

---

## 🚀 Usage

### 1. Identify your input device

List input devices:

```bash
ls /dev/input/event*
```

Or use `libinput` (if installed):

```bash
sudo libinput list-devices
```

Example device path: `/dev/input/event3`

---

### 2. Run the profiler (requires root access)

```bash
sudo ./input_latency /dev/input/event3 --limit 500
```

Options:

* `--limit N` → stop after N events
* `--quiet` → suppress per-event logs, only print summaries

---

### 3. Interact with the device

* Press keys on the **keyboard**
* Move your finger on the **touchpad**
* Tap or swipe the **touchscreen**

Events will be captured and latencies calculated.

---

### 4. Stop with Ctrl-C

The tool prints a **final latency summary** when interrupted.

---

## 📝 Example Output

```
Device: /dev/input/event3 ("AT Translated Set 2 keyboard")
Collecting input events… Press Ctrl-C to stop.
[KEY] code=30 val=1  latency=48.21 us
[KEY] code=30 val=0  latency=50.37 us
...
=== Rolling Latency Stats (usec) over 50 events ===
avg: 52.14   p50: 49.00   p95: 63.50   p99: 75.00

=== Final Latency Stats (usec) over 500 events ===
avg: 51.87   p50: 49.12   p95: 62.88   p99: 74.32
Total events seen: 720 | Latencies measured: 500
```

---

## ⚙️ Technical Details

* **Input Source:**
  Uses Linux **evdev** interface (`/dev/input/event*`).

* **Event Structure:**
  Reads `struct input_event` from `<linux/input.h>`.

* **Clock Handling:**

  * Attempts to set timestamps to **CLOCK\_MONOTONIC** (`EVIOCSCLOCKID`) for consistency.
  * Falls back gracefully if unsupported.

* **Latency Measurement:**

  ```
  latency = clock_gettime(CLOCK_MONOTONIC) - input_event.time
  ```

  * Kernel timestamp (`input_event.time`) converted to nanoseconds.
  * Compared against current time in user-space.

* **Performance Stats:**

  * Average
  * Percentiles (p50, p95, p99)
  * Computed over rolling window + final results.

* **Robustness:**

  * Non-blocking file descriptors (`O_NONBLOCK`).
  * Efficient polling via `poll()`.
  * Graceful signal handling (Ctrl-C → summary).

---

## 🔮 Possible Extensions

* Export latency results to **CSV/JSON** for visualization.
* Real-time charts with **gnuplot** or **Python matplotlib**.
* Filter by specific event types (`EV_KEY`, `EV_ABS`).
* Support multiple devices concurrently.
* Integrate with a simple **UI** to visualize responsiveness impact.
