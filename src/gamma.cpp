#include "gamma.h"

#include <windows.h>
#include <iostream>
#include <fstream>

// ----- Gamma ramp load / save (interleaved RGBRGB... file format) -----
using namespace std;
gamma_ramp::gamma_ramp()
{
    set_identity();
}
void gamma_ramp::set_identity()
{
    for (int i = 0; i < GAMMA_RAMP_SIZE; i++)
    {
        WORD v = (WORD)((double)i / (GAMMA_RAMP_SIZE - 1) * 65535.0);
        r[i] = v;
        g[i] = v;
        b[i] = v;
    }
}
gamma_ramp::gamma_ramp(filesystem::path path)
{
    ifstream fs(path, ios::binary);
    if (!fs)
    {
        std::cerr << "Cannot open: " << path << " (err " << fs.rdstate() << ")" << std::endl;
        set_identity();
        return;
    }
    fs.read((char*)this, sizeof(gamma_ramp));
    if (fs.gcount() != sizeof(gamma_ramp))
    {
        std::cerr << "Invalid gamma file: expected " << sizeof(gamma_ramp) << " bytes, got " << fs.gcount()
                  << std::endl;
        set_identity();
    }
}
bool gamma_ramp::to_file(const std::string& path) const
{
    std::ofstream fs(path, std::ios::binary);
    if (!fs)
    {
        std::cerr << "Cannot open: " << path << " (err " << fs.rdstate() << ")" << std::endl;
        return false;
    }
    fs.write((const char*)this, sizeof(gamma_ramp));
    return fs.good();
}

// ----- Gamma descriptor (.gmd file format) -----

gamma_desc::gamma_desc(std::filesystem::path path)
{
    std::ifstream fs(path, std::ios::binary);
    if (!fs)
    {
        std::cerr << "Cannot open preset: " << path << std::endl;
        return;
    }

    auto read_vec = [&fs](std::vector<pioint_t>& v)
    {
        uint32_t count = 0;
        fs.read((char*)&count, sizeof(count));
        v.resize(count);
        fs.read((char*)v.data(), count * sizeof(pioint_t));
    };
    read_vec(r);
    read_vec(g);
    read_vec(b);
}

gamma_ramp gamma_desc::to_ramp() const
{
    gamma_ramp ramp;

    auto interp = [](const std::vector<pioint_t>& pts, uint16_t* out)
    {
        const size_t n = pts.size();
        if (n == 0)
        {
            for (int i = 0; i < GAMMA_RAMP_SIZE; i++)
                out[i] = (WORD)((double)i / (GAMMA_RAMP_SIZE - 1) * 65535.0);
            return;
        }
        if (n == 1)
        {
            WORD v = (WORD)(pts[0].imag() * 65535.0f);
            for (int i = 0; i < GAMMA_RAMP_SIZE; i++)
                out[i] = v;
            return;
        }

        // Extract x, y in normalized space
        std::vector<float> xs(n), ys(n);
        for (size_t k = 0; k < n; k++)
        {
            xs[k] = pts[k].real();
            ys[k] = pts[k].imag();
        }

        // Segment slopes
        std::vector<float> m(n - 1);
        for (size_t k = 0; k < n - 1; k++)
        {
            float dx = xs[k + 1] - xs[k];
            m[k] = dx > 0 ? (ys[k + 1] - ys[k]) / dx : 0;
        }

        // Tangents (Fritsch-Carlson)
        std::vector<float> d(n);
        d[0] = m[0];
        d[n - 1] = m[n - 2];
        for (size_t k = 1; k < n - 1; k++)
        {
            if (m[k - 1] * m[k] <= 0)
            {
                d[k] = 0;
            }
            else
            {
                float h0 = xs[k] - xs[k - 1];
                float h1 = xs[k + 1] - xs[k];
                float a = (1.0f + h1 / (h0 + h1)) / 3.0f;
                d[k] = (m[k - 1] * m[k]) / (a * m[k] + (1.0f - a) * m[k - 1]);
            }
        }

        // Evaluate Hermite spline
        for (int i = 0; i < GAMMA_RAMP_SIZE; i++)
        {
            float x = (float)i / (GAMMA_RAMP_SIZE - 1);
            if (x <= xs[0]) { out[i] = (WORD)(ys[0] * 65535.0f); continue; }
            if (x >= xs[n - 1]) { out[i] = (WORD)(ys[n - 1] * 65535.0f); continue; }

            size_t seg = 0;
            while (seg < n - 2 && x > xs[seg + 1]) seg++;

            float h = xs[seg + 1] - xs[seg];
            float t = (x - xs[seg]) / h;
            float t2 = t * t;
            float t3 = t2 * t;

            float y0 = ys[seg], y1 = ys[seg + 1];
            float m0 = d[seg] * h, m1 = d[seg + 1] * h;

            float v = (2.0f * t3 - 3.0f * t2 + 1.0f) * y0
                    + (t3 - 2.0f * t2 + t) * m0
                    + (-2.0f * t3 + 3.0f * t2) * y1
                    + (t3 - t2) * m1;
            if (v < 0) v = 0;
            if (v > 1.0f) v = 1.0f;
            out[i] = (WORD)(v * 65535.0f);
        }

        // Enforce monotonic
        for (int i = 1; i < GAMMA_RAMP_SIZE; i++)
            if (out[i] < out[i - 1])
                out[i] = out[i - 1];
    };

    interp(r, ramp.r);
    interp(g, ramp.g);
    interp(b, ramp.b);
    return ramp;
}

bool gamma_desc::to_file(const std::string& path) const
{
    std::ofstream fs(path, std::ios::binary);
    if (!fs)
    {
        std::cerr << "Cannot open for writing: " << path << std::endl;
        return false;
    }

    auto write_vec = [&fs](const std::vector<pioint_t>& v) {
        uint32_t count = (uint32_t)v.size();
        fs.write((const char*)&count, sizeof(count));
        fs.write((const char*)v.data(), count * sizeof(pioint_t));
    };
    write_vec(r);
    write_vec(g);
    write_vec(b);
    return fs.good();
}

/*gamma_panel*/
gamma_panel* gamma_panel::instance()
{
    static gamma_panel inst;
    return &inst;
}
std::vector<std::filesystem::path> gamma_panel::presets() const
{
    std::vector<std::filesystem::path> presets;
    std::error_code ec;
    for (auto& entry : std::filesystem::directory_iterator(std::filesystem::current_path() / "gamma", ec))
    {
        if (ec)
            break;
        if (!entry.is_regular_file())
            continue;
        auto ext = entry.path().extension().string();
        if (ext == ".gmd")
            presets.push_back(entry.path().filename());
    }
    std::sort(presets.begin(), presets.end());
    return presets;
}
std::vector<display_info> gamma_panel::displays()
{
    std::vector<display_info> displays;

    DISPLAY_DEVICEA dd = {sizeof(dd)};
    int devNum = 0;
    displays_.clear();

    while (EnumDisplayDevicesA(nullptr, devNum, &dd, 0))
    {
        if (dd.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP)
        {
            DISPLAY_DEVICEA monitor = {sizeof(monitor)};
            EnumDisplayDevicesA(dd.DeviceName, 0, &monitor, 0);

            display_info di;
            di.deviceName = dd.DeviceName;
            di.name = monitor.DeviceString[0] ? monitor.DeviceString : dd.DeviceString;
            di.index = devNum;
            displays.push_back(di);
            displays_[devNum] = dd.DeviceName;
        }
        devNum++;
        dd = {sizeof(dd)};
    }
    return displays;
}
bool gamma_panel::apply_preset(const std::filesystem::path& preset, int display_idx)
{
    gamma_desc desc(preset);
    gamma_ramp ramp = desc.to_ramp();

    return apply_ramp(ramp, display_idx);
}
bool gamma_panel::apply_ramp(const gamma_ramp& ramp, int display_idx)
{
    HDC hdc = CreateDCA(displays_.at(display_idx).c_str(), nullptr, nullptr, nullptr);
    if (!hdc)
    {
        std::cerr << "Error: Cannot access display " << displays_.at(display_idx) << std::endl;
        return false;
    }
    BOOL result = SetDeviceGammaRamp(hdc, const_cast<LPVOID>(static_cast<LPCVOID>(&ramp)));
    DWORD err = GetLastError();
    DeleteDC(hdc);

    if (!result)
    {
        std::cerr << "Error: SetDeviceGammaRamp failed (err " << err << ")" << std::endl;
        return false;
    }
    return true;
}
bool gamma_panel::reset_display(int display_idx)
{
    gamma_ramp identity;
    return apply_ramp(identity, display_idx);
}

gamma_ramp gamma_panel::current_gamma(int display_idx) const
{
    gamma_ramp ramp;
    HDC hdc = CreateDCA(displays_.at(display_idx).c_str(), nullptr, nullptr, nullptr);
    if (!hdc)
        return ramp;

    BOOL ok = GetDeviceGammaRamp(hdc, &ramp);
    DeleteDC(hdc);
    if (!ok)
    {
        std::cerr << "Error: GetDeviceGammaRamp failed" << std::endl;
    }
    return ramp;
}
