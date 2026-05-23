#include <string>
#include <vector>
#include <stdint.h>
#include <filesystem>
#include <map>
#include <complex>

constexpr int GAMMA_RAMP_SIZE = 256;

struct gamma_ramp
{
    uint16_t r[GAMMA_RAMP_SIZE];
    uint16_t g[GAMMA_RAMP_SIZE];
    uint16_t b[GAMMA_RAMP_SIZE];

    gamma_ramp();
    gamma_ramp(gamma_ramp&&) = default;
    gamma_ramp& operator=(gamma_ramp&&) = default;
    gamma_ramp(std::filesystem::path path);
    bool to_file(const std::string& path) const;
    void set_identity();
};
struct gamma_desc
{
    using pioint_t = std::complex<float>;
    std::vector<pioint_t> r, g, b;

    gamma_desc() : r{0, 1}, g{0, 1}, b{0, 1} {};
    gamma_desc(std::filesystem::path path);
    gamma_ramp to_ramp() const;
    bool to_file(const std::string& path) const;
};
struct display_info
{
    int index;
    std::string name;
    std::string deviceName;
};

struct gamma_panel
{

  private:
    std::filesystem::path current_preset_;
    std::map<int, std::string> displays_;

  public:
    static gamma_panel* instance();
    static void release();
    std::vector<std::filesystem::path> presets() const;
    std::vector<display_info> displays();
    bool apply_preset(const std::filesystem::path& preset, int display_idx);
    bool apply_ramp(const gamma_ramp& ramp, int display_idx);
    bool reset_display(int display_idx);
    std::filesystem::path current_preset() const
    {
        return current_preset_;
    }
    gamma_ramp current_gamma(int display_idx) const;
    void apply_shortkeys();
};

