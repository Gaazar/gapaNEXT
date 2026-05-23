#include "gapaNEXT.h"

namespace fs = std::filesystem;

// ----- Gamma ramp load / save (interleaved RGBRGB... file format) -----

GammaRamp LoadGammaFromFile(const std::string& path) {
    GammaRamp ramp = {};

    HANDLE hFile = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                               nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        std::cerr << "Cannot open: " << path << " (err " << GetLastError() << ")" << std::endl;
        return ramp;
    }

    DWORD expected = GAMMA_RAMP_SIZE * 3 * sizeof(WORD);
    WORD raw[3][GAMMA_RAMP_SIZE];
    DWORD bytesRead = 0;
    if (ReadFile(hFile, raw, sizeof(raw), &bytesRead, nullptr) && bytesRead == expected) {
        for (int j = 0; j < GAMMA_RAMP_SIZE; j++) {
            ramp.red[j]   = raw[0][j];
            ramp.green[j] = raw[1][j];
            ramp.blue[j]  = raw[2][j];
        }
        // Enforce non-decreasing monotonicity
        for (int j = 1; j < GAMMA_RAMP_SIZE; j++) {
            if (ramp.red[j]   < ramp.red[j-1]) ramp.red[j]   = ramp.red[j-1];
            if (ramp.green[j] < ramp.green[j-1]) ramp.green[j] = ramp.green[j-1];
            if (ramp.blue[j]  < ramp.blue[j-1])  ramp.blue[j]  = ramp.blue[j-1];
        }
        ramp.valid = true;
    } else {
        std::cerr << "Invalid gamma file: expected " << expected
                  << " bytes, got " << bytesRead << std::endl;
    }
    CloseHandle(hFile);
    return ramp;
}

bool SaveGammaToFile(const std::string& path, const GammaRamp& ramp) {
    if (!ramp.valid) return false;

    HANDLE hFile = CreateFileA(path.c_str(), GENERIC_WRITE, 0,
                               nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        std::cerr << "Cannot create: " << path << " (err " << GetLastError() << ")" << std::endl;
        return false;
    }

    WORD raw[3][GAMMA_RAMP_SIZE];
    for (int j = 0; j < GAMMA_RAMP_SIZE; j++) {
        raw[0][j] = ramp.red[j];
        raw[1][j] = ramp.green[j];
        raw[2][j] = ramp.blue[j];
    }

    DWORD written = 0;
    DWORD size = (DWORD)sizeof(raw);
    WriteFile(hFile, raw, size, &written, nullptr);
    CloseHandle(hFile);
    return written == size;
}

// ----- Display enumeration -----

std::vector<DisplayInfo> EnumerateDisplays() {
    std::vector<DisplayInfo> displays;

    DISPLAY_DEVICEA dd = {sizeof(dd)};
    int devNum = 0;

    while (EnumDisplayDevicesA(nullptr, devNum, &dd, 0)) {
        if (dd.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP) {
            DISPLAY_DEVICEA monitor = {sizeof(monitor)};
            EnumDisplayDevicesA(dd.DeviceName, 0, &monitor, 0);

            DisplayInfo di;
            di.deviceName = dd.DeviceName;
            di.name = monitor.DeviceString[0] ? monitor.DeviceString : dd.DeviceString;
            di.index = devNum;
            displays.push_back(di);
        }
        devNum++;
        dd = {sizeof(dd)};
    }
    return displays;
}

// ----- GPU gamma ramp -----

GammaRamp BuildIdentityRamp() {
    GammaRamp ramp = {};
    for (int i = 0; i < GAMMA_RAMP_SIZE; i++) {
        WORD v = (WORD)((double)i / (GAMMA_RAMP_SIZE - 1) * 65535.0);
        ramp.red[i] = v;
        ramp.green[i] = v;
        ramp.blue[i] = v;
    }
    ramp.valid = true;
    return ramp;
}

bool ApplyGammaRamp(const GammaRamp& ramp, const std::string& deviceName) {
    if (!ramp.valid) return false;

    HDC hdc = CreateDCA(deviceName.c_str(), nullptr, nullptr, nullptr);
    if (!hdc) {
        std::cerr << "Error: Cannot access display " << deviceName << std::endl;
        return false;
    }

    BOOL result = SetDeviceGammaRamp(hdc, const_cast<LPVOID>(static_cast<LPCVOID>(&ramp)));
    DWORD err = GetLastError();
    DeleteDC(hdc);

    if (!result) {
        std::cerr << "Error: SetDeviceGammaRamp failed (err " << err << ")" << std::endl;
        return false;
    }
    return true;
}

bool GetDisplayGammaRamp(const std::string& deviceName, GammaRamp& ramp) {
    HDC hdc = CreateDCA(deviceName.c_str(), nullptr, nullptr, nullptr);
    if (!hdc) return false;

    BOOL ok = GetDeviceGammaRamp(hdc, &ramp);
    DeleteDC(hdc);
    ramp.valid = (ok != 0);
    return ramp.valid;
}

// ----- Helpers -----

static void PrintDisplays(const std::vector<DisplayInfo>& displays) {
    for (const auto& d : displays) {
        std::cout << "  [" << (d.index + 1) << "] " << d.name
                  << " (" << d.deviceName << ")" << std::endl;
    }
}

static void CmdApply(const std::string& filePath, int displayIdx) {
    GammaRamp ramp = LoadGammaFromFile(filePath);
    if (!ramp.valid) return;

    auto displays = EnumerateDisplays();
    if (displayIdx < 0 || displayIdx > (int)displays.size()) {
        std::cerr << "Invalid display number." << std::endl;
        return;
    }

    std::cout << "Loaded: " << filePath << " (" << fs::file_size(filePath) << " bytes)" << std::endl;

    std::vector<DisplayInfo> targets;
    if (displayIdx == 0)
        targets = displays;
    else
        targets.push_back(displays[displayIdx - 1]);

    for (const auto& d : targets) {
        std::cout << "  " << d.name << " ... ";
        if (ApplyGammaRamp(ramp, d.deviceName))
            std::cout << "OK" << std::endl;
        else
            std::cout << "FAILED" << std::endl;
    }
}

static void CmdReset(int displayIdx) {
    auto displays = EnumerateDisplays();
    if (displayIdx < 0 || displayIdx > (int)displays.size()) {
        std::cerr << "Invalid display number." << std::endl;
        return;
    }

    GammaRamp identity = BuildIdentityRamp();

    std::vector<DisplayInfo> targets;
    if (displayIdx == 0)
        targets = displays;
    else
        targets.push_back(displays[displayIdx - 1]);

    for (const auto& d : targets) {
        std::cout << "  " << d.name << " ... ";
        if (ApplyGammaRamp(identity, d.deviceName))
            std::cout << "OK (linear gamma)" << std::endl;
        else
            std::cout << "FAILED" << std::endl;
    }
}

static void CmdSave(const std::string& filePath, int displayIdx) {
    auto displays = EnumerateDisplays();
    int di = (displayIdx == 0) ? 1 : displayIdx;
    if (di < 1 || di > (int)displays.size()) {
        std::cerr << "Invalid display number." << std::endl;
        return;
    }

    GammaRamp ramp = {};
    if (!GetDisplayGammaRamp(displays[di - 1].deviceName, ramp)) {
        std::cerr << "Failed to read gamma from display." << std::endl;
        return;
    }

    if (SaveGammaToFile(filePath, ramp))
        std::cout << "Saved gamma to: " << filePath << " (" << sizeof(WORD[3][GAMMA_RAMP_SIZE]) << " bytes)" << std::endl;
    else
        std::cerr << "Failed to save gamma file." << std::endl;
}

static void CmdStatus() {
    std::cout << std::endl;
    auto displays = EnumerateDisplays();
    for (const auto& d : displays) {
        GammaRamp ramp = {};
        std::string info = "(unavailable)";
        if (GetDisplayGammaRamp(d.deviceName, ramp))
            info = "R[0]=" + std::to_string(ramp.red[0])
                 + " R[255]=" + std::to_string(ramp.red[255]);
        std::cout << "  " << d.name << " (" << d.deviceName << ") -> " << info << std::endl;
    }
    std::cout << std::endl;
}

static void CmdHelp() {
    std::cout << std::endl;
    std::cout << "Commands:" << std::endl;
    std::cout << "  apply, a <file> [D]  Load gamma from file, apply to display D (0=all)" << std::endl;
    std::cout << "  reset, r [D]        Reset display D to linear gamma (0=all)" << std::endl;
    std::cout << "  save, s <file> [D]  Save current display gamma to file" << std::endl;
    std::cout << "  status, st          Show current gamma per display" << std::endl;
    std::cout << "  help, h, ?          Show this help" << std::endl;
    std::cout << "  exit, quit, q       Exit" << std::endl;
    std::cout << std::endl;
}

static void ShowUsage() {
    std::cout << "Usage:" << std::endl;
    std::cout << "  gapaNEXT                       Interactive REPL mode" << std::endl;
    std::cout << "  gapaNEXT apply <file> [D]      Apply gamma file to display D" << std::endl;
    std::cout << "  gapaNEXT reset [D]             Reset display to linear gamma" << std::endl;
    std::cout << "  gapaNEXT save <file> [D]       Save current gamma to file" << std::endl;
}

// ----- REPL -----

static void RunREPL() {
    std::cout << "gapaNEXT - GPU Gamma Ramp Switcher" << std::endl;
    std::cout << "Type 'help' for commands, 'exit' to quit." << std::endl;
    std::cout << std::endl;

    std::string line;
    for (;;) {
        std::cout << "gapaNEXT> " << std::flush;

        if (!std::getline(std::cin, line)) break;
        if (line.empty()) continue;

        std::vector<std::string> tokens;
        {
            std::istringstream iss(line);
            std::string tok;
            while (iss >> tok) tokens.push_back(tok);
        }
        if (tokens.empty()) continue;

        std::string cmd = tokens[0];
        for (auto& c : cmd) c = (char)tolower((unsigned char)c);

        if (cmd == "exit" || cmd == "quit" || cmd == "q") {
            break;
        }
        if (cmd == "help" || cmd == "h" || cmd == "?") {
            CmdHelp();
            continue;
        }
        if (cmd == "status" || cmd == "st") {
            CmdStatus();
            continue;
        }
        if (cmd == "apply" || cmd == "a") {
            if (tokens.size() < 2) {
                std::cerr << "Usage: apply <file> [display#]" << std::endl;
                continue;
            }
            int di = tokens.size() >= 3 ? atoi(tokens[2].c_str()) : 0;
            CmdApply(tokens[1], di);
            continue;
        }
        if (cmd == "reset" || cmd == "r") {
            int di = tokens.size() >= 2 ? atoi(tokens[1].c_str()) : 0;
            CmdReset(di);
            continue;
        }
        if (cmd == "save" || cmd == "s") {
            if (tokens.size() < 2) {
                std::cerr << "Usage: save <file> [display#]" << std::endl;
                continue;
            }
            int di = tokens.size() >= 3 ? atoi(tokens[2].c_str()) : 0;
            CmdSave(tokens[1], di);
            continue;
        }
        std::cerr << "Unknown command: " << tokens[0] << " (type 'help' for commands)" << std::endl;
    }
}

// ----- Main -----

int main(int argc, char* argv[]) {
    std::string cmd = (argc >= 2) ? argv[1] : "";

    if (cmd == "--help" || cmd == "-h" || cmd == "help") {
        ShowUsage();
        return 0;
    }

    if (cmd == "apply" || cmd == "a") {
        if (argc < 3) {
            std::cerr << "Usage: gapaNEXT apply <file> [display#]" << std::endl;
            return 1;
        }
        int di = argc >= 4 ? atoi(argv[3]) : 0;
        CmdApply(argv[2], di);
        return 0;
    }

    if (cmd == "reset" || cmd == "r") {
        int di = argc >= 3 ? atoi(argv[2]) : 0;
        CmdReset(di);
        return 0;
    }

    if (cmd == "save" || cmd == "s") {
        if (argc < 3) {
            std::cerr << "Usage: gapaNEXT save <file> [display#]" << std::endl;
            return 1;
        }
        int di = argc >= 4 ? atoi(argv[3]) : 0;
        CmdSave(argv[2], di);
        return 0;
    }

    if (cmd == "status" || cmd == "st") {
        CmdStatus();
        return 0;
    }

    RunREPL();
    return 0;
}
