// Source4_fixed.cpp: Full source with Gaming Mode fix and warnings resolved

#define _WIN32_WINNT 0x0600

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>            // SHGetFolderPathW
#include <mmdeviceapi.h>       // Core Audio
#include <endpointvolume.h>
#include <initguid.h>          // GUID_DEVCLASS_MOUSE, GUID_DEVCLASS_BLUETOOTH
#include <devguid.h>
#include <setupapi.h>          // SetupDi*
#include <powrprof.h>          // PowerGetActiveScheme, PowerSetActiveScheme
#include <vector>
#include <string>

#pragma comment(lib, "Setupapi.lib")
#pragma comment(lib, "PowrProf.lib")

// Custom tray-icon message
#define WM_TRAYICON  (WM_APP + 1)

// Menu command IDs
enum { ID_TRAY_EXIT = 100, ID_TRAY_RESTART, ID_TRAY_GAMING };
static UINT WM_TASKBARCREATED;

// Global state
HINSTANCE ghInst;
NOTIFYICONDATAW nid = {};

// INI section and keys
static const wchar_t* CFG_SECTION = L"General";
static const wchar_t* KEY_SHOW_LICENSE = L"ShowLicense";
static const wchar_t* KEY_SHOW_EXPL_WARN = L"ShowExplorerWarning";
static const wchar_t* KEY_GAMING_ENABLED = L"GamingEnabled";
static const wchar_t* KEY_PREV_POWER_SCHEME = L"PrevPowerScheme";
static const wchar_t* KEY_PREV_WIDTH = L"PrevWidth";
static const wchar_t* KEY_PREV_HEIGHT = L"PrevHeight";
static const wchar_t* KEY_PREV_REFRESH = L"PrevRefresh";
static const wchar_t* KEY_SELECTED_TITLE = L"SelectedProcessTitle";

// Config manager
class Config {
    std::wstring path_;
public:
    Config() {
        wchar_t buf[MAX_PATH];
        if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, SHGFP_TYPE_CURRENT, buf))) {
            path_ = buf;
            path_ += L"\\MyTrayApp";
            CreateDirectoryW(path_.c_str(), NULL);
            path_ += L"\\config.ini";
        }
    }
    bool getBool(const wchar_t* key, bool def) const {
        return GetPrivateProfileIntW(CFG_SECTION, key, def ? 1 : 0, path_.c_str()) != 0;
    }
    void setBool(const wchar_t* key, bool val) {
        WritePrivateProfileStringW(CFG_SECTION, key, val ? L"1" : L"0", path_.c_str());
    }
    std::wstring getString(const wchar_t* key, const wchar_t* def = L"") const {
        wchar_t buf[256];
        GetPrivateProfileStringW(CFG_SECTION, key, def, buf, _countof(buf), path_.c_str());
        return buf;
    }
    void setString(const wchar_t* key, const std::wstring& val) {
        WritePrivateProfileStringW(CFG_SECTION, key, val.c_str(), path_.c_str());
    }
    bool gamingEnabled() const { return getBool(KEY_GAMING_ENABLED, false); }
    void setGamingEnabled(bool f) { setBool(KEY_GAMING_ENABLED, f); }
    std::wstring prevPowerScheme() const { return getString(KEY_PREV_POWER_SCHEME); }
    void setPrevPowerScheme(const std::wstring& s) { setString(KEY_PREV_POWER_SCHEME, s); }
    int prevWidth() const { return _wtoi(getString(KEY_PREV_WIDTH).c_str()); }
    void setPrevWidth(int w) { setString(KEY_PREV_WIDTH, std::to_wstring(w)); }
    int prevHeight() const { return _wtoi(getString(KEY_PREV_HEIGHT).c_str()); }
    void setPrevHeight(int h) { setString(KEY_PREV_HEIGHT, std::to_wstring(h)); }
    int prevRefresh() const { return _wtoi(getString(KEY_PREV_REFRESH).c_str()); }
    void setPrevRefresh(int r) { setString(KEY_PREV_REFRESH, std::to_wstring(r)); }
    std::wstring selectedTitle() const { return getString(KEY_SELECTED_TITLE); }
    void setSelectedTitle(const std::wstring& t) { setString(KEY_SELECTED_TITLE, t); }
};

Config cfg;

// Forward declaration matching Windows headers
int WINAPI wWinMain(
    _In_     HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_     PWSTR    lpCmdLine,
    _In_     int      nCmdShow
);

// Undo Gaming Mode
void UndoGamingMode() {
    // Restore power scheme
    const std::wstring prev = cfg.prevPowerScheme();
    if (!prev.empty()) {
        GUID guid;
        if (CLSIDFromString(prev.c_str(), &guid) == S_OK) {
            PowerSetActiveScheme(NULL, &guid);
        }
    }
    // Restore resolution
    DEVMODEW dm = {};
    dm.dmSize = sizeof(dm);
    dm.dmPelsWidth = cfg.prevWidth();
    dm.dmPelsHeight = cfg.prevHeight();
    dm.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_DISPLAYFREQUENCY;
    dm.dmDisplayFrequency = cfg.prevRefresh();
    ChangeDisplaySettingsW(&dm, CDS_UPDATEREGISTRY);
    // Clear stored values
    cfg.setGamingEnabled(false);
}

// Restart Explorer cleanly
void RestartExplorer() {
    system("taskkill /F /IM explorer.exe");
    Sleep(2000);
    STARTUPINFOW si{ sizeof(si) };
    PROCESS_INFORMATION pi;
    if (CreateProcessW(L"C:\\Windows\\explorer.exe", NULL, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
}

// Gaming mode toggle
void GamingProc(HWND hwnd) {
    bool enable = !cfg.gamingEnabled();
    if (enable) {
        // Save current resolution and refresh
        DEVMODEW dm = {};
        dm.dmSize = sizeof(dm);
        EnumDisplaySettingsW(NULL, ENUM_CURRENT_SETTINGS, &dm);
        cfg.setPrevWidth(dm.dmPelsWidth);
        cfg.setPrevHeight(dm.dmPelsHeight);
        cfg.setPrevRefresh(dm.dmDisplayFrequency);
        // Set max refresh for 1280x720
        DEVMODEW dm2 = {};
        DWORD maxHz = dm.dmDisplayFrequency;
        for (DWORD i = 0; EnumDisplaySettingsW(NULL, i, &dm2); ++i) {
            if (dm2.dmPelsWidth == 1280 && dm2.dmPelsHeight == 720 && dm2.dmDisplayFrequency > maxHz) {
                maxHz = dm2.dmDisplayFrequency;
            }
        }
        dm.dmDisplayFrequency = maxHz;
        ChangeDisplaySettingsW(&dm, CDS_UPDATEREGISTRY);
        // Store power scheme
        GUID* pguid = NULL;
        PowerGetActiveScheme(NULL, &pguid);
        wchar_t buf[64];
        if (StringFromGUID2(*pguid, buf, _countof(buf)) > 0) {
            cfg.setPrevPowerScheme(buf);
        }
        LocalFree(pguid);
    }
    else {
        UndoGamingMode();
    }
    cfg.setGamingEnabled(enable);
    // Optional: Restart Explorer to apply DPI scaling changes
    RestartExplorer();
}

// Tray icon and menu
LRESULT CALLBACK TrayProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (uMsg == WM_TRAYICON) {
        if (LOWORD(lParam) == WM_RBUTTONUP) {
            POINT pt;
            GetCursorPos(&pt);
            HMENU menu = CreatePopupMenu();
            InsertMenuW(menu, -1, MF_BYPOSITION, ID_TRAY_GAMING,
                cfg.gamingEnabled() ? L"&Disable Gaming Mode" : L"&Enable Gaming Mode");
            InsertMenuW(menu, -1, MF_BYPOSITION, ID_TRAY_RESTART, L"&Restart Explorer");
            InsertMenuW(menu, -1, MF_BYPOSITION, ID_TRAY_EXIT, L"E&xit");
            SetForegroundWindow(hwnd);
            TrackPopupMenu(menu, TPM_BOTTOMALIGN | TPM_LEFTALIGN, pt.x, pt.y, 0, hwnd, NULL);
            DestroyMenu(menu);
        }
    }
    switch (uMsg) {
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case ID_TRAY_GAMING:
            GamingProc(hwnd);
            return 0;
        case ID_TRAY_RESTART:
            RestartExplorer();
            return 0;
        case ID_TRAY_EXIT:
            if (cfg.gamingEnabled()) {
                UndoGamingMode();
            }
            Shell_NotifyIconW(NIM_DELETE, &nid);
            PostQuitMessage(0);
            return 0;
        }
        break;
    case WM_DESTROY:
        Shell_NotifyIconW(NIM_DELETE, &nid);
        PostQuitMessage(0);
        return 0;
    }
    if (uMsg == WM_TASKBARCREATED) {
        Shell_NotifyIconW(NIM_ADD, &nid);
    }
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

// Application entry
int WINAPI wWinMain(
    _In_     HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_     PWSTR    lpCmdLine,
    _In_     int      nCmdShow
) {
    ghInst = hInstance;
    WM_TASKBARCREATED = RegisterWindowMessageW(L"TaskbarCreated");

    WNDCLASSW wc = {};
    wc.lpfnWndProc = TrayProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"MyTrayClass";
    RegisterClassW(&wc);

    HWND hwnd = CreateWindowW(L"MyTrayClass", L"", 0, 0, 0, 0, 0,
        HWND_MESSAGE, NULL, hInstance, NULL);

    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TRAYICON;
    nid.hIcon = LoadIconW(NULL, IDI_APPLICATION);
    wcscpy_s(nid.szTip, L"MyTrayApp");
    Shell_NotifyIconW(NIM_ADD, &nid);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    return 0;
}
