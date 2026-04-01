#include "gui_app.h"

// Starts the Win32 GUI and keeps the executable entry point separate from app logic.
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
    DuoSortGuiApp app;
    return app.Run(hInstance, nCmdShow);
}
