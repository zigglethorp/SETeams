#include "gui_app.h"

#include <filesystem>
#include <shlobj.h>

using namespace std;

namespace {
// Assigns stable IDs so button clicks can be routed in the window procedure.
enum ControlIds {
    ID_BTN_BROWSE = 1001,
    ID_BTN_RUN = 1002,
    ID_BTN_EXPORT = 1003,
    ID_EDIT_PATH = 1004,
    ID_EDIT_LOG = 1005
};

// Removes leading and trailing whitespace from text read out of Win32 controls.
string Trim(const string& s) {
    size_t start = 0;
    while (start < s.size() && isspace(static_cast<unsigned char>(s[start]))) ++start;
    size_t end = s.size();
    while (end > start && isspace(static_cast<unsigned char>(s[end - 1]))) --end;
    return s.substr(start, end - start);
}

// Opens the classic Windows folder picker used to choose the Apple/iCloud directory.
string BrowseForFolder(HWND owner) {
    BROWSEINFOA bi = {};
    bi.hwndOwner = owner;
    bi.lpszTitle = "Select iCloud / Apple Photos folder";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;

    LPITEMIDLIST pidl = SHBrowseForFolderA(&bi);
    if (!pidl) return "";

    char path[MAX_PATH] = {0};
    string result;
    if (SHGetPathFromIDListA(pidl, path)) result = path;
    CoTaskMemFree(pidl);
    return result;
}
} // namespace

// Registers the window class, creates the UI, and processes messages until exit.
int DuoSortGuiApp::Run(HINSTANCE hInstance, int nCmdShow) {
    engine_.SetLogger([this](const string& line) { AppendLog(line); });

    WNDCLASSA wc = {};
    wc.lpfnWndProc = DuoSortGuiApp::StaticWndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = "DuoSortMvpWindow";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    RegisterClassA(&wc);

    hwndMain_ = CreateWindowA(
        wc.lpszClassName, "DuoSort MVP",
        WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX & ~WS_THICKFRAME,
        CW_USEDEFAULT, CW_USEDEFAULT, 680, 480,
        nullptr, nullptr, hInstance, this);
    if (!hwndMain_) return 1;

    ShowWindow(hwndMain_, nCmdShow);
    UpdateWindow(hwndMain_);

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return static_cast<int>(msg.wParam);
}

// Builds the UI controls and preloads a likely default iCloud folder if one exists.
void DuoSortGuiApp::OnCreate(HWND hwnd) {
    CreateWindowA("STATIC", "Apple/iCloud folder:", WS_VISIBLE | WS_CHILD,
                  12, 12, 140, 20, hwnd, nullptr, nullptr, nullptr);

    hwndPath_ = CreateWindowExA(
        WS_EX_CLIENTEDGE, "EDIT", "", WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL,
        12, 34, 540, 24, hwnd, reinterpret_cast<HMENU>(ID_EDIT_PATH), nullptr, nullptr);

    CreateWindowA("BUTTON", "Browse...", WS_VISIBLE | WS_CHILD,
                  560, 34, 90, 24, hwnd, reinterpret_cast<HMENU>(ID_BTN_BROWSE), nullptr, nullptr);
    CreateWindowA("BUTTON", "Run", WS_VISIBLE | WS_CHILD,
                  12, 66, 90, 26, hwnd, reinterpret_cast<HMENU>(ID_BTN_RUN), nullptr, nullptr);
    CreateWindowA("BUTTON", "Export Links", WS_VISIBLE | WS_CHILD,
                  110, 66, 110, 26, hwnd, reinterpret_cast<HMENU>(ID_BTN_EXPORT), nullptr, nullptr);

    hwndLog_ = CreateWindowExA(
        WS_EX_CLIENTEDGE, "EDIT", "",
        WS_VISIBLE | WS_CHILD | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL,
        12, 100, 638, 320, hwnd, reinterpret_cast<HMENU>(ID_EDIT_LOG), nullptr, nullptr);

    const string defaultPath = engine_.FindDefaultAppleFolder();
    if (!defaultPath.empty()) {
        SetWindowTextA(hwndPath_, defaultPath.c_str());
        engine_.SetAppleRoot(defaultPath);
        AppendLog("Default iCloud path found.");
    } else {
        AppendLog("No default iCloud path found. Click Browse...");
    }
    AppendLog("Place google_photos.csv next to duosort.exe for Google metadata.");
}

// Lets the user choose a folder and reflects that selection in the UI and engine.
void DuoSortGuiApp::OnBrowse(HWND hwnd) {
    const string picked = BrowseForFolder(hwnd);
    if (!picked.empty()) {
        SetWindowTextA(hwndPath_, picked.c_str());
        engine_.SetAppleRoot(picked);
        AppendLog("Selected folder: " + picked);
    }
}

// Validates the selected folder and launches the main engine workflow.
void DuoSortGuiApp::OnRun() {
    ClearLog();
    const string selected = GetPathFromUi();
    if (selected.empty()) {
        AppendLog("Please select an Apple/iCloud folder first.");
        return;
    }

    error_code ec;
    if (!filesystem::exists(selected, ec) || !filesystem::is_directory(selected, ec)) {
        AppendLog("Selected Apple/iCloud folder is not valid: " + selected);
        return;
    }

    engine_.SetAppleRoot(selected);
    engine_.Run();
}

// Exports manual-review links for the Google rows in duplicate groups.
void DuoSortGuiApp::OnExport() {
    engine_.ExportGoogleManualLinks();
}

// Adds one line to the scrolling edit control used as the app log surface.
void DuoSortGuiApp::AppendLog(const string& line) {
    if (!hwndLog_) return;
    int currentLen = GetWindowTextLengthA(hwndLog_);
    SendMessageA(hwndLog_, EM_SETSEL, currentLen, currentLen);
    const string msg = line + "\r\n";
    SendMessageA(hwndLog_, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(msg.c_str()));
}

// Clears the log box so the next run starts with a clean output area.
void DuoSortGuiApp::ClearLog() {
    if (hwndLog_) SetWindowTextA(hwndLog_, "");
}

// Reads the current folder path from the edit control and trims surrounding whitespace.
string DuoSortGuiApp::GetPathFromUi() const {
    if (!hwndPath_) return "";
    char buffer[4096];
    GetWindowTextA(hwndPath_, buffer, static_cast<int>(sizeof(buffer)));
    return Trim(buffer);
}

// Stores the object pointer on window creation and forwards future messages to it.
LRESULT CALLBACK DuoSortGuiApp::StaticWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    DuoSortGuiApp* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTA*>(lParam);
        self = static_cast<DuoSortGuiApp*>(cs->lpCreateParams);
        SetWindowLongPtrA(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<DuoSortGuiApp*>(GetWindowLongPtrA(hwnd, GWLP_USERDATA));
    }

    if (!self) return DefWindowProcA(hwnd, msg, wParam, lParam);
    return self->WndProc(hwnd, msg, wParam, lParam);
}

// Responds to window lifecycle and button-command messages for the main form.
LRESULT DuoSortGuiApp::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE:
            OnCreate(hwnd);
            return 0;
        case WM_COMMAND: {
            const int id = LOWORD(wParam);
            if (id == ID_BTN_BROWSE) OnBrowse(hwnd);
            if (id == ID_BTN_RUN) OnRun();
            if (id == ID_BTN_EXPORT) OnExport();
            return 0;
        }
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcA(hwnd, msg, wParam, lParam);
    }
}
