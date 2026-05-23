#include "tray.h"
#include "gamma.h"
#include "repl.h"

#include <windows.h>
#include <shellapi.h>
#include <thread>
#include <atomic>
#include <string>
#include <vector>
#include <map>
#include <iostream>
#include <filesystem>

namespace fs = std::filesystem;

#define IDM_RESET_ALL 1001
#define IDM_RESET_DISP 2000  // + idx into s_displayIndices
#define IDM_PRESET_ALL 3000  // + preset_index
#define IDM_PRESET_DISP 4000 // + preset_index * 100 + idx into s_displayIndices
#define IDM_OPEN_WEBUI 5001
#define IDM_QUIT 5002
#define WM_TRAYMSG (WM_APP + 1)

namespace
{

// ---- thread state ----
std::thread s_thread;
std::atomic<bool> s_running{false};
HWND s_hwnd = NULL;
int s_port = 9980;
UINT s_taskbarCreated = 0;

// ---- cached state for menu dispatch ----
std::vector<int> s_displayIndices;

// ---- narrow-to-wide helper ----
std::wstring ToWide(const std::string& s, UINT cp = CP_ACP)
{
    if (s.empty())
        return {};
    int len = MultiByteToWideChar(cp, 0, s.c_str(), (int)s.size(), NULL, 0);
    if (len <= 0)
        return {};
    std::wstring ws(len, L'\0');
    MultiByteToWideChar(cp, 0, s.c_str(), (int)s.size(), &ws[0], len);
    return ws;
}

// ---- lang ----
bool IsZhCn()
{
    LANGID id = GetUserDefaultUILanguage();
    return PRIMARYLANGID(id) == LANG_CHINESE;
}

const wchar_t* Tr(const char* key)
{
    static const std::map<std::string, std::pair<const wchar_t*, const wchar_t*>> tbl = {
        {"reset", {L"重置 ", L"Reset "}},
        {"reset_menu", {L"重置", L"Reset"}},
        {"all", {L"全部", L"All"}},
        {"no_presets", {L"没有预设", L"No Presets"}},
        {"open_webui", {L"打开网页界面", L"Open WebUI"}},
        {"quit", {L"退出", L"Quit"}},
    };
    auto it = tbl.find(key);
    if (it == tbl.end())
        return L"?";
    return IsZhCn() ? it->second.first : it->second.second;
}

// ---- tray icon ----
HICON LoadTrayIcon()
{
    WCHAR buf[MAX_PATH];
    GetModuleFileNameW(NULL, buf, MAX_PATH);
    fs::path exeDir = fs::path(buf).parent_path();

    std::wstring paths[] = {
        L"tray.ico",
        L"src/tray.ico",
        (exeDir / L"tray.ico").wstring(),
        (exeDir / L".." / L"src" / L"tray.ico").wstring(),
    };

    for (auto& p : paths)
    {
        HICON h = (HICON)LoadImageW(NULL, p.c_str(), IMAGE_ICON, 0, 0, LR_LOADFROMFILE);
        if (h)
            return h;
    }
    return LoadIcon(NULL, IDI_APPLICATION);
}

void AddTrayIcon(HWND hwnd)
{
    HICON hIcon = LoadTrayIcon();

    NOTIFYICONDATAW nid = {};
    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TRAYMSG;
    nid.hIcon = hIcon;
    wcscpy_s(nid.szTip, L"gapaNEXT");

    Shell_NotifyIconW(NIM_ADD, &nid);
    if (hIcon && hIcon != LoadIcon(NULL, IDI_APPLICATION))
        DestroyIcon(hIcon);
}

void RemoveTrayIcon(HWND hwnd)
{
    NOTIFYICONDATAW nid = {};
    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd;
    nid.uID = 1;
    Shell_NotifyIconW(NIM_DELETE, &nid);
}

// ---- context menu ----
void ShowContextMenu(HWND hwnd)
{
    auto* panel = gamma_panel::instance();
    auto displays = panel->displays();
    auto presets = panel->presets();

    s_displayIndices.clear();
    for (auto& d : displays)
        s_displayIndices.push_back(d.index);
    int ndisp = (int)displays.size();

    HMENU hMenu = CreatePopupMenu();
    int pos = 0;

    // ---- Reset submenu ----
    HMENU hResetSub = CreatePopupMenu();
    InsertMenuW(hResetSub, 0, MF_BYPOSITION | MF_STRING, IDM_RESET_ALL, Tr("all"));
    for (int i = 0; i < ndisp; i++)
    {
        std::wstring label = ToWide(displays[i].name);
        InsertMenuW(hResetSub, i + 1, MF_BYPOSITION | MF_STRING, IDM_RESET_DISP + i, label.c_str());
    }
    InsertMenuW(hMenu, pos++, MF_BYPOSITION | MF_STRING | MF_POPUP, (UINT_PTR)hResetSub, Tr("reset_menu"));

    // ---- separator ----
    InsertMenuW(hMenu, pos++, MF_BYPOSITION | MF_SEPARATOR, 0, NULL);

    // ---- Presets ----
    if (presets.empty())
    {
        InsertMenuW(hMenu, pos++, MF_BYPOSITION | MF_STRING | MF_GRAYED, 0, Tr("no_presets"));
    }
    else if (ndisp <= 1)
    {
        // ≤1 display: direct preset items apply to all
        for (size_t i = 0; i < presets.size(); i++)
        {
            std::wstring name = presets[i].stem().wstring();
            InsertMenuW(hMenu, pos++, MF_BYPOSITION | MF_STRING, IDM_PRESET_ALL + (int)i, name.c_str());
        }
    }
    else
    {
        // >1 display: each preset is a submenu with display choices
        for (size_t i = 0; i < presets.size(); i++)
        {
            HMENU hPresetSub = CreatePopupMenu();
            std::wstring name = presets[i].stem().wstring();

            InsertMenuW(hPresetSub, 0, MF_BYPOSITION | MF_STRING, IDM_PRESET_ALL + (int)i, Tr("all"));
            for (int j = 0; j < ndisp; j++)
            {
                std::wstring label = ToWide(displays[j].name);
                InsertMenuW(hPresetSub, j + 1, MF_BYPOSITION | MF_STRING, IDM_PRESET_DISP + (int)i * 100 + j,
                            label.c_str());
            }
            InsertMenuW(hMenu, pos++, MF_BYPOSITION | MF_STRING | MF_POPUP, (UINT_PTR)hPresetSub, name.c_str());
        }
    }

    // ---- separator ----
    InsertMenuW(hMenu, pos++, MF_BYPOSITION | MF_SEPARATOR, 0, NULL);

    // ---- Open WebUI ----
    InsertMenuW(hMenu, pos++, MF_BYPOSITION | MF_STRING, IDM_OPEN_WEBUI, Tr("open_webui"));

    // ---- Quit ----
    InsertMenuW(hMenu, pos++, MF_BYPOSITION | MF_STRING, IDM_QUIT, Tr("quit"));

    // Show
    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(hwnd);
    TrackPopupMenu(hMenu, TPM_LEFTALIGN | TPM_BOTTOMALIGN, pt.x, pt.y, 0, hwnd, NULL);
    DestroyMenu(hMenu);
}

void OpenWebUI()
{
    std::wstring url = L"http://localhost:" + std::to_wstring(s_port);
    ShellExecuteW(NULL, L"open", url.c_str(), NULL, NULL, SW_SHOW);
}

// ---- window proc ----
LRESULT CALLBACK TrayWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_TRAYMSG:
        if (lParam == WM_LBUTTONUP || lParam == WM_LBUTTONDBLCLK)
        {
            OpenWebUI();
        }
        else if (lParam == WM_RBUTTONUP)
        {
            ShowContextMenu(hwnd);
        }
        return 0;

    case WM_COMMAND:
    {
        int cmd = LOWORD(wParam);
        auto* panel = gamma_panel::instance();

        if (cmd == IDM_QUIT)
        {
            RequestQuit();
        }
        else if (cmd == IDM_OPEN_WEBUI)
        {
            OpenWebUI();
        }
        else if (cmd == IDM_RESET_ALL)
        {
            auto d = panel->displays();
            for (auto& di : d)
                panel->reset_display(di.index);
        }
        else if (cmd >= IDM_RESET_DISP && cmd < IDM_RESET_DISP + 1000)
        {
            int idx = cmd - IDM_RESET_DISP;
            if (idx < (int)s_displayIndices.size())
                panel->reset_display(s_displayIndices[idx]);
        }
        else if (cmd >= IDM_PRESET_DISP && cmd < IDM_PRESET_DISP + 10000)
        {
            int pi = (cmd - IDM_PRESET_DISP) / 100;
            int di = (cmd - IDM_PRESET_DISP) % 100;
            auto p = panel->presets();
            if (pi >= 0 && pi < (int)p.size() && di < (int)s_displayIndices.size())
                panel->apply_preset(std::filesystem::current_path() / "gamma" / p[pi], s_displayIndices[di]);
        }
        else if (cmd >= IDM_PRESET_ALL && cmd < IDM_PRESET_ALL + 1000)
        {
            int pi = cmd - IDM_PRESET_ALL;
            auto p = panel->presets();
            if (pi >= 0 && pi < (int)p.size())
            {
                auto d = panel->displays();
                for (auto& di : d)
                    panel->apply_preset(std::filesystem::current_path() / "gamma" / p[pi], di.index);
            }
        }
        return 0;
    }

    case WM_DESTROY:
        RemoveTrayIcon(hwnd);
        PostQuitMessage(0);
        return 0;

    default:
        if (msg == s_taskbarCreated)
        {
            AddTrayIcon(hwnd);
            return 0;
        }
        break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // anonymous namespace

// ---- public API ----

void RunTrayMenu(int port)
{
    if (s_running.exchange(true))
        return;
    s_port = port;

    s_thread = std::thread(
        []()
        {
            s_taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");

            WNDCLASSEXW wc = {};
            wc.cbSize = sizeof(wc);
            wc.lpfnWndProc = TrayWndProc;
            wc.hInstance = GetModuleHandleW(NULL);
            wc.lpszClassName = L"gapaNEXT_Tray";
            RegisterClassExW(&wc);

            s_hwnd = CreateWindowExW(0, L"gapaNEXT_Tray", L"", 0, 0, 0, 0, 0, NULL, NULL, wc.hInstance, NULL);
            AddTrayIcon(s_hwnd);

            MSG msg;
            while (GetMessageW(&msg, NULL, 0, 0))
            {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }

            s_hwnd = NULL;
        });
}

void StopTrayMenu()
{
    if (!s_running.exchange(false))
        return;

    if (s_hwnd)
        PostMessageW(s_hwnd, WM_QUIT, 0, 0);

    if (s_thread.joinable())
        s_thread.join();
}
