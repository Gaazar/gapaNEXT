#include "gapaNEXT.h"

#pragma warning(disable : 4996)

namespace fs = std::filesystem;

static std::wstring ToWide(const std::string &s)
{
    if (s.empty())
        return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring ws(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), ws.data(), len);
    return ws;
}

static std::string ToNarrow(const std::wstring &ws)
{
    if (ws.empty())
        return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(), nullptr, 0, nullptr, nullptr);
    std::string s(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(), s.data(), len, nullptr, nullptr);
    return s;
}

static DWORD ReadU32BE(const BYTE *p)
{
    return (DWORD(p[0]) << 24) | (DWORD(p[1]) << 16) | (DWORD(p[2]) << 8) | DWORD(p[3]);
}

// ----- Profile enumeration -----

std::vector<ColorProfile> EnumerateColorProfiles()
{
    std::vector<ColorProfile> profiles;

    WCHAR dirBuf[MAX_PATH];
    DWORD dirLen = MAX_PATH;
    if (!GetColorDirectoryW(nullptr, dirBuf, &dirLen))
    {
        std::cerr << "Failed to get color directory." << std::endl;
        return profiles;
    }
    std::wstring colorDir(dirBuf);

    for (const auto &entry : fs::directory_iterator(colorDir))
    {
        if (!entry.is_regular_file())
            continue;
        auto ext = entry.path().extension().wstring();
        for (auto &c : ext)
            c = towupper(c);
        if (ext != L".ICM" && ext != L".ICC")
            continue;

        ColorProfile cp;
        cp.filename = entry.path().filename().string();
        cp.fullPath = entry.path().string();
        cp.description = ReadProfileDescription(entry.path().wstring());
        // Check for VCGT tag
        GammaRamp ramp = ReadProfileGammaRamp(entry.path().wstring());
        cp.hasGammaRamp = ramp.valid;
        profiles.push_back(cp);
    }

    std::sort(profiles.begin(), profiles.end(), [](const auto &a, const auto &b) { return a.filename < b.filename; });
    return profiles;
}

// ----- ICC profile parser helper -----

struct IccFile
{
    HANDLE h;
    DWORD tagCount;

    IccFile(const std::wstring &path) : h(INVALID_HANDLE_VALUE), tagCount(0)
    {
        h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                        nullptr);
        if (h == INVALID_HANDLE_VALUE)
            return;

        DWORD br;
        BYTE header[128];
        if (!ReadFile(h, header, sizeof(header), &br, nullptr) || br < 128)
        {
            CloseHandle(h);
            h = INVALID_HANDLE_VALUE;
            return;
        }

        // Tag count is the first 4 bytes after the 128-byte header
        BYTE tc[4];
        if (!ReadFile(h, tc, 4, &br, nullptr) || br < 4)
        {
            CloseHandle(h);
            h = INVALID_HANDLE_VALUE;
            return;
        }
        tagCount = ReadU32BE(tc);
        if (tagCount > 200)
        {
            CloseHandle(h);
            h = INVALID_HANDLE_VALUE;
            return;
        }
    }

    ~IccFile()
    {
        if (h != INVALID_HANDLE_VALUE)
            CloseHandle(h);
    }
    explicit operator bool() const
    {
        return h != INVALID_HANDLE_VALUE;
    }

    // Tag table starts at file offset 132
    bool GetTagEntry(int idx, char sigOut[4], DWORD &offset, DWORD &size)
    {
        DWORD br;
        BYTE entry[12];
        SetFilePointer(h, 132 + idx * 12, nullptr, FILE_BEGIN);
        if (!ReadFile(h, entry, 12, &br, nullptr) || br < 12)
            return false;
        memcpy(sigOut, entry, 4);
        offset = ReadU32BE(entry + 4);
        size = ReadU32BE(entry + 8);
        return true;
    }

    bool ReadTagData(DWORD offset, std::vector<BYTE> &data, DWORD size)
    {
        data.resize(size);
        DWORD br;
        SetFilePointer(h, offset, nullptr, FILE_BEGIN);
        return ReadFile(h, data.data(), size, &br, nullptr) && br >= size;
    }
};

// ----- ICC description parsing -----

std::string ReadProfileDescription(const std::wstring &path)
{
    IccFile icc(path);
    if (!icc)
        return {};

    std::string description;

    for (DWORD i = 0; i < icc.tagCount; i++)
    {
        char sig[4];
        DWORD offset, size;
        if (!icc.GetTagEntry(i, sig, offset, size))
            break;

        if (memcmp(sig, "desc", 4) != 0 || size <= 4)
            continue;

        std::vector<BYTE> data;
        if (!icc.ReadTagData(offset, data, size))
            continue;

        DWORD typeSig = ReadU32BE(data.data());

        if (typeSig == 0x64657363)
        { // 'desc'
            if (size >= 13)
            {
                DWORD asciiLen = DWORD(data[8]);
                if (asciiLen > 0 && 12 + asciiLen <= size)
                {
                    description.assign((char *)data.data() + 12, asciiLen);
                    while (!description.empty() && description.back() == '\0')
                        description.pop_back();
                }
            }
        }
        else if (typeSig == 0x6D6C7563)
        { // 'mluc'
            if (size >= 28)
            {
                DWORD numRecords = ReadU32BE(data.data() + 8);
                DWORD rs = 12;
                if (numRecords > 0 && 28 + rs <= size)
                {
                    DWORD recOff = 28;
                    for (DWORD r = 0; r < numRecords && r < 10; r++)
                    {
                        DWORD lang = ReadU32BE(data.data() + 16 + r * rs);
                        if (lang == 0x0001)
                        {
                            recOff = 28 + r * rs;
                            break;
                        }
                        if (r == 0)
                            recOff = 28;
                    }
                    DWORD strLen = ReadU32BE(data.data() + recOff + 4);
                    DWORD strOff = ReadU32BE(data.data() + recOff + 8);
                    if (strLen > 0 && strOff + strLen * 2 <= size)
                    {
                        std::wstring wdesc;
                        const WCHAR *wd = reinterpret_cast<const WCHAR *>(data.data() + strOff);
                        for (DWORD c = 0; c < strLen && wd[c]; c++)
                            wdesc += wd[c];
                        description = ToNarrow(wdesc);
                    }
                }
            }
        }
        break;
    }

    return description;
}

// ----- VCGT gamma ramp parsing -----

GammaRamp ReadProfileGammaRamp(const std::wstring &path)
{
    GammaRamp ramp = {};
    IccFile icc(path);
    if (!icc)
        return ramp;

    for (DWORD i = 0; i < icc.tagCount; i++)
    {
        char sig[4];
        DWORD offset, size;
        if (!icc.GetTagEntry(i, sig, offset, size))
            break;
        if (memcmp(sig, "vcgt", 4) != 0 || size < 14)
            continue;

        std::vector<BYTE> data;
        if (!icc.ReadTagData(offset, data, size) || size < 14)
            break;

        // VCGT tag: type(4) + reserved(4) + gammaType(4)
        DWORD gammaType = ReadU32BE(data.data() + 8);

        if (gammaType == 0)
        {
            // Table type. May have optional sub-header:
            // numChannels(2) + numEntries(2) + entrySize(2)
            // If missing, default to 3 channels, entrySize=2,
            // and numEntries = (size-12) / (3*2)
            DWORD dataOff = 12;
            WORD numCh = 3, numEnt = 256, entSz = 2;

            if (size >= 20)
            {
                numCh = (WORD)((WORD(data[12]) << 8) | data[13]);
                numEnt = (WORD)((WORD(data[14]) << 8) | data[15]);
                entSz = (WORD)((WORD(data[16]) << 8) | data[17]);
                if (entSz == 1 || entSz == 2)
                {
                    dataOff = 18;
                }
                else
                {
                    // Sub-header not present, use defaults
                    numCh = 3;
                    entSz = 2;
                    numEnt = (WORD)((size - 12) / (numCh * entSz));
                }
            }
            else
            {
                numEnt = (WORD)((size - 12) / (numCh * entSz));
            }
            if (numEnt < 2 || numEnt > 65535)
                break;

            const BYTE *tbl = data.data() + dataOff;
            DWORD tblBytes = size - dataOff;

            // VCGT table: channels may be planar (R...R, G...G, B...B) or
            // interleaved (RGB, RGB, ...). Detect by checking monotonicity.
            DWORD chanSize = (DWORD)numEnt * entSz;

            // Try planar first (most common for VCGT)
            auto readPlanar = [&](int ch, int e) -> WORD {
                DWORD off = (DWORD)ch * chanSize + (DWORD)e * entSz;
                if (off + 2 > tblBytes)
                    return 0;
                return (WORD(tbl[off]) << 8) | tbl[off + 1];
            };
            auto readInterleaved = [&](int ch, int e) -> WORD {
                DWORD off = ((DWORD)e * numCh + ch) * entSz;
                if (off + 2 > tblBytes)
                    return 0;
                return (WORD(tbl[off]) << 8) | tbl[off + 1];
            };

            // Quick check: which layout is monotonic?
            auto isMono = [&](auto &readFn) -> bool {
                WORD prev[3] = {0, 0, 0};
                for (DWORD e = 0; e < (std::min<DWORD>)(numEnt, 10); e++)
                {
                    for (int ch = 0; ch < (int)numCh; ch++)
                    {
                        WORD v = readFn(ch, e);
                        if (v < prev[ch])
                            return false;
                        prev[ch] = v;
                    }
                }
                return true;
            };

            bool usePlanar = isMono(readPlanar);
            if (!usePlanar && isMono(readInterleaved))
                usePlanar = false;
            // If both fail monotonic, prefer planar then fix

            if (numEnt == GAMMA_RAMP_SIZE)
            {
                for (int j = 0; j < GAMMA_RAMP_SIZE; j++)
                {
                    if (usePlanar)
                    {
                        ramp.red[j] = readPlanar(0, j);
                        ramp.green[j] = readPlanar(1, j);
                        ramp.blue[j] = readPlanar(2, j);
                    }
                    else
                    {
                        ramp.red[j] = readInterleaved(0, j);
                        ramp.green[j] = readInterleaved(1, j);
                        ramp.blue[j] = readInterleaved(2, j);
                    }
                }
            }
            else
            {
                for (int j = 0; j < GAMMA_RAMP_SIZE; j++)
                {
                    double idx = (double)j / (GAMMA_RAMP_SIZE - 1) * (numEnt - 1);
                    int lo = (int)idx;
                    int hi = (std::min)(lo + 1, (int)(numEnt - 1));
                    double frac = idx - lo;

                    auto lerp = [](WORD a, WORD b, double t) -> WORD { return (WORD)(a + (b - a) * t); };
                    if (usePlanar)
                    {
                        ramp.red[j] = lerp(readPlanar(0, lo), readPlanar(0, hi), frac);
                        ramp.green[j] = lerp(readPlanar(1, lo), readPlanar(1, hi), frac);
                        ramp.blue[j] = lerp(readPlanar(2, lo), readPlanar(2, hi), frac);
                    }
                    else
                    {
                        ramp.red[j] = lerp(readInterleaved(0, lo), readInterleaved(0, hi), frac);
                        ramp.green[j] = lerp(readInterleaved(1, lo), readInterleaved(1, hi), frac);
                        ramp.blue[j] = lerp(readInterleaved(2, lo), readInterleaved(2, hi), frac);
                    }
                }
            }
            // Enforce non-decreasing monotonicity (required by SetDeviceGammaRamp)
            for (int j = 1; j < GAMMA_RAMP_SIZE; j++)
            {
                if (ramp.red[j] < ramp.red[j - 1])
                    ramp.red[j] = ramp.red[j - 1];
                if (ramp.green[j] < ramp.green[j - 1])
                    ramp.green[j] = ramp.green[j - 1];
                if (ramp.blue[j] < ramp.blue[j - 1])
                    ramp.blue[j] = ramp.blue[j - 1];
            }

            ramp.valid = true;
        }
        else if (gammaType == 1)
        {
            if (size < 12 + 36)
                break;

            auto readFloat = [](const BYTE *p) -> float {
                DWORD bits = ReadU32BE(p);
                float f;
                memcpy(&f, &bits, sizeof(f));
                return f;
            };

            const BYTE *fp = data.data() + 12;
            float rGamma = readFloat(fp + 0), rMin = readFloat(fp + 4), rMax = readFloat(fp + 8);
            float gGamma = readFloat(fp + 12), gMin = readFloat(fp + 16), gMax = readFloat(fp + 20);
            float bGamma = readFloat(fp + 24), bMin = readFloat(fp + 28), bMax = readFloat(fp + 32);

            for (int j = 0; j < GAMMA_RAMP_SIZE; j++)
            {
                double t = (double)j / (GAMMA_RAMP_SIZE - 1);
                ramp.red[j] = (WORD)((rMin + (rMax - rMin) * (float)std::pow(t, 1.0 / rGamma)) * 65535.0f);
                ramp.green[j] = (WORD)((gMin + (gMax - gMin) * (float)std::pow(t, 1.0 / gGamma)) * 65535.0f);
                ramp.blue[j] = (WORD)((bMin + (bMax - bMin) * (float)std::pow(t, 1.0 / bGamma)) * 65535.0f);
            }
            ramp.valid = true;
        }
        break;
    }

    return ramp;
}

// ----- Display enumeration -----

std::vector<DisplayInfo> EnumerateDisplays()
{
    std::vector<DisplayInfo> displays;

    DISPLAY_DEVICEA dd = {sizeof(dd)};
    int devNum = 0;

    while (EnumDisplayDevicesA(nullptr, devNum, &dd, 0))
    {
        if (dd.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP)
        {
            DISPLAY_DEVICEA monitor = {sizeof(monitor)};
            EnumDisplayDevicesA(dd.DeviceName, 0, &monitor, 0);

            DisplayInfo di;
            di.deviceName = dd.DeviceName;
            di.name = monitor.DeviceString[0] ? monitor.DeviceString : dd.DeviceString;
            di.index = devNum;
            di.currentProfile = GetCurrentProfileForDisplay(di.deviceName);
            displays.push_back(di);
        }
        devNum++;
        dd = {sizeof(dd)};
    }
    return displays;
}

std::string GetCurrentProfileForDisplay(const std::string &deviceName)
{
    HDC hdc = CreateDCA(deviceName.c_str(), nullptr, nullptr, nullptr);
    if (!hdc)
        return {};

    char pathBuf[MAX_PATH] = {};
    DWORD bufSize = MAX_PATH;
    if (GetICMProfileA(hdc, &bufSize, pathBuf))
    {
        DeleteDC(hdc);
        std::string full(pathBuf);
        return fs::path(full).filename().string();
    }
    DeleteDC(hdc);
    return {};
}

// ----- ICM profile switching -----

bool ApplyProfileToDisplay(const std::string &profilePath, const std::string &deviceName)
{
    HDC hdc = CreateDCA(deviceName.c_str(), nullptr, nullptr, nullptr);
    if (!hdc)
    {
        std::cerr << "Error: Cannot access display " << deviceName << std::endl;
        return false;
    }

    char pathBuf[MAX_PATH] = {};
    strncpy_s(pathBuf, profilePath.c_str(), MAX_PATH - 1);
    BOOL result = SetICMProfileA(hdc, pathBuf);
    DeleteDC(hdc);

    if (!result)
    {
        DWORD err = GetLastError();
        std::cerr << "Error: SetICMProfile failed (code " << err << ")" << std::endl;
        return false;
    }
    return true;
}

// Optional: apply GPU gamma ramp for immediate visual update (VCGT profiles only)
static bool ApplyGammaRamp(const GammaRamp &ramp, const std::string &deviceName)
{
    if (!ramp.valid)
        return false;
    HDC hdc = CreateDCA(deviceName.c_str(), nullptr, nullptr, nullptr);
    if (!hdc)
        return false;
    BOOL result = SetDeviceGammaRamp(hdc, const_cast<LPVOID>(static_cast<LPCVOID>(&ramp)));
    DeleteDC(hdc);
    return result != 0;
}

static GammaRamp BuildIdentityRamp()
{
    GammaRamp ramp = {};
    for (int i = 0; i < GAMMA_RAMP_SIZE; i++)
    {
        WORD v = (WORD)((double)i / (GAMMA_RAMP_SIZE - 1) * 65535.0);
        ramp.red[i] = v;
        ramp.green[i] = v;
        ramp.blue[i] = v;
    }
    ramp.valid = true;
    return ramp;
}

// Cache for loaded gamma ramps
static std::vector<ColorProfile> g_cachedProfiles;
static std::vector<GammaRamp> g_cachedRamps;

static void RefreshCache()
{
    g_cachedProfiles = EnumerateColorProfiles();
    g_cachedRamps.clear();
    for (const auto &p : g_cachedProfiles)
    {
        g_cachedRamps.push_back(ReadProfileGammaRamp(ToWide(p.fullPath)));
    }
}

// ----- UI helpers -----

static void PrintProfiles(const std::vector<ColorProfile> &profiles)
{
    for (size_t i = 0; i < profiles.size(); i++)
    {
        std::cout << "  [" << (i + 1) << "] " << profiles[i].filename;
        if (!profiles[i].description.empty())
            std::cout << "  (" << profiles[i].description << ")";
        std::cout << std::endl;
    }
}

static void PrintDisplays(const std::vector<DisplayInfo> &displays)
{
    for (const auto &d : displays)
    {
        std::cout << "  [" << (d.index + 1) << "] " << d.name << " (" << d.deviceName << ")";
        if (!d.currentProfile.empty())
            std::cout << "  [current: " << d.currentProfile << "]";
        std::cout << std::endl;
    }
}

static void CmdGamma(int profileIdx)
{
    if (profileIdx < 1 || profileIdx > (int)g_cachedProfiles.size())
    {
        std::cerr << "Invalid profile number." << std::endl;
        return;
    }
    const auto &profile = g_cachedProfiles[profileIdx - 1];
    const GammaRamp &ramp = g_cachedRamps[profileIdx - 1];

    // Show raw hex dump of VCGT tag data
    IccFile icc(ToWide(profile.fullPath));
    if (icc)
    {
        for (DWORD i = 0; i < icc.tagCount; i++)
        {
            char sig[4];
            DWORD offset, size;
            if (!icc.GetTagEntry(i, sig, offset, size))
                break;
            if (memcmp(sig, "vcgt", 4) != 0)
                continue;

            std::vector<BYTE> data;
            if (icc.ReadTagData(offset, data, size))
            {
                std::cout << "VCGT raw (first 30 bytes):" << std::endl;
                for (DWORD b = 0; b < (std::min<DWORD>)(30, size); b++)
                {
                    if (b % 16 == 0)
                        printf("  %04X: ", b);
                    printf("%02X ", data[b]);
                    if (b % 16 == 15 || b == (std::min<DWORD>)(30, size) - 1)
                        std::cout << std::endl;
                }
                DWORD gt = ReadU32BE(data.data() + 8);
                std::cout << "gammaType=" << gt << " size=" << size << std::endl;

                // Show what subheader would read
                WORD v12 = (WORD(data[12]) << 8) | data[13];
                WORD v14 = (WORD(data[14]) << 8) | data[15];
                WORD v16 = (WORD(data[16]) << 8) | data[17];
                std::cout << "bytes[12..17] as subheader: numCh=" << v12 << " numEntries=" << v14
                          << " entrySize=" << v16 << std::endl;
                std::cout << "With subheader: 18+" << v12 << "*" << v14 << "*" << v16 << "=" << (18 + v12 * v14 * v16)
                          << " vs size=" << size << std::endl;
                std::cout << "Without subheader: 12+3*" << ((size - 12) / 6) << "*2=" << size << std::endl;
            }
            break;
        }
    }

    if (!ramp.valid)
    {
        std::cout << "No gamma ramp data in this profile." << std::endl;
        return;
    }
    std::cout << std::endl << "Parsed gamma ramp (first 5 / last 5 of " << GAMMA_RAMP_SIZE << "):" << std::endl;
    std::cout << "Idx     R        G        B" << std::endl;
    for (int i = 0; i < 5; i++)
        std::cout << "[" << i << "]  " << ramp.red[i] << "  " << ramp.green[i] << "  " << ramp.blue[i] << std::endl;
    std::cout << " ..." << std::endl;
    for (int i = GAMMA_RAMP_SIZE - 5; i < GAMMA_RAMP_SIZE; i++)
        std::cout << "[" << i << "] " << ramp.red[i] << "  " << ramp.green[i] << "  " << ramp.blue[i] << std::endl;
    std::cout << std::endl;
}

static void CmdApply(int profileIdx, int displayIdx)
{
    if (profileIdx < 1 || profileIdx > (int)g_cachedProfiles.size())
    {
        std::cerr << "Invalid profile number. Use 'list' to see available profiles." << std::endl;
        return;
    }
    auto displays = EnumerateDisplays();
    if (displayIdx < 0 || displayIdx > (int)displays.size())
    {
        std::cerr << "Invalid display number. Use 'list' to see available displays." << std::endl;
        return;
    }

    const auto &profile = g_cachedProfiles[profileIdx - 1];
    const GammaRamp &ramp = g_cachedRamps[profileIdx - 1];

    std::vector<DisplayInfo> targets;
    if (displayIdx == 0)
        targets = displays;
    else
        targets.push_back(displays[displayIdx - 1]);

    for (const auto &d : targets)
    {
        std::cout << "  " << d.name << " ... ";

        // Primary: associate ICC profile via Windows color management
        if (ApplyProfileToDisplay(profile.fullPath, d.deviceName))
        {
            std::cout << "OK (ICM)";

            // Optional: also apply gamma ramp for immediate visual feedback
            if (ramp.valid && ApplyGammaRamp(ramp, d.deviceName))
                std::cout << " + gamma";
            else if (ramp.valid)
                std::cout << " (gamma skipped)";

            std::cout << std::endl;
        }
        else
        {
            std::cout << "FAILED" << std::endl;
        }
    }
}

static void CmdReset(int displayIdx)
{
    auto displays = EnumerateDisplays();
    if (displayIdx < 0 || displayIdx > (int)displays.size())
    {
        std::cerr << "Invalid display number." << std::endl;
        return;
    }

    // Find sRGB profile path for ICM reset
    std::string srgbPath;
    {
        WCHAR dirBuf[MAX_PATH];
        DWORD dirLen = MAX_PATH;
        if (GetColorDirectoryW(nullptr, dirBuf, &dirLen))
        {
            std::wstring wdir(dirBuf);
            wdir += L"\\sRGB Color Space Profile.icm";
            if (fs::exists(wdir))
                srgbPath = ToNarrow(wdir);
        }
    }

    GammaRamp identity = BuildIdentityRamp();

    std::vector<DisplayInfo> targets;
    if (displayIdx == 0)
        targets = displays;
    else
        targets.push_back(displays[displayIdx - 1]);

    for (const auto &d : targets)
    {
        std::cout << "  " << d.name << " ... ";

        // Set sRGB via ICM, then reset gamma ramp
        bool icmOk = srgbPath.empty() ? true : ApplyProfileToDisplay(srgbPath, d.deviceName);
        bool gammaOk = ApplyGammaRamp(identity, d.deviceName);

        if (icmOk && gammaOk)
        {
            std::cout << "OK (sRGB + linear gamma)" << std::endl;
        }
        else if (icmOk)
        {
            std::cout << "OK (sRGB ICM, gamma failed)" << std::endl;
        }
        else
        {
            std::cout << "FAILED" << std::endl;
        }
    }
}

static void CmdList()
{
    RefreshCache();
    std::cout << std::endl << "Profiles:" << std::endl;
    if (g_cachedProfiles.empty())
    {
        std::cout << "  (none)" << std::endl;
    }
    else
    {
        PrintProfiles(g_cachedProfiles);
    }

    std::cout << std::endl << "Displays:" << std::endl;
    auto displays = EnumerateDisplays();
    if (displays.empty())
    {
        std::cout << "  (none)" << std::endl;
    }
    else
    {
        PrintDisplays(displays);
    }
    std::cout << std::endl;
}

static void CmdStatus()
{
    std::cout << std::endl;
    auto displays = EnumerateDisplays();
    for (const auto &d : displays)
    {
        std::cout << "  " << d.name << " (" << d.deviceName << ")"
                  << " -> " << (d.currentProfile.empty() ? "(default)" : d.currentProfile) << std::endl;
    }
    std::cout << std::endl;
}

static void CmdHelp()
{
    std::cout << std::endl;
    std::cout << "Commands:" << std::endl;
    std::cout << "  list, l              Refresh and list profiles & displays" << std::endl;
    std::cout << "  status, s            Show current profile per display" << std::endl;
    std::cout << "  apply, a <P> <D>     Switch display D to profile P (0=all)" << std::endl;
    std::cout << "  reset, r [D]         Reset display to sRGB + linear gamma (0=all)" << std::endl;
    std::cout << "  tags, t <P>          Show ICC tags inside profile P" << std::endl;
    std::cout << "  gamma, g <P>         Show gamma ramp values from profile P" << std::endl;
    std::cout << "  help, h, ?           Show this help" << std::endl;
    std::cout << "  exit, quit, q        Exit" << std::endl;
    std::cout << std::endl;
    std::cout << "Uses Windows ICM (SetICMProfile) to associate profiles." << std::endl;
    std::cout << "Works with ALL .icm/.icc profiles, not just VCGT-bearing ones." << std::endl;
    std::cout << std::endl;
}

static void CmdTags(const std::wstring &path)
{
    IccFile icc(path);
    if (!icc)
    {
        std::cerr << "Cannot open file." << std::endl;
        return;
    }

    std::cout << "File: " << ToNarrow(path) << std::endl;
    std::cout << "Tags (" << icc.tagCount << "):" << std::endl;
    std::cout << "----------------------------------------" << std::endl;

    for (DWORD i = 0; i < icc.tagCount && i < 50; i++)
    {
        char sig[4];
        DWORD offset, size;
        if (!icc.GetTagEntry(i, sig, offset, size))
            break;

        std::vector<BYTE> data;
        char typeSig[5] = {};
        DWORD readSize = size < 4 ? size : 4;
        if (size >= 4 && icc.ReadTagData(offset, data, readSize))
        {
            memcpy(typeSig, data.data(), 4);
        }

        bool isGamma = (memcmp(sig, "vcgt", 4) == 0);
        std::cout << "  " << std::string(sig, 4) << "  offset=" << offset << " size=" << size
                  << "  type=" << (typeSig[0] ? typeSig : "?") << (isGamma ? "  << GAMMA TABLE" : "") << std::endl;
    }
    std::cout << std::endl;
}

static void ShowUsage()
{
    std::cout << "Usage:" << std::endl;
    std::cout << "  gapaNEXT                Interactive REPL mode" << std::endl;
    std::cout << "  gapaNEXT list           List profiles and displays" << std::endl;
    std::cout << "  gapaNEXT apply <P> <D>  Apply profile P to display D (0=all)" << std::endl;
    std::cout << "  gapaNEXT reset [D]      Reset display to linear gamma" << std::endl;
    std::cout << "  gapaNEXT status         Show current profile per display" << std::endl;
}

static void RunREPL()
{
    std::cout << "gapaNEXT - Display Gamma Profile Switcher" << std::endl;
    std::cout << "Type 'help' for commands, 'exit' to quit." << std::endl;
    std::cout << std::endl;

    RefreshCache();

    std::string line;
    for (;;)
    {
        std::cout << "gapaNEXT> " << std::flush;

        if (!std::getline(std::cin, line))
            break;
        if (line.empty())
            continue;

        std::vector<std::string> tokens;
        {
            std::istringstream iss(line);
            std::string tok;
            while (iss >> tok)
                tokens.push_back(tok);
        }
        if (tokens.empty())
            continue;

        std::string cmd = tokens[0];
        for (auto &c : cmd)
            c = (char)tolower((unsigned char)c);

        if (cmd == "exit" || cmd == "quit" || cmd == "q")
        {
            break;
        }
        if (cmd == "help" || cmd == "h" || cmd == "?")
        {
            CmdHelp();
            continue;
        }
        if (cmd == "list" || cmd == "l")
        {
            CmdList();
            continue;
        }
        if (cmd == "status" || cmd == "s")
        {
            CmdStatus();
            continue;
        }
        if (cmd == "apply" || cmd == "a")
        {
            if (tokens.size() < 3)
            {
                std::cerr << "Usage: apply <profile#> <display#>" << std::endl;
                std::cerr << "       display# = 0 means all displays." << std::endl;
                continue;
            }
            CmdApply(atoi(tokens[1].c_str()), atoi(tokens[2].c_str()));
            continue;
        }
        if (cmd == "reset" || cmd == "r")
        {
            int di = tokens.size() >= 2 ? atoi(tokens[1].c_str()) : 0;
            CmdReset(di);
            continue;
        }
        if (cmd == "tags" || cmd == "t")
        {
            if (tokens.size() < 2)
            {
                std::cerr << "Usage: tags <profile#>" << std::endl;
                continue;
            }
            int pi = atoi(tokens[1].c_str());
            if (pi < 1 || pi > (int)g_cachedProfiles.size())
            {
                std::cerr << "Invalid profile number." << std::endl;
                continue;
            }
            std::cout << std::endl;
            CmdTags(ToWide(g_cachedProfiles[pi - 1].fullPath));
            std::cout << std::endl;
            continue;
        }
        if (cmd == "gamma" || cmd == "g")
        {
            if (tokens.size() < 2)
            {
                std::cerr << "Usage: gamma <profile#>" << std::endl;
                continue;
            }
            int pi = atoi(tokens[1].c_str());
            if (pi < 1 || pi > (int)g_cachedProfiles.size())
            {
                std::cerr << "Invalid profile number." << std::endl;
                continue;
            }
            CmdGamma(pi);
            continue;
        }

        std::cerr << "Unknown command: " << tokens[0] << " (type 'help' for commands)" << std::endl;
    }
}

int main(int argc, char *argv[])
{
    std::string cmd = (argc >= 2) ? argv[1] : "";

    if (cmd == "--help" || cmd == "-h" || cmd == "help")
    {
        ShowUsage();
        return 0;
    }

    if (cmd == "list")
    {
        RefreshCache();
        std::cout << "Profiles:" << std::endl;
        if (g_cachedProfiles.empty())
            std::cout << "  (none)" << std::endl;
        else
            PrintProfiles(g_cachedProfiles);
        std::cout << std::endl << "Displays:" << std::endl;
        auto displays = EnumerateDisplays();
        if (displays.empty())
            std::cout << "  (none)" << std::endl;
        else
            PrintDisplays(displays);
        return 0;
    }

    if (cmd == "status")
    {
        CmdStatus();
        return 0;
    }

    if (cmd == "apply")
    {
        if (argc < 4)
        {
            std::cerr << "Usage: gapaNEXT apply <profile#> <display#>" << std::endl;
            std::cerr << "       display# = 0 means all displays." << std::endl;
            return 1;
        }
        RefreshCache();
        CmdApply(atoi(argv[2]), atoi(argv[3]));
        return 0;
    }

    if (cmd == "reset")
    {
        int di = argc >= 3 ? atoi(argv[2]) : 0;
        CmdReset(di);
        return 0;
    }

    RunREPL();
    return 0;
}
