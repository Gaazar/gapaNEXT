#include "repl.h"
#include "websvr.h"
#include "gamma.h"
#include "tray.h"

#include <iostream>
#include <sstream>
#include <thread>
#include <atomic>

// ----- Quit signalling -----
static std::atomic<bool> s_quitRequested{false};
static HANDLE s_hMainThread = NULL;

void RequestQuit()
{
    s_quitRequested = true;
#ifdef GAPA_REPL
    if (s_hMainThread)
        CancelSynchronousIo(s_hMainThread);
#else
    StopWebServer();
#endif
}

// ----- Helpers -----

static void CmdApply(const std::string& name, int displayIdx)
{
    auto* panel = gamma_panel::instance();
    auto presets = panel->presets();
    auto displays = panel->displays();

    if (displayIdx < 0 || displayIdx > (int)displays.size())
    {
        std::cerr << "Invalid display number." << std::endl;
        return;
    }

    std::filesystem::path presetPath;
    for (const auto& p : presets)
    {
        if (p.string() == name || p.stem().string() == name)
        {
            presetPath = p;
            break;
        }
    }
    if (presetPath.empty())
    {
        std::cerr << "Preset not found: " << name << std::endl;
        return;
    }

    if (displayIdx == 0)
    {
        for (const auto& d : displays)
        {
            std::cout << "  " << d.name << " ... ";
            if (panel->apply_preset(presetPath, d.index))
                std::cout << "OK" << std::endl;
            else
                std::cout << "FAILED" << std::endl;
        }
    }
    else
    {
        const auto& d = displays[displayIdx - 1];
        std::cout << "  " << d.name << " ... ";
        if (panel->apply_preset(presetPath, d.index))
            std::cout << "OK" << std::endl;
        else
            std::cout << "FAILED" << std::endl;
    }
}

static void CmdReset(int displayIdx)
{
    auto* panel = gamma_panel::instance();
    auto displays = panel->displays();
    if (displayIdx < 0 || displayIdx > (int)displays.size())
    {
        std::cerr << "Invalid display number." << std::endl;
        return;
    }

    if (displayIdx == 0)
    {
        for (const auto& d : displays)
        {
            std::cout << "  " << d.name << " ... ";
            if (panel->reset_display(d.index))
                std::cout << "OK (linear gamma)" << std::endl;
            else
                std::cout << "FAILED" << std::endl;
        }
    }
    else
    {
        const auto& d = displays[displayIdx - 1];
        std::cout << "  " << d.name << " ... ";
        if (panel->reset_display(d.index))
            std::cout << "OK (linear gamma)" << std::endl;
        else
            std::cout << "FAILED" << std::endl;
    }
}

static void CmdStatus()
{
    std::cout << std::endl;
    auto* panel = gamma_panel::instance();
    auto displays = panel->displays();
    for (const auto& d : displays)
    {
        gamma_ramp ramp = panel->current_gamma(d.index);
        std::string info = "R[0]=" + std::to_string(ramp.r[0]) + " R[255]=" + std::to_string(ramp.r[255]);
        std::cout << "  " << d.name << " (" << d.deviceName << ") -> " << info << std::endl;
    }
    std::cout << std::endl;
}

static void CmdList()
{
    auto* panel = gamma_panel::instance();
    auto presets = panel->presets();
    auto displays = panel->displays();

    std::cout << std::endl;
    std::cout << "Displays:" << std::endl;
    if (displays.empty())
    {
        std::cout << "  (none)" << std::endl;
    }
    else
    {
        for (size_t i = 0; i < displays.size(); i++)
        {
            const auto& d = displays[i];
            std::cout << "  [" << (i + 1) << "] " << d.name << " (" << d.deviceName << ")" << std::endl;
        }
    }

    std::cout << std::endl;
    std::cout << "Presets:" << std::endl;
    if (presets.empty())
    {
        std::cout << "  (none)" << std::endl;
    }
    else
    {
        for (size_t i = 0; i < presets.size(); i++)
            std::cout << "  [" << (i + 1) << "] " << presets[i].string() << std::endl;
    }
    std::cout << std::endl;
}

static void CmdHelp()
{
    std::cout << std::endl;
    std::cout << "Commands:" << std::endl;
    std::cout << "  apply, a <preset> [D]  Apply named preset to display D (0=all)" << std::endl;
    std::cout << "  reset, r [D]          Reset display D to linear gamma (0=all)" << std::endl;
    std::cout << "  list, l               List available displays and presets" << std::endl;
    std::cout << "  status, st            Show current gamma per display" << std::endl;
    std::cout << "  help, h, ?            Show this help" << std::endl;
    std::cout << "  exit, quit, q         Safe exit (reset gamma + release)" << std::endl;
    std::cout << std::endl;
}

static void ShowUsage()
{
    std::cout << "Usage: gapaNEXT [port]  Start web UI server (default :9980)" << std::endl;
}

// ----- REPL -----

void RunREPL()
{
    s_hMainThread = OpenThread(THREAD_TERMINATE, FALSE, GetCurrentThreadId());

    std::cout << "gapaNEXT - GPU Gamma Ramp Switcher" << std::endl;
    std::cout << "Type 'help' for commands, 'exit' to quit." << std::endl;
    std::cout << std::endl;

    std::string line;
    for (;;)
    {
        std::cout << "gapaNEXT> " << std::flush;

        if (!std::getline(std::cin, line) || s_quitRequested)
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
        for (auto& c : cmd)
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
        if (cmd == "status" || cmd == "st")
        {
            CmdStatus();
            continue;
        }
        if (cmd == "list" || cmd == "l")
        {
            CmdList();
            continue;
        }
        if (cmd == "apply" || cmd == "a")
        {
            if (tokens.size() < 2)
            {
                std::cerr << "Usage: apply <preset> [display#]" << std::endl;
                continue;
            }
            int di = tokens.size() >= 3 ? atoi(tokens[2].c_str()) : 0;
            CmdApply(tokens[1], di);
            continue;
        }
        if (cmd == "reset" || cmd == "r")
        {
            int di = tokens.size() >= 2 ? atoi(tokens[1].c_str()) : 0;
            CmdReset(di);
            continue;
        }
        std::cerr << "Unknown command: " << tokens[0] << " (type 'help' for commands)" << std::endl;
    }
    if (s_hMainThread)
        CloseHandle(s_hMainThread);
}
