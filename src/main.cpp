#include "repl.h"
#include "websvr.h"
#include "gamma.h"
#include "tray.h"

#include <thread>
#include <iostream>
#include <windows.h>

int main(int argc, char* argv[])
{
    int port = 9980;
    if (argc >= 2)
        port = atoi(argv[1]);
    if (port < 1 || port > 65535)
        port = 9980;

    HANDLE hMutex = CreateMutexA(NULL, FALSE, "gapaNEXT_SingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        OpenWebUI(port);
        CloseHandle(hMutex);
        return 0;
    }

    gamma_panel::instance()->apply_shortkeys();
    RunTrayMenu(port);
#ifdef GAPA_REPL
    std::thread webThread([port]() { RunWebServer(port); });
    RunREPL();
    StopWebServer();
    webThread.join();
#else
    RunWebServer(port);
#endif
    StopTrayMenu();

    std::cout << "Shutting down safely... ";
    gamma_panel::release();
    std::cout << "OK." << std::endl;

    return 0;
}