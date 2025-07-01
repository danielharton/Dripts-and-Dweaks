#define _WIN32_WINNT 0x0600

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>            // SHGetFolderPathW
#include <mmdeviceapi.h>       // Core Audio
#include <endpointvolume.h>
#include <initguid.h>          // GUID_DEVCLASS_MOUSE, GUID_DEVCLASS_BLUETOOTH
#include <devguid.h>
#include <setupapi.h>          // SetupDi*
#include <powrprof.h>          // SetSuspendState
#include <bluetoothapis.h>     // Bluetooth API functions
#include <fstream>
#include <string>

#pragma comment(lib, "Setupapi.lib")
#pragma comment(lib, "PowrProf.lib")
#pragma comment(lib, "Ole32.lib")
#pragma comment(lib, "Bthprops.lib")  // Bluetooth APIs

// Custom tray-icon message
#define WM_TRAYICON  (WM_APP + 1)

// Global state
HINSTANCE       ghInst;
UINT            WM_TASKBARCREATED;
NOTIFYICONDATAW nid = {};
static const wchar_t LICENSE_CLASS[] = L"MyTrayAppLicense";
static const wchar_t EXPLWARN_CLASS[] = L"MyTrayAppExplWarn";
static const wchar_t TRAY_CLASS[] = L"MyTrayAppTray";
static bool      g_explOK = false;

// Menu command IDs
enum { ID_TRAY_EXIT = 100, ID_TRAY_RESTART, ID_TRAY_SLEEP };

// Text constants
static const wchar_t* LICENSE_TEXT = LR"(MIT License

Copyright (c) 2025 firstname lastname

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.)";

static const wchar_t* EXPLAINER_TEXT = LR"(All Explorer windows will be closed and the
taskbar and Start menu will be inaccessible for approximately 10 seconds.)";

// Config manager
class Config {
public:
    Config() {
        wchar_t buf[MAX_PATH];
        if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, SHGFP_TYPE_CURRENT, buf))) {
            path_ = buf;
            path_ += L"\\MyTrayApp";
            CreateDirectoryW(path_.c_str(), nullptr);
            path_ += L"\\config.ini";
        }
        showLicense_ = true;
        showExplorerWarn_ = true;
        load();
    }
    bool showLicense() const { return showLicense_; }
    void setShowLicense(bool v) { showLicense_ = v; save(); }
    bool showExplorerWarning() const { return showExplorerWarn_; }
    void setShowExplorerWarning(bool v) { showExplorerWarn_ = v; save(); }

private:
    std::wstring path_;
    bool showLicense_, showExplorerWarn_;
    void load() {
        std::wifstream in(path_);
        if (!in.is_open()) return;
        in >> showLicense_ >> showExplorerWarn_;
    }
    void save() const {
        std::wofstream out(path_, std::ios::trunc);
        out << showLicense_ << L" " << showExplorerWarn_;
    }
} cfg;

// Prototypes
void ShowLicenseWindow();
bool ShowExplorerWarningWindow();
void RestartExplorer();
void DoSleepMode();
LRESULT CALLBACK LicenseProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK ExplWarnProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK TrayProc(HWND, UINT, WPARAM, LPARAM);
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int);

// License dialog
LRESULT CALLBACK LicenseProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_CREATE:
        CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", LICENSE_TEXT,
            WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
            10, 10, 580, 300, hwnd, (HMENU)0, ghInst, nullptr);
        CreateWindowExW(0, L"BUTTON", L"Agree",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 10, 320, 100, 30,
            hwnd, (HMENU)1, ghInst, nullptr);
        CreateWindowExW(0, L"BUTTON", L"Agree && Don't show again",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 120, 320, 220, 30,
            hwnd, (HMENU)2, ghInst, nullptr);
        CreateWindowExW(0, L"BUTTON", L"Decline",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 350, 320, 100, 30,
            hwnd, (HMENU)3, ghInst, nullptr);
        return 0;
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case 1: DestroyWindow(hwnd); break;
        case 2: cfg.setShowLicense(false); DestroyWindow(hwnd); break;
        default: ExitProcess(0);
        }
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

void ShowLicenseWindow() {
    WNDCLASSEXW wc{ sizeof(wc) };
    wc.lpfnWndProc = LicenseProc;
    wc.hInstance = ghInst;
    wc.lpszClassName = LICENSE_CLASS;
    RegisterClassExW(&wc);
    HWND h = CreateWindowExW(0, LICENSE_CLASS, L"License Agreement",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT, CW_USEDEFAULT, 600, 400,
        nullptr, nullptr, ghInst, nullptr);
    if (h) {
        ShowWindow(h, SW_SHOW);
        MSG msg;
        while (GetMessageW(&msg, nullptr, 0, 0)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
}

// Explorer warning
LRESULT CALLBACK ExplWarnProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_CREATE:
        CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", EXPLAINER_TEXT,
            WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
            10, 10, 580, 200, hwnd, (HMENU)0, ghInst, nullptr);
        CreateWindowExW(0, L"BUTTON", L"Agree",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 10, 220, 100, 30,
            hwnd, (HMENU)1, ghInst, nullptr);
        CreateWindowExW(0, L"BUTTON", L"Agree && Don't show again",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 120, 220, 250, 30,
            hwnd, (HMENU)2, ghInst, nullptr);
        CreateWindowExW(0, L"BUTTON", L"Decline",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 380, 220, 100, 30,
            hwnd, (HMENU)3, ghInst, nullptr);
        return 0;
    case WM_COMMAND: {
        int id = LOWORD(wParam);
        g_explOK = (id == 1 || id == 2);
        if (id == 2) cfg.setShowExplorerWarning(false);
        DestroyWindow(hwnd);
        return 0;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

bool ShowExplorerWarningWindow() {
    g_explOK = false;
    WNDCLASSEXW wc{ sizeof(wc) };
    wc.lpfnWndProc = ExplWarnProc;
    wc.hInstance = ghInst;
    wc.lpszClassName = EXPLWARN_CLASS;
    RegisterClassExW(&wc);
    HWND h = CreateWindowExW(0, EXPLWARN_CLASS, L"Warning",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT, CW_USEDEFAULT, 600, 300,
        nullptr, nullptr, ghInst, nullptr);
    if (!h) return false;
    ShowWindow(h, SW_SHOW);
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return g_explOK;
}

// Improved Sleep mode with better Bluetooth control
#pragma warning(push)
#pragma warning(disable:28251)
void DoSleepMode() {
    // Mute audio
    (void)CoInitializeEx(NULL, COINIT_MULTITHREADED);
    IMMDeviceEnumerator* pEnum = nullptr;
    if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL,
        CLSCTX_ALL, IID_PPV_ARGS(&pEnum)))) {
        IMMDevice* pDev = nullptr;
        if (SUCCEEDED(pEnum->GetDefaultAudioEndpoint(eRender, eConsole, &pDev))) {
            IAudioEndpointVolume* pVol = nullptr;
            if (SUCCEEDED(pDev->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, NULL, (void**)&pVol))) {
                pVol->SetMute(TRUE, NULL);
                pVol->Release();
            }
            pDev->Release();
        }
        pEnum->Release();
    }
    CoUninitialize();

    // Improved device toggle function with better error handling
    auto ToggleDeviceClass = [](const GUID* classGuid, DWORD newState) -> bool {
        bool success = false;
        HDEVINFO deviceInfoSet = SetupDiGetClassDevsW(
            classGuid,
            nullptr,
            nullptr,
            DIGCF_PRESENT
        );

        if (deviceInfoSet == INVALID_HANDLE_VALUE) {
            return false;
        }

        SP_DEVINFO_DATA deviceInfoData{ sizeof(SP_DEVINFO_DATA) };

        for (DWORD deviceIndex = 0;
            SetupDiEnumDeviceInfo(deviceInfoSet, deviceIndex, &deviceInfoData);
            ++deviceIndex) {

            SP_PROPCHANGE_PARAMS propChangeParams{};
            propChangeParams.ClassInstallHeader.cbSize = sizeof(SP_CLASSINSTALL_HEADER);
            propChangeParams.ClassInstallHeader.InstallFunction = DIF_PROPERTYCHANGE;
            propChangeParams.StateChange = newState;
            propChangeParams.Scope = DICS_FLAG_GLOBAL;
            propChangeParams.HwProfile = 0;

            if (SetupDiSetClassInstallParamsW(
                deviceInfoSet,
                &deviceInfoData,
                (PSP_CLASSINSTALL_HEADER)&propChangeParams,
                sizeof(propChangeParams))) {

                if (SetupDiCallClassInstaller(
                    DIF_PROPERTYCHANGE,
                    deviceInfoSet,
                    &deviceInfoData)) {
                    success = true;
                }
            }
        }

        SetupDiDestroyDeviceInfoList(deviceInfoSet);
        return success;
        };

    // Turn off Bluetooth using multiple methods for better compatibility

    // Method 1: Disable Bluetooth class devices
    ToggleDeviceClass(&GUID_DEVCLASS_BLUETOOTH, DICS_DISABLE);

    // Method 2: Use Windows API to turn off Bluetooth radio
    // This is more effective on Windows 11
    HANDLE hRadio = NULL;
    BLUETOOTH_FIND_RADIO_PARAMS radioParams = { sizeof(BLUETOOTH_FIND_RADIO_PARAMS) };
    HBLUETOOTH_RADIO_FIND hFind = BluetoothFindFirstRadio(&radioParams, &hRadio);

    if (hFind != NULL) {
        do {
            // Turn off the radio
            BLUETOOTH_RADIO_INFO radioInfo = { sizeof(BLUETOOTH_RADIO_INFO) };
            if (BluetoothGetRadioInfo(hRadio, &radioInfo) == ERROR_SUCCESS) {
                // Disable the radio
                BluetoothEnableDiscovery(hRadio, FALSE);
                BluetoothEnableIncomingConnections(hRadio, FALSE);
            }
            CloseHandle(hRadio);
        } while (BluetoothFindNextRadio(hFind, &hRadio));

        BluetoothFindRadioClose(hFind);
    }

    // Method 3: Command line approach as backup
    system("powershell -WindowStyle Hidden -Command \"Get-PnpDevice -Class Bluetooth | Disable-PnpDevice -Confirm:$false\" 2>nul");

    // Also disable mouse devices if desired
    ToggleDeviceClass(&GUID_DEVCLASS_MOUSE, DICS_DISABLE);

    // Get shutdown privilege
    HANDLE hToken;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) {
        TOKEN_PRIVILEGES tokenPrivileges{};
        if (LookupPrivilegeValueW(NULL, SE_SHUTDOWN_NAME, &tokenPrivileges.Privileges[0].Luid)) {
            tokenPrivileges.PrivilegeCount = 1;
            tokenPrivileges.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

            if (AdjustTokenPrivileges(hToken, FALSE, &tokenPrivileges, 0, NULL, NULL)) {
                // Small delay to ensure devices are disabled
                Sleep(1000);

                // Try to put system to sleep
                BOOL sleepResult = SetSuspendState(FALSE, TRUE, FALSE);

                if (!sleepResult) {
                    // If SetSuspendState fails, try alternative method
                    system("rundll32.exe powrprof.dll,SetSuspendState 0,1,0");
                }
            }
        }
        CloseHandle(hToken);
    }

    // Re-enable devices after wake up (this code runs after system wakes)
    // Add a small delay to ensure system is fully awake
    Sleep(2000);

    // Re-enable Bluetooth
    ToggleDeviceClass(&GUID_DEVCLASS_BLUETOOTH, DICS_ENABLE);
    system("powershell -WindowStyle Hidden -Command \"Get-PnpDevice -Class Bluetooth | Enable-PnpDevice -Confirm:$false\" 2>nul");

    // Re-enable mouse
    ToggleDeviceClass(&GUID_DEVCLASS_MOUSE, DICS_ENABLE);
}
#pragma warning(pop)

// Restart Explorer
void RestartExplorer() {
    system("taskkill /F /IM explorer.exe");
    Sleep(2000);
    STARTUPINFOW si{ sizeof(si) }; PROCESS_INFORMATION pi;
    if (CreateProcessW(L"C:\\Windows\\explorer.exe", NULL, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    }
}

// Tray & menu
LRESULT CALLBACK TrayProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (uMsg == WM_TASKBARCREATED) {
        Shell_NotifyIconW(NIM_ADD, &nid);
        return 0;
    }
    switch (uMsg) {
    case WM_CREATE:
        nid.cbSize = sizeof(nid); nid.hWnd = hwnd; nid.uID = 1;
        nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
        nid.uCallbackMessage = WM_TRAYICON;
        nid.hIcon = LoadIconW(NULL, IDI_APPLICATION);
        wcscpy_s(nid.szTip, L"MyTrayApp");
        Shell_NotifyIconW(NIM_ADD, &nid);
        return 0;
    case WM_TRAYICON:
        if (lParam == WM_RBUTTONUP) {
            POINT pt; GetCursorPos(&pt);
            HMENU menu = CreatePopupMenu();
            InsertMenuW(menu, 0, MF_BYPOSITION, ID_TRAY_EXIT, L"Close app");
            InsertMenuW(menu, 1, MF_BYPOSITION, ID_TRAY_RESTART, L"Restart Explorer");
            InsertMenuW(menu, 2, MF_BYPOSITION, ID_TRAY_SLEEP, L"Sleep mode");
            SetForegroundWindow(hwnd);
            TrackPopupMenu(menu, TPM_BOTTOMALIGN | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);
            DestroyMenu(menu);
        }
        return 0;
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case ID_TRAY_EXIT:
            Shell_NotifyIconW(NIM_DELETE, &nid);
            PostQuitMessage(0); break;
        case ID_TRAY_RESTART:
            if (!cfg.showExplorerWarning() || ShowExplorerWarningWindow()) RestartExplorer();
            break;
        case ID_TRAY_SLEEP:
            DoSleepMode();
            break;
        }
        return 0;
    case WM_DESTROY:
        Shell_NotifyIconW(NIM_DELETE, &nid);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

// Entry point
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int) {
    ghInst = hInstance;
    WM_TASKBARCREATED = RegisterWindowMessageW(L"TaskbarCreated");
    HANDLE mtx = CreateMutexW(NULL, FALSE, L"Global\\MyTrayAppMutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS) return 0;
    if (cfg.showLicense()) ShowLicenseWindow();
    WNDCLASSEXW wc{ sizeof(wc) };
    wc.lpfnWndProc = TrayProc;
    wc.hInstance = ghInst;
    wc.lpszClassName = TRAY_CLASS;
    RegisterClassExW(&wc);
    HWND hwnd = CreateWindowExW(0, TRAY_CLASS, L"", 0, 0, 0, 0, 0, NULL, NULL, ghInst, NULL);
    if (!hwnd) return 0;
    ShowWindow(hwnd, SW_HIDE);
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}