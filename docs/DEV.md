# Developer Guide

This guide covers building the firmware from source and advanced configuration options.

It is the authoritative command catalog and code-documentation policy. Status terms are defined in `AGENTS.md`; a command shown here is not verified merely because it exists, and compilation is not hardware validation. In the 2026-07-28 adoption environment, GitHub/internet access and ESP-IDF were unavailable, no board/serial device was supplied, firmware/flash/hardware checks were **environment-limited**, and dependency-fetching tests were also **environment-limited**.

## Software Requirements

- ESP-IDF v6.0 or later
- Python 3.7+ (for build tools)
- npm, vite (note: this excludes use under Wayland)
- ESP Component Manager (comes with ESP-IDF)

## Building from Source

### 1. Set up ESP-IDF

```bash
# Source the ESP-IDF environment
cd <path to esp-idf>
. ./export.sh
```

### 2. Build the Project

We provide a `build.py` helper script that handles configuration and building for different boards.

```bash
cd <path to photoframe-api>

# install npm dependencies
cd webapp
npm install
cd ..

# Build for Waveshare PhotoPainter (7.3" 7-color e-paper)
./build.py --board waveshare_photopainter_73

# Build for Seeed Studio XIAO EE02 (13.3" e-paper)
./build.py --board seeedstudio_xiao_ee02

# Build for Seeed Studio XIAO EE04 (7.3" 6-color e-paper)
./build.py --board seeedstudio_xiao_ee04

# Build for Seeed Studio reTerminal E1002 (7.3" 6-color e-paper)
./build.py --board seeedstudio_reterminal_e1002

# Build for Seeed Studio reTerminal E1004 (13.3" 6-color e-paper)
./build.py --board seeedstudio_reterminal_e1004

# Build for Seeed Studio reTerminal E1003 (10.3" grayscale e-paper)
./build.py --board seeedstudio_reterminal_e1003

# Clean build (optional)
./build.py --board waveshare_photopainter_73 --fullclean
```

The script automatically:
1. Builds the frontend webapp (`webapp/`)
2. Sets the correct `sdkconfig.defaults` for the selected board
3. Runs `idf.py build` OR `idf.py build` with correct options

### 3. Flash and Monitor

The project uses ESP Component Manager to automatically download the `esp_jpeg` component during the first build.

```bash
# Flash to device (replace PORT with your serial port, e.g., /dev/cu.usbserial-*)
idf.py -p PORT flash

# Monitor output
idf.py -p PORT monitor

# Flash and monitor in one go
idf.py -p PORT flash monitor
```

**Note:** On the first build, ESP-IDF will automatically download the `esp_jpeg` component from the component registry. This requires an internet connection.

## Configuration Options

Edit `main/config.h` to customize firmware behavior:

```c
#define AUTO_SLEEP_TIMEOUT_SEC      120          // Auto-sleep timeout (2 minutes)
#define DEFAULT_ROTATE_CRON         "0 */12 *"       // Default rotation schedule (every 12h)
#define DISPLAY_WIDTH               800           // E-paper width
#define DISPLAY_HEIGHT              480    // E-paper height
```

### Key Configuration Parameters

- **AUTO_SLEEP_TIMEOUT_SEC**: Time in seconds before the device enters deep sleep when idle
- **DEFAULT_ROTATE_CRON**: Default rotation schedule (cron) for fresh devices, configurable via the web interface
- **DISPLAY_WIDTH/HEIGHT**: E-paper display dimensions (800×480 for landscape)

## Development Workflow

### Serial Monitor

Monitor device logs in real-time:

```bash
idf.py -p PORT monitor
```

Press `Ctrl+]` to exit the monitor.

## Verification and review command catalog

These commands are derived from current local scripts/workflows:

| Area | Command | Classification at adoption |
|---|---|---|
| Formatting | `make format-check` | CI-verified historically; not run here because it may invoke `npm ci` |
| Host/CLI tests | `make test` | CI-verified historically; environment-limited here (GoogleTest/dependency fetch and generated build directory) |
| Direct host tests | `cmake -S host_tests -B host_tests/build && cmake --build host_tests/build && ctest --test-dir host_tests/build --output-on-failure` | documented target; environment-limited here |
| CLI tests | `cd process-cli && npm test` | documented from package scripts; unknown locally |
| Web tests | `cd webapp && npm test` | documented from package scripts; unknown locally |
| Web lint | `cd webapp && npm run lint:check` | documented from package scripts; unknown locally |
| Review | `git diff --check` | verified locally for the governance bootstrap |
| Focused review | `git diff -- README.md CHANGELOG.md AGENTS.md docs/` | verified locally for scope inspection |
| Firmware | `python3 build.py --board BOARD` | CI-verified historically for the six IDs below; environment-limited here |
| Flash/monitor | `idf.py -p PORT flash monitor` | hardware-only; environment-limited here |
| Hardware | follow `docs/VALIDATION.md` matrix and record observations | pending hardware validation |

Board build IDs are `waveshare_photopainter_73`, `seeedstudio_xiao_ee02`, `seeedstudio_xiao_ee04`, `seeedstudio_reterminal_e1002`, `seeedstudio_reterminal_e1003`, and `seeedstudio_reterminal_e1004`. Use `python3 build.py --board ID` for each. Documentation checks include verifying routed paths exist, checking local relative links, searching for current/target contradictions, and confirming the diff touches documentation only.

## Code-documentation policy

Document public-header contracts, state-machine transitions, task/concurrency ownership, buffer ownership/lifetimes, persistence keys and migration, failure/retry behavior, security-sensitive parsing and redaction, GPIO polarity and evidence, board-specific exceptions, and non-obvious power/wake rationale. Comments must distinguish source-derived assumptions from physical validation; do not present speculative hardware behavior as fact. Update architecture, hardware, operations, testing, decisions, validation, and changelog whenever their contracts are affected.

### Erase Flash

To completely reset the device (including WiFi credentials):

```bash
idf.py erase-flash
```

### Finding Serial Port

**macOS:**
```bash
ls /dev/cu.*
```

**Linux:**
```bash
ls /dev/ttyUSB*
```

**Windows:**
Check Device Manager for COM ports.

## Project Structure

```
esp32-photoframe/
├── main/
│   ├── main.c                 # Entry point
│   ├── config.h               # Configuration
│   ├── display_manager.c      # E-paper display control
│   ├── http_server.c          # Web server and API
│   ├── image_processor.c      # Image processing (dithering, tone mapping)
│   ├── power_manager.c        # Sleep/wake management
│   └── webapp/                # Web interface files
├── components/
│   └── epaper_src/            # E-paper driver
├── process-cli/               # Node.js CLI tool
└── docs/                      # Demo page
```

## Debugging

### Enable Verbose Logging

In `idf.py menuconfig`:
1. Navigate to `Component config` → `Log output`
2. Set default log level to `Debug` or `Verbose`

### Common Issues

**Build fails with component errors:**
- Ensure ESP Component Manager is up to date
- Delete `managed_components/` and rebuild

**Flash fails:**
- Check USB cable connection
- Try a different USB port
- Reduce baud rate: `idf.py -p PORT -b 115200 flash`

**Device not responding:**
- Press and hold BOOT button while connecting USB
- Try erasing flash: `idf.py erase-flash`
