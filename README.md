# gapaNEXT

GPU Gamma Ramp Switcher — a Windows desktop tool to control and fine-tune display gamma calibration.

## Features

- **Gamma ramp control** via Windows GDI32 (`SetDeviceGammaRamp` / `GetDeviceGammaRamp`)
- **Web UI** (SvelteKit) served by a built-in embedded HTTP server on port 9980
- **Optional REPL CLI** for command-line gamma management
- **System tray** integration for quick access and background running
- **Configurable hotkeys** defined in `gapa.json`
- **Multi-display** support — apply/reset per display independently
- **Preset files** (`.gmd`) — import, edit, and save gamma curve presets

## Quick Start

### Prerequisites

- Windows 10+
- Visual Studio 2022 with MSVC (C++20)
- CMake 3.10+
- Node.js (for building the web UI)

### Build

```sh
cmake --preset default
cmake --build build --config Release
```

The CMake build automatically installs npm dependencies, builds the SvelteKit frontend, and copies it to `dist/`.

### Run

```sh
# Start web UI server (port 9980)
build\Release\gapaNEXT.exe

# Custom port
build\Release\gapaNEXT.exe 8080

# With REPL CLI (requires GAPA_REPL=ON)
build\Release\gapaNEXT.exe
```

Open `http://localhost:9980` in a browser to access the web UI.

## REPL Mode

Enable with `cmake -DGAPA_REPL=ON`. Provides an interactive CLI:

```
gapaNEXT> help
  apply, a <preset> [D]   Apply named preset to display D (0=all)
  reset, r [D]            Reset display D to linear gamma (0=all)
  list, l                 List available displays and presets
  status, st              Show current gamma per display
  exit, quit, q           Safe exit (reset gamma + release)
```

## Configuration

### `gapa.json`

Keyboard shortcuts and actions stored alongside the executable:

```json
{
  "keybindings": [
    { "action": "apply_preset", "key": ["f11"], "display": [0], "preset": "shadowboost.gmd" },
    { "action": "reset_display", "key": ["f10"], "display": [0] },
    { "action": "reset_all",  "key": ["shift", "alt", "f12"] }
  ]
}
```

### Gamma Presets (`.gmd`)

Gamma curve description files stored in the `gamma/` directory. Each file defines red/green/blue control points that are interpolated into a 256-entry 16-bit gamma ramp. Manage presets from the web UI or REPL.

## Tech Stack

| Layer       | Technology                            |
|-------------|---------------------------------------|
| Application | C++20 (MSVC)                          |
| Build       | CMake                                 |
| HTTP Server | [Crow](https://github.com/CrowCpp/Crow) |
| Async I/O   | [ASIO](https://think-async.com/Asio/) |
| Frontend    | [SvelteKit](https://svelte.dev/)      |
| Gamma API   | Windows GDI32                         |

## License

MIT — see [LICENSE.txt](LICENSE.txt)
