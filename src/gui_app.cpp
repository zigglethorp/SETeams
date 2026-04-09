#include "gui_app.h"

#include <algorithm>
#include <commdlg.h>
#include <filesystem>
#include <objidl.h>
#include <gdiplus.h>
#include <shellapi.h>
#include <shlobj.h>
#include <cstring>
#include <sstream>

using namespace std;

namespace {
// Keep Win32 control IDs centralized so both window procedures can route button clicks cleanly.
enum ControlIds {
    ID_BTN_BROWSE_APPLE = 1001,
    ID_BTN_RUN = 1002,
    ID_EDIT_APPLE_PATH = 1004,
    ID_EDIT_LOG = 1005,
    ID_BTN_BROWSE_GOOGLE = 1006,
    ID_EDIT_GOOGLE_PATH = 1007,
    ID_BTN_OPEN_TAKEOUT = 1008,
    ID_BTN_REVIEW = 1009,
    ID_REVIEW_STATUS = 2001,
    ID_REVIEW_PREV = 2002,
    ID_REVIEW_NEXT = 2003,
    ID_REVIEW_DELETE_LEFT = 2004,
    ID_REVIEW_DELETE_RIGHT = 2005,
    ID_REVIEW_SKIP = 2006
};

string Trim(const string& s) {
    size_t start = 0;
    while (start < s.size() && isspace(static_cast<unsigned char>(s[start]))) ++start;
    size_t end = s.size();
    while (end > start && isspace(static_cast<unsigned char>(s[end - 1]))) --end;
    return s.substr(start, end - start);
}

string BrowseForFolder(HWND owner, const char* title) {
    BROWSEINFOA bi = {};
    bi.hwndOwner = owner;
    bi.lpszTitle = title;
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;

    LPITEMIDLIST pidl = SHBrowseForFolderA(&bi);
    if (!pidl) return "";

    char path[MAX_PATH] = {0};
    string result;
    if (SHGetPathFromIDListA(pidl, path)) result = path;
    CoTaskMemFree(pidl);
    return result;
}

string BrowseForZipFile(HWND owner) {
    OPENFILENAMEA ofn = {};
    char buffer[MAX_PATH] = {0};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = "Google Takeout Archives (*.zip)\0*.zip\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = buffer;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = "Select Google Takeout zip archive";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;

    if (GetOpenFileNameA(&ofn)) return buffer;
    return "";
}

wstring ToWide(const string& text) {
    return filesystem::path(text).wstring();
}

string FormatPhotoSummary(const PhotoRecord& photo) {
    ostringstream out;
    out << photo.source << " | " << photo.fileName << " | " << photo.fileSize << " bytes";
    if (!photo.localPath.empty()) out << " | " << photo.localPath;
    if (photo.deleted) out << " | deleted";
    return out.str();
}

bool PhotoCanBeReviewed(const PhotoRecord& photo) {
    if (photo.deleted || photo.localPath.empty()) return false;
    error_code ec;
    return filesystem::exists(photo.localPath, ec) && filesystem::is_regular_file(photo.localPath, ec);
}

bool IsZipFilePath(const filesystem::path& path) {
    string ext = path.extension().string();
    transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch) {
        return static_cast<char>(tolower(ch));
    });
    return ext == ".zip";
}
} // namespace

int DuoSortGuiApp::Run(HINSTANCE hInstance, int nCmdShow) {
    engine_.SetLogger([this](const string& line) { AppendLog(line); });

    // GDI+ is only used for preview rendering in the duplicate review window.
    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    Gdiplus::GdiplusStartup(&gdiplusToken_, &gdiplusStartupInput, nullptr);

    WNDCLASSA wc = {};
    wc.lpfnWndProc = DuoSortGuiApp::StaticWndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = "DuoSortMvpWindow";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    RegisterClassA(&wc);

    WNDCLASSA reviewWc = {};
    reviewWc.lpfnWndProc = DuoSortGuiApp::StaticReviewWndProc;
    reviewWc.hInstance = hInstance;
    reviewWc.lpszClassName = "DuoSortReviewWindow";
    reviewWc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    reviewWc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    RegisterClassA(&reviewWc);

    hwndMain_ = CreateWindowA(
        wc.lpszClassName, "DuoSort MVP",
        WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX & ~WS_THICKFRAME,
        CW_USEDEFAULT, CW_USEDEFAULT, 980, 620,
        nullptr, nullptr, hInstance, this);
    if (!hwndMain_) return 1;

    ShowWindow(hwndMain_, nCmdShow);
    UpdateWindow(hwndMain_);

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    if (gdiplusToken_ != 0) Gdiplus::GdiplusShutdown(gdiplusToken_);
    return static_cast<int>(msg.wParam);
}

void DuoSortGuiApp::OnCreate(HWND hwnd) {
    CreateWindowA("STATIC", "Apple/iCloud folder:", WS_VISIBLE | WS_CHILD,
                  12, 12, 160, 20, hwnd, nullptr, nullptr, nullptr);

    hwndApplePath_ = CreateWindowExA(
        WS_EX_CLIENTEDGE, "EDIT", "", WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL,
        12, 34, 800, 24, hwnd, reinterpret_cast<HMENU>(ID_EDIT_APPLE_PATH), nullptr, nullptr);

    CreateWindowA("BUTTON", "Browse...", WS_VISIBLE | WS_CHILD,
                  824, 34, 120, 24, hwnd, reinterpret_cast<HMENU>(ID_BTN_BROWSE_APPLE), nullptr, nullptr);

    CreateWindowA("STATIC", "Google Takeout folder or zip:", WS_VISIBLE | WS_CHILD,
                  12, 68, 220, 20, hwnd, nullptr, nullptr, nullptr);

    hwndGooglePath_ = CreateWindowExA(
        WS_EX_CLIENTEDGE, "EDIT", "", WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL,
        12, 90, 640, 24, hwnd, reinterpret_cast<HMENU>(ID_EDIT_GOOGLE_PATH), nullptr, nullptr);

    CreateWindowA("BUTTON", "Browse Folder...", WS_VISIBLE | WS_CHILD,
                  664, 90, 130, 24, hwnd, reinterpret_cast<HMENU>(ID_BTN_BROWSE_GOOGLE), nullptr, nullptr);
    CreateWindowA("BUTTON", "Open Takeout Site", WS_VISIBLE | WS_CHILD,
                  806, 90, 138, 24, hwnd, reinterpret_cast<HMENU>(ID_BTN_OPEN_TAKEOUT), nullptr, nullptr);

    CreateWindowA("BUTTON", "Run", WS_VISIBLE | WS_CHILD,
                  12, 126, 90, 28, hwnd, reinterpret_cast<HMENU>(ID_BTN_RUN), nullptr, nullptr);
    CreateWindowA("BUTTON", "Review Duplicates", WS_VISIBLE | WS_CHILD,
                  114, 126, 150, 28, hwnd, reinterpret_cast<HMENU>(ID_BTN_REVIEW), nullptr, nullptr);

    hwndLog_ = CreateWindowExA(
        WS_EX_CLIENTEDGE, "EDIT", "",
        WS_VISIBLE | WS_CHILD | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL,
        12, 168, 932, 390, hwnd, reinterpret_cast<HMENU>(ID_EDIT_LOG), nullptr, nullptr);

    const string defaultApplePath = engine_.FindDefaultAppleFolder();
    if (!defaultApplePath.empty()) {
        SetWindowTextA(hwndApplePath_, defaultApplePath.c_str());
        engine_.SetAppleRoot(defaultApplePath);
        AppendLog("Default iCloud path found.");
    } else {
        AppendLog("No default iCloud path found. Click Browse... and choose it manually.");
    }

    const string defaultGooglePath = engine_.FindDefaultGoogleFolder();
    if (!defaultGooglePath.empty()) {
        SetWindowTextA(hwndGooglePath_, defaultGooglePath.c_str());
        engine_.SetGoogleRoot(defaultGooglePath);
        AppendLog("Default Google Takeout source found.");
    } else {
        AppendLog("No default Google Takeout source found. Choose an extracted folder or a Takeout zip manually.");
    }
    AppendLog("Need a new export? Open Google Takeout: https://takeout.google.com/settings/takeout/custom/photos");
}

void DuoSortGuiApp::OnBrowseApple(HWND hwnd) {
    const string picked = BrowseForFolder(hwnd, "Select iCloud / Apple Photos folder");
    if (!picked.empty()) {
        SetWindowTextA(hwndApplePath_, picked.c_str());
        engine_.SetAppleRoot(picked);
        AppendLog("Selected Apple folder: " + picked);
    }
}

void DuoSortGuiApp::OnBrowseGoogle(HWND hwnd) {
    // First try the simpler folder picker, then fall back to a zip picker if the user cancels.
    const string folder = BrowseForFolder(hwnd, "Select extracted Google Takeout folder or Google Photos folder");
    if (!folder.empty()) {
        SetWindowTextA(hwndGooglePath_, folder.c_str());
        engine_.SetGoogleRoot(folder);
        AppendLog("Selected Google Takeout folder: " + folder);
        return;
    }

    const string zipFile = BrowseForZipFile(hwnd);
    if (!zipFile.empty()) {
        SetWindowTextA(hwndGooglePath_, zipFile.c_str());
        engine_.SetGoogleRoot(zipFile);
        AppendLog("Selected Google Takeout zip: " + zipFile);
    }
}

void DuoSortGuiApp::OnOpenGoogleTakeout() {
    const char* url = "https://takeout.google.com/settings/takeout/custom/photos";
    const auto result = reinterpret_cast<intptr_t>(
        ShellExecuteA(hwndMain_, "open", url, nullptr, nullptr, SW_SHOWNORMAL));
    if (result <= 32) {
        AppendLog("Unable to open Google Takeout automatically. Visit: " + string(url));
    }
}

void DuoSortGuiApp::OnRun() {
    ClearLog();
    const string applePath = GetApplePathFromUi();
    const string googlePath = GetGooglePathFromUi();

    error_code ec;
    const bool hasApple = !applePath.empty() && filesystem::exists(applePath, ec) && filesystem::is_directory(applePath, ec);
    ec.clear();
    const filesystem::path googleFsPath(googlePath);
    const bool googleIsDirectory = !googlePath.empty() && filesystem::exists(googleFsPath, ec) &&
                                   filesystem::is_directory(googleFsPath, ec);
    ec.clear();
    const bool googleIsZip = !googlePath.empty() && filesystem::exists(googleFsPath, ec) &&
                             filesystem::is_regular_file(googleFsPath, ec) &&
                             IsZipFilePath(googleFsPath);

    if (!applePath.empty() && !hasApple) {
        AppendLog("Apple/iCloud folder is not valid: " + applePath);
        AppendLog("Please browse to your Apple/iCloud photo folder manually.");
    }
    if (!googlePath.empty() && !googleIsDirectory && !googleIsZip) {
        AppendLog("Google Takeout source is not valid: " + googlePath);
        AppendLog("Please browse to an extracted Takeout folder or a Google Takeout .zip file manually.");
    }
    if (!hasApple && !googleIsDirectory && !googleIsZip) {
        AppendLog("DuoSort could not find a valid Apple or Google source automatically.");
        AppendLog("Please use Browse... to choose your photo source manually.");
        return;
    }

    // Push only validated values into the engine so later code can assume these are usable sources.
    engine_.SetAppleRoot(hasApple ? applePath : "");
    engine_.SetGoogleRoot((googleIsDirectory || googleIsZip) ? googlePath : "");
    engine_.Run();
    BuildReviewPairs();
    if (reviewPairs_.empty()) {
        AppendLog("No reviewable duplicate photo pairs are available yet.");
    } else {
        AppendLog("Review pairs ready: " + to_string(reviewPairs_.size()) + ". Click Review Duplicates.");
    }
}

void DuoSortGuiApp::OnReviewDuplicates() {
    BuildReviewPairs();
    if (reviewPairs_.empty()) {
        MessageBoxA(hwndMain_,
                    "No reviewable duplicate pairs are available. Run a scan first, and make sure the duplicates are local files.",
                    "DuoSort Review",
                    MB_OK | MB_ICONINFORMATION);
        return;
    }
    OpenReviewWindow();
}

void DuoSortGuiApp::AppendLog(const string& line) {
    if (!hwndLog_) return;
    int currentLen = GetWindowTextLengthA(hwndLog_);
    SendMessageA(hwndLog_, EM_SETSEL, currentLen, currentLen);
    const string msg = line + "\r\n";
    SendMessageA(hwndLog_, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(msg.c_str()));
}

void DuoSortGuiApp::ClearLog() {
    if (hwndLog_) SetWindowTextA(hwndLog_, "");
}

string DuoSortGuiApp::GetApplePathFromUi() const {
    if (!hwndApplePath_) return "";
    char buffer[4096];
    GetWindowTextA(hwndApplePath_, buffer, static_cast<int>(sizeof(buffer)));
    return Trim(buffer);
}

string DuoSortGuiApp::GetGooglePathFromUi() const {
    if (!hwndGooglePath_) return "";
    char buffer[4096];
    GetWindowTextA(hwndGooglePath_, buffer, static_cast<int>(sizeof(buffer)));
    return Trim(buffer);
}

void DuoSortGuiApp::BuildReviewPairs() {
    reviewPairs_.clear();
    skippedReviewPairs_.clear();
    reviewPairIndex_ = 0;

    const auto& photos = engine_.Photos();
    // Flatten each duplicate group into pairwise comparisons so the review UI can stay simple.
    for (const auto& group : engine_.DuplicateGroups()) {
        if (group.size() < 2) continue;
        for (size_t i = 0; i < group.size(); ++i) {
            for (size_t j = i + 1; j < group.size(); ++j) {
                if (group[i] >= photos.size() || group[j] >= photos.size()) continue;
                if (!PhotoCanBeReviewed(photos[group[i]]) || !PhotoCanBeReviewed(photos[group[j]])) continue;
                reviewPairs_.push_back({group[i], group[j]});
            }
        }
    }

    skippedReviewPairs_.assign(reviewPairs_.size(), false);
}

bool DuoSortGuiApp::MoveToNextReviewPair(int direction) {
    if (reviewPairs_.empty()) return false;

    size_t attempts = 0;
    size_t index = reviewPairIndex_;
    const auto& photos = engine_.Photos();
    // Walk circularly through the pair list until we find one that still exists and was not skipped.
    while (attempts < reviewPairs_.size()) {
        const auto& pair = reviewPairs_[index];
        if (pair.first < photos.size() && pair.second < photos.size() &&
            PhotoCanBeReviewed(photos[pair.first]) && PhotoCanBeReviewed(photos[pair.second]) &&
            (index >= skippedReviewPairs_.size() || !skippedReviewPairs_[index])) {
            reviewPairIndex_ = index;
            return true;
        }

        if (direction >= 0) {
            index = (index + 1) % reviewPairs_.size();
        } else {
            index = (index == 0) ? reviewPairs_.size() - 1 : index - 1;
        }
        ++attempts;
    }

    return false;
}

void DuoSortGuiApp::SkipCurrentReviewPair() {
    if (reviewPairs_.empty() || reviewPairIndex_ >= skippedReviewPairs_.size()) return;

    // Skip means "keep both for now" rather than removing the pair permanently from duplicate detection.
    skippedReviewPairs_[reviewPairIndex_] = true;
    AppendLog("Skipped review pair " + to_string(reviewPairIndex_ + 1) + ".");

    size_t nextIndex = reviewPairIndex_;
    if (!reviewPairs_.empty()) nextIndex = (reviewPairIndex_ + 1) % reviewPairs_.size();
    reviewPairIndex_ = nextIndex;

    if (!MoveToNextReviewPair(+1)) {
        SetWindowTextA(hwndReviewStatus_, "All duplicate pairs have been reviewed or skipped.");
        InvalidateRect(hwndReview_, nullptr, TRUE);
        MessageBoxA(hwndReview_,
                    "This pair was kept. There are no more reviewable pairs right now.",
                    "DuoSort Review",
                    MB_OK | MB_ICONINFORMATION);
        return;
    }

    RefreshReviewWindow();
}

void DuoSortGuiApp::RefreshReviewWindow() {
    if (!hwndReview_ || !hwndReviewStatus_) return;

    if (!MoveToNextReviewPair(0)) {
        SetWindowTextA(hwndReviewStatus_, "No reviewable pairs remain.");
        InvalidateRect(hwndReview_, nullptr, TRUE);
        return;
    }

    const auto& pair = reviewPairs_[reviewPairIndex_];
    const auto& photos = engine_.Photos();
    const string status = "Pair " + to_string(reviewPairIndex_ + 1) + " of " + to_string(reviewPairs_.size()) +
                          " | Left: " + photos[pair.first].fileName +
                          " | Right: " + photos[pair.second].fileName;
    SetWindowTextA(hwndReviewStatus_, status.c_str());
    InvalidateRect(hwndReview_, nullptr, TRUE);
}

void DuoSortGuiApp::DeleteCurrentReviewPhoto(bool deleteLeft) {
    if (reviewPairs_.empty() || reviewPairIndex_ >= reviewPairs_.size()) return;

    const auto& pair = reviewPairs_[reviewPairIndex_];
    const size_t targetIndex = deleteLeft ? pair.first : pair.second;
    const auto& photos = engine_.Photos();
    if (targetIndex >= photos.size()) return;

    const string prompt = "Delete this photo?\n\n" + photos[targetIndex].localPath;
    if (MessageBoxA(hwndReview_, prompt.c_str(), "Confirm Delete", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) {
        return;
    }

    string message;
    if (!engine_.DeletePhoto(targetIndex, message)) {
        MessageBoxA(hwndReview_, message.c_str(), "Delete Failed", MB_OK | MB_ICONERROR);
        return;
    }

    AppendLog(message);
    // Rebuild review pairs after deletion so missing files disappear from the active queue immediately.
    BuildReviewPairs();
    if (reviewPairs_.empty()) {
        SetWindowTextA(hwndReviewStatus_, "No reviewable pairs remain.");
        InvalidateRect(hwndReview_, nullptr, TRUE);
        MessageBoxA(hwndReview_, "The selected file was deleted. No reviewable duplicate pairs remain.", "DuoSort Review", MB_OK | MB_ICONINFORMATION);
        return;
    }

    if (reviewPairIndex_ >= reviewPairs_.size()) reviewPairIndex_ = reviewPairs_.size() - 1;
    RefreshReviewWindow();
}

void DuoSortGuiApp::PaintReviewPane(HDC hdc, const RECT& bounds, size_t photoIndex, const char* title) const {
    const auto& photos = engine_.Photos();
    HBRUSH panelBrush = CreateSolidBrush(RGB(245, 245, 245));
    FillRect(hdc, &bounds, panelBrush);
    DeleteObject(panelBrush);
    FrameRect(hdc, &bounds, reinterpret_cast<HBRUSH>(GetStockObject(GRAY_BRUSH)));

    TextOutA(hdc, bounds.left + 8, bounds.top + 8, title, static_cast<int>(strlen(title)));
    if (photoIndex >= photos.size()) return;

    const auto& photo = photos[photoIndex];
    const string summary = FormatPhotoSummary(photo);
    TextOutA(hdc, bounds.left + 8, bounds.top + 30, summary.c_str(), static_cast<int>(summary.size()));

    if (!PhotoCanBeReviewed(photo)) {
        const char* unavailable = "Preview unavailable.";
        TextOutA(hdc, bounds.left + 8, bounds.top + 54, unavailable, static_cast<int>(strlen(unavailable)));
        return;
    }

    Gdiplus::Graphics graphics(hdc);
    graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    unique_ptr<Gdiplus::Image> image(Gdiplus::Image::FromFile(ToWide(photo.localPath).c_str(), FALSE));
    if (!image || image->GetLastStatus() != Gdiplus::Ok) {
        const char* unavailable = "Preview unavailable for this file type.";
        TextOutA(hdc, bounds.left + 8, bounds.top + 54, unavailable, static_cast<int>(strlen(unavailable)));
        return;
    }

    const int top = bounds.top + 58;
    const int availableWidth = max(1, static_cast<int>((bounds.right - bounds.left) - 16));
    const int availableHeight = max(1, static_cast<int>((bounds.bottom - top) - 12));
    const double imageWidth = static_cast<double>(image->GetWidth());
    const double imageHeight = static_cast<double>(image->GetHeight());
    if (imageWidth <= 0.0 || imageHeight <= 0.0) return;

    // Fit the image into the pane while keeping aspect ratio intact.
    const double scale = min(static_cast<double>(availableWidth) / imageWidth,
                             static_cast<double>(availableHeight) / imageHeight);
    const int drawWidth = max(1, static_cast<int>(imageWidth * scale));
    const int drawHeight = max(1, static_cast<int>(imageHeight * scale));
    const int x = bounds.left + 8 + (availableWidth - drawWidth) / 2;
    const int y = top + (availableHeight - drawHeight) / 2;
    graphics.DrawImage(image.get(), x, y, drawWidth, drawHeight);
}

void DuoSortGuiApp::OpenReviewWindow() {
    if (hwndReview_) {
        ShowWindow(hwndReview_, SW_SHOW);
        SetForegroundWindow(hwndReview_);
        RefreshReviewWindow();
        return;
    }

    hwndReview_ = CreateWindowA(
        "DuoSortReviewWindow",
        "DuoSort Duplicate Review",
        WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 1220, 760,
        hwndMain_, nullptr, GetModuleHandleA(nullptr), this);
    if (!hwndReview_) return;

    ShowWindow(hwndReview_, SW_SHOW);
    UpdateWindow(hwndReview_);
    RefreshReviewWindow();
}

LRESULT CALLBACK DuoSortGuiApp::StaticReviewWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    DuoSortGuiApp* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTA*>(lParam);
        self = static_cast<DuoSortGuiApp*>(cs->lpCreateParams);
        SetWindowLongPtrA(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<DuoSortGuiApp*>(GetWindowLongPtrA(hwnd, GWLP_USERDATA));
    }

    if (!self) return DefWindowProcA(hwnd, msg, wParam, lParam);
    return self->ReviewWndProc(hwnd, msg, wParam, lParam);
}

LRESULT DuoSortGuiApp::ReviewWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE:
            // The review window is intentionally lightweight: controls live here, image panes are custom-painted.
            hwndReviewStatus_ = CreateWindowA(
                "STATIC", "Review duplicates side by side.", WS_VISIBLE | WS_CHILD,
                12, 12, 1180, 22, hwnd, reinterpret_cast<HMENU>(ID_REVIEW_STATUS), nullptr, nullptr);
            CreateWindowA("BUTTON", "Previous", WS_VISIBLE | WS_CHILD,
                          12, 44, 110, 28, hwnd, reinterpret_cast<HMENU>(ID_REVIEW_PREV), nullptr, nullptr);
            CreateWindowA("BUTTON", "Next", WS_VISIBLE | WS_CHILD,
                          130, 44, 110, 28, hwnd, reinterpret_cast<HMENU>(ID_REVIEW_NEXT), nullptr, nullptr);
            CreateWindowA("BUTTON", "Delete Left", WS_VISIBLE | WS_CHILD,
                          248, 44, 120, 28, hwnd, reinterpret_cast<HMENU>(ID_REVIEW_DELETE_LEFT), nullptr, nullptr);
            CreateWindowA("BUTTON", "Delete Right", WS_VISIBLE | WS_CHILD,
                          376, 44, 120, 28, hwnd, reinterpret_cast<HMENU>(ID_REVIEW_DELETE_RIGHT), nullptr, nullptr);
            CreateWindowA("BUTTON", "Skip / Keep Both", WS_VISIBLE | WS_CHILD,
                          504, 44, 150, 28, hwnd, reinterpret_cast<HMENU>(ID_REVIEW_SKIP), nullptr, nullptr);
            return 0;

        case WM_COMMAND: {
            const int id = LOWORD(wParam);
            if (id == ID_REVIEW_PREV) {
                if (MoveToNextReviewPair(-1)) RefreshReviewWindow();
                return 0;
            }
            if (id == ID_REVIEW_NEXT) {
                if (MoveToNextReviewPair(+1)) RefreshReviewWindow();
                return 0;
            }
            if (id == ID_REVIEW_DELETE_LEFT) {
                DeleteCurrentReviewPhoto(true);
                return 0;
            }
            if (id == ID_REVIEW_DELETE_RIGHT) {
                DeleteCurrentReviewPhoto(false);
                return 0;
            }
            if (id == ID_REVIEW_SKIP) {
                SkipCurrentReviewPair();
                return 0;
            }
            return 0;
        }

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);

            RECT clientRect;
            GetClientRect(hwnd, &clientRect);
            // Split the client area into two preview panes for direct visual comparison.
            RECT leftPane = {12, 86, (clientRect.right / 2) - 6, clientRect.bottom - 12};
            RECT rightPane = {(clientRect.right / 2) + 6, 86, clientRect.right - 12, clientRect.bottom - 12};

            if (!reviewPairs_.empty() && reviewPairIndex_ < reviewPairs_.size()) {
                const auto& pair = reviewPairs_[reviewPairIndex_];
                PaintReviewPane(hdc, leftPane, pair.first, "Left Photo");
                PaintReviewPane(hdc, rightPane, pair.second, "Right Photo");
            } else {
                HBRUSH brush = CreateSolidBrush(RGB(245, 245, 245));
                FillRect(hdc, &leftPane, brush);
                FillRect(hdc, &rightPane, brush);
                DeleteObject(brush);
                const char* message = "No reviewable pairs available.";
                TextOutA(hdc, 20, 100, message, static_cast<int>(strlen(message)));
            }

            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY:
            hwndReview_ = nullptr;
            hwndReviewStatus_ = nullptr;
            return 0;

        default:
            return DefWindowProcA(hwnd, msg, wParam, lParam);
    }
}

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

LRESULT DuoSortGuiApp::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE:
            OnCreate(hwnd);
            return 0;

        case WM_COMMAND: {
            const int id = LOWORD(wParam);
            if (id == ID_BTN_BROWSE_APPLE) OnBrowseApple(hwnd);
            if (id == ID_BTN_BROWSE_GOOGLE) OnBrowseGoogle(hwnd);
            if (id == ID_BTN_OPEN_TAKEOUT) OnOpenGoogleTakeout();
            if (id == ID_BTN_RUN) OnRun();
            if (id == ID_BTN_REVIEW) OnReviewDuplicates();
            return 0;
        }

        case WM_DESTROY:
            if (hwndReview_) DestroyWindow(hwndReview_);
            PostQuitMessage(0);
            return 0;

        default:
            return DefWindowProcA(hwnd, msg, wParam, lParam);
    }
}
