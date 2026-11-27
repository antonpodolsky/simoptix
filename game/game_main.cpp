// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// simoptix_game: a demo game for the headset that shows a grid to judge sharpness by, so that ways of
// drawing each eye can be tried without LMU. It is an ordinary OpenXR program: a D3D11 session, one
// swapchain per eye in sRGB, as LMU makes them, and a grid of 10-degree cells fixed in the room
// (game_grid.h). Each eye can be drawn at its own size, as a share of the size the headset recommends.
// It runs in a console window, which takes the keys: Space centres the grid ahead, Q or Esc quits.
//
// Usage: simoptix_game.exe [--left-scale <0.1..2>] [--right-scale <0.1..2>]
//                          [--loader <path to openxr_loader.dll>]
// (A scale of 0.5 draws that eye at half the width and half the height. The loader is SteamVR's or Pimax's
// by default.)
#include <windows.h>
#include <conio.h>

#include <atomic>
#include <cstdio>
#include <string>

#include "game_grid.h"
#include "game_openxr.h"
#include "game_settings.h"

namespace {

    std::atomic<bool> g_quitAsked{false};

    BOOL WINAPI OnConsoleClose(DWORD) {
        g_quitAsked = true;
        return TRUE;
    }

    // Reads the keys typed into the console: centres the grid on the head, or asks to quit.
    void ReadKeys(const GameOpenXr& openxr, float* chartYaw) {
        while (_kbhit()) {
            const int key = _getch();

            if (key == ' ') {
                *chartYaw = openxr.HeadYaw();
            } else if (key == 'q' || key == 'Q' || key == 27) {
                g_quitAsked = true;
            }
        }
    }

}

int wmain(int argc, wchar_t** argv) {
    // Each line shows at once, also when the output goes to a file or another program.
    setvbuf(stdout, nullptr, _IONBF, 0);
    GameSettings settings;
    std::string error;

    if (!ParseSettings(argc, argv, &settings, &error)) {
        printf("%s\n%s\n", error.c_str(), SettingsUsage());
        return 2;
    }

    GameOpenXr openxr;
    GameGrid grid;

    if (!openxr.Open(settings, &error) || !grid.Init(openxr.Device(), &error)) {
        printf("%s\n", error.c_str());
        return 1;
    }

    SetConsoleCtrlHandler(OnConsoleClose, TRUE);
    printf("waiting for the headset's session (Q or Esc quits)\n");
    float chartYaw = 0;
    bool exitAsked = false;

    while (!openxr.ShouldExit()) {
        openxr.PollEvents();
        ReadKeys(openxr, &chartYaw);

        if (g_quitAsked && !exitAsked) {
            openxr.RequestExit();
            exitAsked = true;
        }

        // xrWaitFrame paces a running session. Before it runs, or after it stops, don't spin.
        if (!openxr.IsRunning()) {
            Sleep(50);
            continue;
        }

        openxr.RunFrame(grid, chartYaw);
    }

    return 0;
}
