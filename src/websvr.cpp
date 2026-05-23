#include "websvr.h"
#include "crow.h"
#include "gamma.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <optional>
#include <algorithm>

#include "shellapi.h"

namespace fs = std::filesystem;

// ----- Helpers -----

static std::string ReadFileStr(const std::string& path)
{
    std::ifstream f(path, std::ios::binary);
    if (!f)
        return {};
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static std::string GetMimeType(const std::string& path)
{
    if (path.ends_with(".html") || path.ends_with(".htm"))
        return "text/html; charset=utf-8";
    if (path.ends_with(".js"))
        return "application/javascript";
    if (path.ends_with(".css"))
        return "text/css";
    if (path.ends_with(".json"))
        return "application/json";
    if (path.ends_with(".svg"))
        return "image/svg+xml";
    if (path.ends_with(".png"))
        return "image/png";
    if (path.ends_with(".jpg") || path.ends_with(".jpeg"))
        return "image/jpeg";
    if (path.ends_with(".ico"))
        return "image/x-icon";
    if (path.ends_with(".woff"))
        return "font/woff";
    if (path.ends_with(".woff2"))
        return "font/woff2";
    if (path.ends_with(".txt"))
        return "text/plain; charset=utf-8";
    return "application/octet-stream";
}

static crow::json::wvalue RampToJson(const gamma_ramp& ramp)
{
    std::vector<int> r(GAMMA_RAMP_SIZE), g(GAMMA_RAMP_SIZE), b(GAMMA_RAMP_SIZE);
    for (int i = 0; i < GAMMA_RAMP_SIZE; i++)
    {
        r[i] = ramp.r[i];
        g[i] = ramp.g[i];
        b[i] = ramp.b[i];
    }
    crow::json::wvalue j;
    j["red"] = std::move(r);
    j["green"] = std::move(g);
    j["blue"] = std::move(b);
    return j;
}

static std::optional<gamma_ramp> JsonToRamp(const crow::json::rvalue& j)
{
    if (!j.has("red") || !j.has("green") || !j.has("blue"))
        return std::nullopt;

    gamma_ramp ramp;
    int i = 0;
    for (auto& v : j["red"])
    {
        if (i < GAMMA_RAMP_SIZE)
            ramp.r[i] = (uint16_t)((int)v);
        i++;
    }
    i = 0;
    for (auto& v : j["green"])
    {
        if (i < GAMMA_RAMP_SIZE)
            ramp.g[i] = (uint16_t)((int)v);
        i++;
    }
    i = 0;
    for (auto& v : j["blue"])
    {
        if (i < GAMMA_RAMP_SIZE)
            ramp.b[i] = (uint16_t)((int)v);
        i++;
    }
    // Enforce monotonicity
    for (int k = 1; k < GAMMA_RAMP_SIZE; k++)
    {
        if (ramp.r[k] < ramp.r[k - 1])
            ramp.r[k] = ramp.r[k - 1];
        if (ramp.g[k] < ramp.g[k - 1])
            ramp.g[k] = ramp.g[k - 1];
        if (ramp.b[k] < ramp.b[k - 1])
            ramp.b[k] = ramp.b[k - 1];
    }
    return ramp;
}

// ----- Web server -----
crow::SimpleApp app;
void OpenWebUI(int port)
{
    if (port == 0) port = app.port();
    std::string url = "http://localhost:" + std::to_string(port);
    ShellExecuteA(NULL, "open", url.c_str(), NULL, NULL, SW_SHOWNORMAL);
}
void StopWebServer()
{
    app.stop();
}
void RunWebServer(int port)
{
    std::string webRoot = "dist";

    // Serve static files from dist/ with SPA fallback
    CROW_CATCHALL_ROUTE(app)
    (
        [webRoot](const crow::request& req, crow::response& res)
        {
            // Router sets res.code = 404 before calling catchall; override it
            res.code = 200;

            std::string path = req.url;
            // Strip query string
            auto pos = path.find('?');
            if (pos != std::string::npos)
                path = path.substr(0, pos);
            // Sanitize: prevent directory traversal
            if (path.find("..") != std::string::npos)
            {
                res.code = 403;
                return;
            }

            std::string filePath = webRoot + path;
            if (fs::exists(filePath) && fs::is_regular_file(filePath))
            {
                res.set_header("Content-Type", GetMimeType(path));
                res.write(ReadFileStr(filePath));
                return;
            }

            // SPA fallback: serve index.html for any unmatched route
            res.set_header("Content-Type", "text/html; charset=utf-8");
            res.write(ReadFileStr(webRoot + "/index.html"));
        });

    // API: list displays
    CROW_ROUTE(app, "/api/displays")
    (
        []()
        {
            auto displays = gamma_panel::instance()->displays();
            crow::json::wvalue j;
            std::vector<crow::json::wvalue> list;
            for (auto& d : displays)
            {
                crow::json::wvalue di;
                di["index"] = d.index;
                di["name"] = d.name;
                di["deviceName"] = d.deviceName;
                list.push_back(std::move(di));
            }
            j["displays"] = std::move(list);
            return j;
        });

    // API: get current gamma
    CROW_ROUTE(app, "/api/gamma/current")
    (
        []()
        {
            auto* panel = gamma_panel::instance();
            auto displays = panel->displays();
            crow::json::wvalue j;
            if (!displays.empty())
            {
                gamma_ramp ramp = panel->current_gamma(displays[0].index);
                j["ramp"] = RampToJson(ramp);
            }
            return j;
        });

    // API: apply gamma
    CROW_ROUTE(app, "/api/gamma/apply")
        .methods("POST"_method)(
            [](const crow::request& req)
            {
                auto body = crow::json::load(req.body);
                if (!body)
                {
                    crow::json::wvalue e;
                    e["ok"] = false;
                    e["error"] = "invalid json";
                    return e;
                }
                int displayIdx = body.has("display") ? (int)body["display"] : 0;
                auto ramp = JsonToRamp(body["ramp"]);
                if (!ramp)
                {
                    crow::json::wvalue e;
                    e["ok"] = false;
                    e["error"] = "invalid ramp data";
                    return e;
                }

                auto* panel = gamma_panel::instance();
                auto displays = panel->displays();
                int applied = 0, failed = 0;
                for (auto& d : displays)
                {
                    if (displayIdx != 0 && d.index + 1 != displayIdx)
                        continue;
                    if (panel->apply_ramp(*ramp, d.index))
                        applied++;
                    else
                        failed++;
                }
                crow::json::wvalue j;
                j["ok"] = (failed == 0);
                j["applied"] = applied;
                j["failed"] = failed;
                return j;
            });

    // API: reset gamma
    CROW_ROUTE(app, "/api/gamma/reset")
        .methods("POST"_method)(
            [](const crow::request& req)
            {
                auto body = crow::json::load(req.body);
                int displayIdx = body ? (body.has("display") ? (int)body["display"] : 0) : 0;

                auto* panel = gamma_panel::instance();
                auto displays = panel->displays();
                int applied = 0, failed = 0;
                for (auto& d : displays)
                {
                    if (displayIdx != 0 && d.index + 1 != displayIdx)
                        continue;
                    if (panel->reset_display(d.index))
                        applied++;
                    else
                        failed++;
                }
                crow::json::wvalue j;
                j["ok"] = (failed == 0);
                j["applied"] = applied;
                return j;
            });

    // API: list preset files
    CROW_ROUTE(app, "/api/files")
    (
        []()
        {
            auto presets = gamma_panel::instance()->presets();
            crow::json::wvalue j;
            std::vector<std::string> files;
            for (auto& p : presets)
                files.push_back(p.string());
            std::sort(files.begin(), files.end());
            j["files"] = std::move(files);
            return j;
        });

    // API: download preset as ramp JSON
    CROW_ROUTE(app, "/api/files/<string>")
    (
        [](const std::string& name)
        {
            gamma_desc desc(fs::current_path().concat("gamma") / name);
            gamma_ramp ramp = desc.to_ramp();
            return RampToJson(ramp);
        });

    // API: save ramp to file
    CROW_ROUTE(app, "/api/files/save")
        .methods("POST"_method)(
            [](const crow::request& req)
            {
                auto body = crow::json::load(req.body);
                if (!body || !body.has("ramp") || !body.has("name"))
                {
                    crow::json::wvalue e;
                    e["ok"] = false;
                    e["error"] = "invalid json";
                    return e;
                }
                std::string name = body["name"].s();
                auto ramp = JsonToRamp(body["ramp"]);
                if (!ramp)
                {
                    crow::json::wvalue e;
                    e["ok"] = false;
                    e["error"] = "invalid ramp data";
                    return e;
                }
                bool ok = ramp->to_file(name);
                crow::json::wvalue j;
                j["ok"] = ok;
                if (!ok)
                    j["error"] = "save failed";
                return j;
            });

    // API: get preset control points
    CROW_ROUTE(app, "/api/presets/<string>")
    (
        [](const std::string& name)
        {
            fs::path presetPath = fs::current_path() / "gamma" / name;
            gamma_desc desc(presetPath);
            crow::json::wvalue j;
            j["name"] = name;
            const char* keys[] = {"r", "g", "b"};
            for (int ch = 0; ch < 3; ch++)
            {
                auto& vec = (ch == 0) ? desc.r : (ch == 1) ? desc.g : desc.b;
                std::vector<crow::json::wvalue> pts;
                for (auto& p : vec)
                {
                    crow::json::wvalue pt;
                    pt[0] = (int)(p.real() * 255.0f + 0.5f);
                    pt[1] = (int)(p.imag() * 65535.0f + 0.5f);
                    pts.push_back(std::move(pt));
                }
                j["points"][keys[ch]] = std::move(pts);
            }
            return j;
        });

    // API: save preset (.gmd from control points)
    CROW_ROUTE(app, "/api/presets/save")
        .methods("POST"_method)(
            [](const crow::request& req)
            {
                auto body = crow::json::load(req.body);
                if (!body || !body.has("name") || !body.has("points"))
                {
                    crow::json::wvalue e;
                    e["ok"] = false;
                    e["error"] = "invalid json";
                    return e;
                }
                std::string name = body["name"].s();
                auto pts = body["points"];

                gamma_desc desc;
                const char* keys[] = {"r", "g", "b"};
                for (int ch = 0; ch < 3; ch++)
                {
                    auto& arr = pts[keys[ch]];
                    std::vector<gamma_desc::pioint_t> vec;
                    for (auto& pt : arr)
                    {
                        float nx = (float)(int)pt[0] / 255.0f;
                        float ny = (float)(int)pt[1] / 65535.0f;
                        vec.push_back({nx, ny});
                    }
                    if (ch == 0)
                        desc.r = std::move(vec);
                    else if (ch == 1)
                        desc.g = std::move(vec);
                    else
                        desc.b = std::move(vec);
                }

                fs::path gammaDir = fs::current_path() / "gamma";
                std::error_code ec;
                if (!fs::exists(gammaDir))
                    fs::create_directory(gammaDir, ec);
                fs::path presetPath = gammaDir / name;
                bool ok = desc.to_file(presetPath.string());
                crow::json::wvalue j;
                j["ok"] = ok;
                if (!ok)
                    j["error"] = "save failed";
                return j;
            });

    // API: get keybindings
    CROW_ROUTE(app, "/api/keybindings")
    (
        []()
        {
            fs::path path = fs::current_path() / "gapa.json";
            std::string content = ReadFileStr(path.string());
            if (content.empty())
            {
                crow::json::wvalue j;
                j["keybindings"] = std::vector<crow::json::wvalue>();
                return j;
            }
            auto json = crow::json::load(content);
            if (!json)
                return crow::json::wvalue();
            return crow::json::wvalue(json);
        });

    // API: save keybindings
    CROW_ROUTE(app, "/api/keybindings")
        .methods("POST"_method)(
            [](const crow::request& req)
            {
                auto body = crow::json::load(req.body);
                if (!body)
                {
                    crow::json::wvalue e;
                    e["ok"] = false;
                    e["error"] = "invalid json";
                    return e;
                }
                fs::path path = fs::current_path() / "gapa.json";
                std::ofstream f(path);
                if (!f)
                {
                    crow::json::wvalue e;
                    e["ok"] = false;
                    e["error"] = "cannot write";
                    return e;
                }
                f << req.body;
                crow::json::wvalue j;
                j["ok"] = f.good();
                f.flush();
                f.close();
                gamma_panel::instance()->apply_shortkeys();
                return j;
            });

    std::cout << "gapaNEXT web server starting on http://localhost:" << port << std::endl;
    std::cout << "Press Ctrl+C to stop." << std::endl;

#ifndef GAPA_REPL
    std::string url = "http://localhost:" + std::to_string(port);
    ShellExecuteA(NULL, "open", url.c_str(), NULL, NULL, SW_SHOWNORMAL);
#endif
    app.port(port).multithreaded().run();
}
