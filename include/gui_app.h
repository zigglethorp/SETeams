#pragma once

#include "duosort_engine.h"

#include <string>
#include <utility>
#include <vector>
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
    void OnBrowseApple(HWND hwnd);
    // Opens a folder picker and updates the selected Google Takeout source folder.
    void OnBrowseGoogle(HWND hwnd);
    // Validates UI input and launches a full DuoSort run.
    void OnRun();
    // Opens the Google Takeout site in the user's default browser.
    void OnOpenGoogleTakeout();
    // Opens the duplicate-review window after a scan.
    void OnReviewDuplicates();

    // Appends one line to the read-only log box.
    void AppendLog(const std::string& line);
    // Clears the log box before a new run starts.
    void ClearLog();
    // Reads and trims the Apple folder path from the edit control.
    std::string GetApplePathFromUi() const;
    // Reads and trims the Google folder path from the edit control.
    std::string GetGooglePathFromUi() const;
    // Rebuilds the flat review-pair list from the latest duplicate groups.
    void BuildReviewPairs();
    // Advances to the next reviewable pair, returning false if none remain.
    bool MoveToNextReviewPair(int direction);
    // Marks the current pair as intentionally kept and advances to another pair.
    void SkipCurrentReviewPair();
    // Refreshes review labels and repaints both image panes.
    void RefreshReviewWindow();
    // Handles confirmed deletion from the review window.
    void DeleteCurrentReviewPhoto(bool deleteLeft);
    // Draws one preview pane in the duplicate review window.
    void PaintReviewPane(HDC hdc, const RECT& bounds, size_t photoIndex, const char* title) const;
    // Opens or focuses the duplicate-review window.
    void OpenReviewWindow();
    // Bridges the static callback signature for the review window.
    static LRESULT CALLBACK StaticReviewWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    // Handles the duplicate-review window messages.
    LRESULT ReviewWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    // Bridges the static Win32 callback signature to the active C++ instance.
    static LRESULT CALLBACK StaticWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    // Handles the main window's message dispatch for buttons and lifecycle events.
    LRESULT WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    HWND hwndMain_ = nullptr;
    HWND hwndApplePath_ = nullptr;
    HWND hwndGooglePath_ = nullptr;
    HWND hwndLog_ = nullptr;
    HWND hwndReview_ = nullptr;
    HWND hwndReviewStatus_ = nullptr;
    DuoSortEngine engine_;
    std::vector<std::pair<size_t, size_t>> reviewPairs_;
    std::vector<bool> skippedReviewPairs_;
    size_t reviewPairIndex_ = 0;
    ULONG_PTR gdiplusToken_ = 0;
};
