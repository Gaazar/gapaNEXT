#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wingdi.h>

#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include <filesystem>

constexpr int GAMMA_RAMP_SIZE = 256;

struct GammaRamp {
    WORD red[GAMMA_RAMP_SIZE];
    WORD green[GAMMA_RAMP_SIZE];
    WORD blue[GAMMA_RAMP_SIZE];
    bool valid = false;
};

struct DisplayInfo {
    int index;
    std::string name;
    std::string deviceName;
};

GammaRamp LoadGammaFromFile(const std::string& path);
bool SaveGammaToFile(const std::string& path, const GammaRamp& ramp);
GammaRamp BuildIdentityRamp();
bool ApplyGammaRamp(const GammaRamp& ramp, const std::string& deviceName);
bool GetDisplayGammaRamp(const std::string& deviceName, GammaRamp& ramp);
std::vector<DisplayInfo> EnumerateDisplays();
