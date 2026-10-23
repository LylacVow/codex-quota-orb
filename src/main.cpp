#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <iterator>
#include <memory>
#include <windows.h>
#include <shellapi.h>
#include <string>
#include <thread>
#include <tlhelp32.h>

#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "shell32.lib")

namespace {

constexpr wchar_t kWindowClass[] = L"CodexQuotaTrayWindow";
constexpr wchar_t kRegistryPath[] = L"Software\\CodexQuotaOrb";
constexpr UINT_PTR kTimerWatch = 1;
constexpr UINT kMsgTray = WM_APP + 40;
constexpr UINT kMsgQuotaReady = WM_APP + 41;
constexpr UINT kMenuClose = 1001;
constexpr int kWatchSeconds = 10;
constexpr int kRefreshSeconds = 60;
constexpr UINT kTrayId = 1;

// Stable identity lets Windows remember where the user placed the tray icon.
constexpr GUID kTrayGuid = {
    0x7a799daa, 0x562a, 0x4df7, {0x91, 0x99, 0x1f, 0x36, 0xb8, 0x4d, 0x70, 0x29}
};

struct QuotaData {
    bool valid = false;
    int remainingPercent = -1;
    std::wstring error;
};

HWND g_window = nullptr;
HANDLE g_mutex = nullptr;
HICON g_icon = nullptr;
UINT g_taskbarCreated = 0;
std::atomic<bool> g_fetchInProgress{false};
bool g_codexRunning = false;
int g_secondsSinceFetch = kRefreshSeconds;
int g_remainingPercent = -1;

bool FileExists(const std::wstring& path) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
}

std::wstring Quote(const std::wstring& value) {
    return L"\"" + value + L"\"";
}

bool ReadRegistryDword(const wchar_t* name, DWORD& value) {
    DWORD size = sizeof(value);
    return RegGetValueW(HKEY_CURRENT_USER, kRegistryPath, name, RRF_RT_REG_DWORD,
                        nullptr, &value, &size) == ERROR_SUCCESS;
}

void WriteRegistryDword(const wchar_t* name, DWORD value) {
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRegistryPath, 0, nullptr, 0, KEY_SET_VALUE,
                        nullptr, &key, nullptr) == ERROR_SUCCESS) {
        RegSetValueExW(key, name, 0, REG_DWORD,
                       reinterpret_cast<const BYTE*>(&value), sizeof(value));
        RegCloseKey(key);
    }
}

bool IsSystemLightTheme() {
    DWORD light = 0;
    DWORD size = sizeof(light);
    return RegGetValueW(
        HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        L"SystemUsesLightTheme", RRF_RT_REG_DWORD, nullptr, &light, &size) == ERROR_SUCCESS
        && light != 0;
}

HICON CreatePercentIcon(int remainingPercent) {
    constexpr int size = 64;
    BITMAPV5HEADER header{};
    header.bV5Size = sizeof(header);
    header.bV5Width = size;
    header.bV5Height = -size;
    header.bV5Planes = 1;
    header.bV5BitCount = 32;
    header.bV5Compression = BI_BITFIELDS;
    header.bV5RedMask = 0x00FF0000;
    header.bV5GreenMask = 0x0000FF00;
    header.bV5BlueMask = 0x000000FF;
    header.bV5AlphaMask = 0xFF000000;

    HDC screen = GetDC(nullptr);
    void* rawPixels = nullptr;
    HBITMAP color = CreateDIBSection(
        screen, reinterpret_cast<BITMAPINFO*>(&header), DIB_RGB_COLORS,
        &rawPixels, nullptr, 0);
    ReleaseDC(nullptr, screen);
    if (!color || !rawPixels) {
        if (color) DeleteObject(color);
        return CopyIcon(LoadIconW(nullptr, IDI_APPLICATION));
    }

    std::memset(rawPixels, 0, size * size * 4);
    HDC memory = CreateCompatibleDC(nullptr);
    HGDIOBJ oldBitmap = SelectObject(memory, color);
    SetBkMode(memory, TRANSPARENT);
    SetTextColor(memory, RGB(255, 255, 255));

    const std::wstring text = remainingPercent >= 0
        ? std::to_wstring(remainingPercent) + L"%"
        : L"--";
    const int fontHeight = text.size() >= 4 ? 22 : 27;
    HFONT font = CreateFontW(
        -fontHeight, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Semibold");
    HGDIOBJ oldFont = SelectObject(memory, font);
    RECT bounds{0, 1, size, size - 1};
    DrawTextW(memory, text.c_str(), -1, &bounds,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(memory, oldFont);
    DeleteObject(font);
    SelectObject(memory, oldBitmap);
    DeleteDC(memory);

    const BYTE target = IsSystemLightTheme() ? 28 : 248;
    auto* pixels = static_cast<BYTE*>(rawPixels);
    for (int i = 0; i < size * size; ++i) {
        BYTE* pixel = pixels + i * 4;
        const BYTE coverage = std::max({pixel[0], pixel[1], pixel[2]});
        pixel[0] = target;
        pixel[1] = target;
        pixel[2] = target;
        pixel[3] = coverage;
    }

    BYTE maskBits[size * size / 8]{};
    HBITMAP mask = CreateBitmap(size, size, 1, 1, maskBits);
    ICONINFO iconInfo{};
    iconInfo.fIcon = TRUE;
    iconInfo.hbmMask = mask;
    iconInfo.hbmColor = color;
    HICON icon = CreateIconIndirect(&iconInfo);
    DeleteObject(mask);
    DeleteObject(color);
    return icon ? icon : CopyIcon(LoadIconW(nullptr, IDI_APPLICATION));
}

void FillTrayData(NOTIFYICONDATAW& data) {
    data = {};
    data.cbSize = sizeof(data);
    data.hWnd = g_window;
    data.uID = kTrayId;
    data.guidItem = kTrayGuid;
    data.uFlags = NIF_GUID | NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP | NIF_STATE;
    data.dwStateMask = NIS_HIDDEN;
    data.dwState = 0;
    data.uCallbackMessage = kMsgTray;
    data.hIcon = g_icon;

    std::wstring tip = L"Codex 周额度：";
    tip += g_remainingPercent >= 0
        ? std::to_wstring(g_remainingPercent) + L"%"
        : L"--";
    tip += g_codexRunning ? L"（已连接）" : L"（等待 Codex）";
    wcsncpy_s(data.szTip, tip.c_str(), _TRUNCATE);
}

void AddTrayIcon() {
    if (g_icon) DestroyIcon(g_icon);
    g_icon = CreatePercentIcon(g_remainingPercent);

    NOTIFYICONDATAW data{};
    FillTrayData(data);
    Shell_NotifyIconW(NIM_ADD, &data);
    data.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &data);
}

void UpdateTrayIcon(bool rebuildIcon) {
    if (rebuildIcon) {
        HICON next = CreatePercentIcon(g_remainingPercent);
        if (g_icon) DestroyIcon(g_icon);
        g_icon = next;
    }
    NOTIFYICONDATAW data{};
    FillTrayData(data);
    Shell_NotifyIconW(NIM_MODIFY, &data);
}

void RemoveTrayIcon() {
    NOTIFYICONDATAW data{};
    data.cbSize = sizeof(data);
    data.hWnd = g_window;
    data.uID = kTrayId;
    data.guidItem = kTrayGuid;
    data.uFlags = NIF_GUID;
    Shell_NotifyIconW(NIM_DELETE, &data);
    if (g_icon) {
        DestroyIcon(g_icon);
        g_icon = nullptr;
    }
}

bool FindJsonValue(const std::string& json, const std::string& key,
                   std::size_t& valuePosition, std::size_t from = 0) {
    const std::string pattern = "\"" + key + "\"";
    std::size_t position = from;
    while ((position = json.find(pattern, position)) != std::string::npos) {
        std::size_t cursor = position + pattern.size();
        while (cursor < json.size() && std::isspace(static_cast<unsigned char>(json[cursor]))) ++cursor;
        if (cursor < json.size() && json[cursor] == ':') {
            ++cursor;
            while (cursor < json.size() && std::isspace(static_cast<unsigned char>(json[cursor]))) ++cursor;
            valuePosition = cursor;
            return true;
        }
        position += pattern.size();
    }
    return false;
}

bool ExtractJsonObject(const std::string& json, const std::string& key, std::string& object) {
    std::size_t start = 0;
    if (!FindJsonValue(json, key, start) || start >= json.size() || json[start] != '{') return false;
    int depth = 0;
    bool inString = false;
    bool escaped = false;
    for (std::size_t i = start; i < json.size(); ++i) {
        const char ch = json[i];
        if (inString) {
            if (escaped) escaped = false;
            else if (ch == '\\') escaped = true;
            else if (ch == '"') inString = false;
            continue;
        }
        if (ch == '"') inString = true;
        else if (ch == '{') ++depth;
        else if (ch == '}' && --depth == 0) {
            object.assign(json, start, i - start + 1);
            return true;
        }
    }
    return false;
}

bool ExtractJsonNumber(const std::string& json, const std::string& key, double& number) {
    std::size_t position = 0;
    if (!FindJsonValue(json, key, position) || position >= json.size()) return false;
    char* end = nullptr;
    number = std::strtod(json.c_str() + position, &end);
    return end != json.c_str() + position;
}

bool ExtractJsonInteger(const std::string& json, const std::string& key, std::int64_t& number) {
    double value = 0.0;
    if (!ExtractJsonNumber(json, key, value)) return false;
    number = static_cast<std::int64_t>(value);
    return true;
}

bool ParseQuotaResponse(const std::string& response, QuotaData& output) {
    std::string result;
    if (!ExtractJsonObject(response, "result", result)) {
        output.error = L"Codex 返回缺少 result";
        return false;
    }

    std::string bucket;
    std::string buckets;
    if (ExtractJsonObject(result, "rateLimitsByLimitId", buckets)) {
        ExtractJsonObject(buckets, "codex", bucket);
    }
    if (bucket.empty() && !ExtractJsonObject(result, "rateLimits", bucket)) {
        output.error = L"未找到 Codex 额度桶";
        return false;
    }

    struct WindowValue {
        bool valid = false;
        double used = 0.0;
        std::int64_t duration = 0;
    };
    const auto parseWindow = [&bucket](const char* name) {
        WindowValue window{};
        std::string object;
        if (!ExtractJsonObject(bucket, name, object)) return window;
        std::int64_t ignoredReset = 0;
        window.valid = ExtractJsonNumber(object, "usedPercent", window.used)
            && ExtractJsonInteger(object, "windowDurationMins", window.duration)
            && ExtractJsonInteger(object, "resetsAt", ignoredReset);
        return window;
    };

    const WindowValue primary = parseWindow("primary");
    const WindowValue secondary = parseWindow("secondary");
    const WindowValue* weekly = nullptr;
    if (primary.valid && primary.duration == 10080) weekly = &primary;
    if (secondary.valid && secondary.duration == 10080) weekly = &secondary;
    if (!weekly) {
        output.error = L"当前账户未返回周额度窗口";
        return false;
    }

    const int used = static_cast<int>(std::lround(std::clamp(weekly->used, 0.0, 100.0)));
    output.valid = true;
    output.remainingPercent = std::clamp(100 - used, 0, 100);
    return true;
}

std::wstring FindCodexExecutable() {
    wchar_t overridePath[MAX_PATH * 4]{};
    const DWORD overrideLength = GetEnvironmentVariableW(
        L"CODEX_ORB_CODEX_PATH", overridePath, static_cast<DWORD>(std::size(overridePath)));
    if (overrideLength > 0 && overrideLength < std::size(overridePath) && FileExists(overridePath)) {
        return overridePath;
    }

    wchar_t searchResult[MAX_PATH * 4]{};
    if (SearchPathW(nullptr, L"codex.exe", nullptr,
                    static_cast<DWORD>(std::size(searchResult)), searchResult, nullptr) > 0
        && FileExists(searchResult)) {
        return searchResult;
    }

    wchar_t localAppData[MAX_PATH * 4]{};
    const DWORD length = GetEnvironmentVariableW(
        L"LOCALAPPDATA", localAppData, static_cast<DWORD>(std::size(localAppData)));
    if (length == 0 || length >= std::size(localAppData)) return {};

    const std::wstring localRoot(localAppData);
    const std::wstring known = localRoot + L"\\Programs\\OpenAI\\Codex\\bin\\codex.exe";
    if (FileExists(known)) return known;

    const std::wstring binRoot = localRoot + L"\\OpenAI\\Codex\\bin";
    WIN32_FIND_DATAW findData{};
    HANDLE search = FindFirstFileW((binRoot + L"\\*").c_str(), &findData);
    if (search == INVALID_HANDLE_VALUE) return {};

    std::wstring newest;
    FILETIME newestTime{};
    do {
        if (!(findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            || wcscmp(findData.cFileName, L".") == 0
            || wcscmp(findData.cFileName, L"..") == 0) continue;
        const std::wstring candidate = binRoot + L"\\" + findData.cFileName + L"\\codex.exe";
        WIN32_FILE_ATTRIBUTE_DATA attributes{};
        if (GetFileAttributesExW(candidate.c_str(), GetFileExInfoStandard, &attributes)
            && (newest.empty() || CompareFileTime(&attributes.ftLastWriteTime, &newestTime) > 0)) {
            newest = candidate;
            newestTime = attributes.ftLastWriteTime;
        }
    } while (FindNextFileW(search, &findData));
    FindClose(search);
    return newest;
}

bool WriteAll(HANDLE handle, const std::string& text) {
    DWORD total = 0;
    while (total < text.size()) {
        DWORD written = 0;
        if (!WriteFile(handle, text.data() + total,
                       static_cast<DWORD>(text.size() - total), &written, nullptr)
            || written == 0) return false;
        total += written;
    }
    return true;
}

bool FetchQuotaFromCodex(QuotaData& output) {
    const std::wstring codex = FindCodexExecutable();
    if (codex.empty()) {
        output.error = L"未找到 codex.exe";
        return false;
    }

    SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE childOutRead = nullptr;
    HANDLE childOutWrite = nullptr;
    HANDLE childInRead = nullptr;
    HANDLE childInWrite = nullptr;
    if (!CreatePipe(&childOutRead, &childOutWrite, &security, 0)
        || !SetHandleInformation(childOutRead, HANDLE_FLAG_INHERIT, 0)
        || !CreatePipe(&childInRead, &childInWrite, &security, 0)
        || !SetHandleInformation(childInWrite, HANDLE_FLAG_INHERIT, 0)) {
        output.error = L"无法创建本地通信管道";
        if (childOutRead) CloseHandle(childOutRead);
        if (childOutWrite) CloseHandle(childOutWrite);
        if (childInRead) CloseHandle(childInRead);
        if (childInWrite) CloseHandle(childInWrite);
        return false;
    }

    HANDLE nullOutput = CreateFileW(L"NUL", GENERIC_WRITE,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE,
                                    &security, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    startup.hStdInput = childInRead;
    startup.hStdOutput = childOutWrite;
    startup.hStdError = nullOutput;

    PROCESS_INFORMATION process{};
    std::wstring command = Quote(codex) + L" -s read-only -a never app-server --listen stdio://";
    const BOOL launched = CreateProcessW(
        codex.c_str(), command.data(), nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT,
        nullptr, nullptr, &startup, &process);

    CloseHandle(childInRead);
    CloseHandle(childOutWrite);
    if (nullOutput != INVALID_HANDLE_VALUE) CloseHandle(nullOutput);
    if (!launched) {
        CloseHandle(childOutRead);
        CloseHandle(childInWrite);
        output.error = L"无法启动 codex app-server";
        return false;
    }

    const std::string initialize =
        "{\"id\":1,\"method\":\"initialize\",\"params\":{\"clientInfo\":{\"name\":\"codex-quota-orb\",\"title\":\"Codex Quota Orb\",\"version\":\"0.2.0\"},\"capabilities\":{\"experimentalApi\":true}}}\n";
    bool success = WriteAll(childInWrite, initialize);
    bool requestSent = false;
    std::string pending;
    std::string response;
    const ULONGLONG deadline = GetTickCount64() + 15000;

    while (success && GetTickCount64() < deadline) {
        DWORD available = 0;
        if (!PeekNamedPipe(childOutRead, nullptr, 0, nullptr, &available, nullptr)) break;
        if (available == 0) {
            if (WaitForSingleObject(process.hProcess, 0) == WAIT_OBJECT_0) break;
            Sleep(20);
            continue;
        }
        char buffer[4096];
        DWORD read = 0;
        if (!ReadFile(childOutRead, buffer, std::min<DWORD>(available, sizeof(buffer)), &read, nullptr)
            || read == 0) break;
        pending.append(buffer, read);
        std::size_t newline = 0;
        while ((newline = pending.find('\n')) != std::string::npos) {
            std::string line = pending.substr(0, newline);
            pending.erase(0, newline + 1);
            double id = 0.0;
            if (!ExtractJsonNumber(line, "id", id)) continue;
            if (static_cast<int>(id) == 1 && !requestSent) {
                requestSent = WriteAll(childInWrite,
                    "{\"id\":2,\"method\":\"account/rateLimits/read\",\"params\":null}\n");
                success = requestSent;
            } else if (static_cast<int>(id) == 2) {
                response = std::move(line);
                success = true;
                break;
            }
        }
        if (!response.empty()) break;
    }

    CloseHandle(childInWrite);
    CloseHandle(childOutRead);
    if (WaitForSingleObject(process.hProcess, 100) == WAIT_TIMEOUT) {
        TerminateProcess(process.hProcess, 0);
    }
    WaitForSingleObject(process.hProcess, 1000);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);

    if (!success || response.empty()) {
        output.error = L"读取 Codex 额度超时";
        return false;
    }
    if (response.find("\"error\"") != std::string::npos) {
        output.error = L"Codex app-server 返回错误";
        return false;
    }
    return ParseQuotaResponse(response, output);
}

bool IsCodexRunning() {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    bool found = false;
    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (_wcsicmp(entry.szExeFile, L"codex-code-mode-host.exe") == 0
                || _wcsicmp(entry.szExeFile, L"codex.exe") == 0) {
                found = true;
                break;
            }
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return found;
}

void BeginQuotaFetch() {
    if (!g_codexRunning || g_fetchInProgress.exchange(true)) return;
    const HWND target = g_window;
    std::thread([target] {
        auto data = std::make_unique<QuotaData>();
        FetchQuotaFromCodex(*data);
        QuotaData* raw = data.release();
        if (!IsWindow(target) || !PostMessageW(target, kMsgQuotaReady, 0,
                                               reinterpret_cast<LPARAM>(raw))) {
            delete raw;
        }
        g_fetchInProgress = false;
    }).detach();
}

void ShowContextMenu(HWND window) {
    POINT point{};
    GetCursorPos(&point);
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, kMenuClose, L"关闭");
    SetForegroundWindow(window);
    const UINT command = TrackPopupMenu(
        menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY,
        point.x, point.y, 0, window, nullptr);
    DestroyMenu(menu);
    if (command == kMenuClose) DestroyWindow(window);
}

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == g_taskbarCreated) {
        AddTrayIcon();
        return 0;
    }
    switch (message) {
    case WM_NCCREATE:
        g_window = window;
        return TRUE;
    case WM_CREATE:
        AddTrayIcon();
        g_codexRunning = IsCodexRunning();
        UpdateTrayIcon(false);
        if (g_codexRunning) {
            g_secondsSinceFetch = 0;
            BeginQuotaFetch();
        }
        SetTimer(window, kTimerWatch, kWatchSeconds * 1000, nullptr);
        return 0;
    case WM_TIMER:
        if (g_codexRunning) g_secondsSinceFetch += kWatchSeconds;
        {
            const bool runningNow = IsCodexRunning();
            if (runningNow != g_codexRunning) {
                g_codexRunning = runningNow;
                UpdateTrayIcon(false);
                if (g_codexRunning) {
                    g_secondsSinceFetch = 0;
                    BeginQuotaFetch();
                } else {
                    g_secondsSinceFetch = kRefreshSeconds;
                }
            } else if (g_codexRunning && g_secondsSinceFetch >= kRefreshSeconds) {
                g_secondsSinceFetch = 0;
                BeginQuotaFetch();
            }
        }
        return 0;
    case kMsgQuotaReady: {
        std::unique_ptr<QuotaData> incoming(reinterpret_cast<QuotaData*>(lParam));
        if (incoming->valid) {
            const bool changed = incoming->remainingPercent != g_remainingPercent;
            g_remainingPercent = incoming->remainingPercent;
            WriteRegistryDword(L"LastRemainingPercent", static_cast<DWORD>(g_remainingPercent));
            UpdateTrayIcon(changed);
        }
        return 0;
    }
    case kMsgTray: {
        const UINT event = LOWORD(lParam);
        if (event == WM_CONTEXTMENU || event == WM_RBUTTONUP) ShowContextMenu(window);
        return 0;
    }
    case WM_SETTINGCHANGE:
    case WM_THEMECHANGED:
        UpdateTrayIcon(true);
        return 0;
    case WM_DESTROY:
        KillTimer(window, kTimerWatch);
        RemoveTrayIcon();
        g_window = nullptr;
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(window, message, wParam, lParam);
    }
}

bool IsAlreadyRunning() {
    g_mutex = CreateMutexW(nullptr, FALSE, L"Local\\CodexQuotaOrb.SingleInstance");
    return g_mutex && GetLastError() == ERROR_ALREADY_EXISTS;
}

} // namespace

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, wchar_t*, int) {
    if (IsAlreadyRunning()) {
        if (g_mutex) CloseHandle(g_mutex);
        return 0;
    }

    DWORD cached = 0;
    if (ReadRegistryDword(L"LastRemainingPercent", cached) && cached <= 100) {
        g_remainingPercent = static_cast<int>(cached);
    }
    g_taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.hInstance = instance;
    windowClass.lpfnWndProc = WindowProc;
    windowClass.lpszClassName = kWindowClass;
    RegisterClassExW(&windowClass);

    g_window = CreateWindowExW(
        WS_EX_TOOLWINDOW, kWindowClass, L"Codex Quota Orb",
        WS_OVERLAPPED, 0, 0, 0, 0,
        nullptr, nullptr, instance, nullptr);
    if (!g_window) {
        if (g_mutex) CloseHandle(g_mutex);
        return 1;
    }

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    if (g_mutex) CloseHandle(g_mutex);
    return static_cast<int>(message.wParam);
}
