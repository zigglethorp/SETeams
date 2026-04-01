#pragma once

#include "duosort_engine.h"

#include <string>
#include <windows.h>

// Thin Win32 presentation layer for MVP demos.
// Responsibilities:
// 1) Render and manage basic controls.
// 2) Forward user actions to DuoSortEngine.
// 3) Display structured logs returned by the engine.
class DuoSortGuiApp {
public:
    // Creates the main window and runs the standard Win32 message loop.
    int Run(HINSTANCE hInstance, int nCmdShow);

private:
    // Creates the controls and seeds the initial path/log text when the window opens.
    void OnCreate(HWND hwnd);
    // Opens a folder picker and updates the selected Apple source folder.
    void OnBrowse(HWND hwnd);
    // Validates UI input and launches a full DuoSort run.
    void OnRun();
    // Writes manual Google delete links for the most recent duplicate results.
    void OnExport();

    // Appends one line to the read-only log box.
    void AppendLog(const std::string& line);
    // Clears the log box before a new run starts.
    void ClearLog();
    // Reads and trims the Apple folder path from the edit control.
    std::string GetPathFromUi() const;

    // Bridges the static Win32 callback signature to the active C++ instance.
    static LRESULT CALLBACK StaticWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    // Handles the main window's message dispatch for buttons and lifecycle events.
    LRESULT WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    HWND hwndMain_ = nullptr;
    HWND hwndPath_ = nullptr;
    HWND hwndLog_ = nullptr;
    DuoSortEngine engine_;
};
