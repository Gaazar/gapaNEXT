#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <icm.h>
#include <wingdi.h>

#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include <filesystem>
#include <cmath>

struct ColorProfile {
    std::string filename;
    std::string fullPath;
    std::string description;
    bool hasGammaRamp = false;
};

struct DisplayInfo {
    int index;
    std::string name;
    std::string deviceName;
    std::string currentProfile;
};

constexpr int GAMMA_RAMP_SIZE = 256;

struct GammaRamp {
    WORD red[GAMMA_RAMP_SIZE];
    WORD green[GAMMA_RAMP_SIZE];
    WORD blue[GAMMA_RAMP_SIZE];
    bool valid = false;
};

std::vector<ColorProfile> EnumerateColorProfiles();
std::vector<DisplayInfo> EnumerateDisplays();
std::string ReadProfileDescription(const std::wstring& path);
GammaRamp ReadProfileGammaRamp(const std::wstring& path);
std::string GetCurrentProfileForDisplay(const std::string& deviceName);
bool ApplyGammaToDisplay(const GammaRamp& ramp, const std::string& deviceName);
bool GetDisplayGammaRamp(const std::string& deviceName, GammaRamp& ramp);
