#include <algorithm>
#include <windows.h>
#include <windowsx.h>
#include <objidl.h>
#include <propidl.h>
#include <tlhelp32.h>
#include <dwmapi.h>
#include <shellapi.h>

namespace Gdiplus {
using std::max;
using std::min;
}
#include <gdiplus.h>

#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <cwchar>
#include <iterator>
#include <memory>
#include <string>
#include <thread>

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "advapi32.lib")

namespace {

constexpr wchar_t kWindowClass[] = L"CodexQuotaOrbWindow";
constexpr wchar_t kRegistryPath[] = L"Software\\CodexQuotaOrb";
constexpr int kLogicalSize = 88;
constexpr int kEdgeMargin = 10;
constexpr UINT_PTR kTimerClock = 1;
constexpr UINT_PTR kTimerWatch = 2;
constexpr UINT kMsgQuotaReady = WM_APP + 41;
constexpr UINT kMenuTopmost = 1001;
constexpr UINT kMenuClose = 1002;
constexpr int kRefreshSeconds = 60;

struct QuotaData {
    bool valid = false;
    int usedPercent = 0;
    int remainingPercent = 0;
    int resetCards = -1;
    std::int64_t resetsAt = 0;
    std::wstring error;
};

struct Theme {
    bool light = false;
    Gdiplus::Color accent;
    Gdiplus::Color background;
    Gdiplus::Color border;
    Gdiplus::Color text;
    Gdiplus::Color muted;
    Gdiplus::Color track;
    Gdiplus::Color pill;
};

HWND g_window = nullptr;
ULONG_PTR g_gdiplusToken = 0;
std::atomic<bool> g_fetchInProgress{false};
QuotaData g_quota;
Theme g_theme;
bool g_topmost = true;
bool g_watchMode = false;
bool g_dragging = false;
POINT g_dragOrigin{};
RECT g_dragWindow{};
int g_dpi = 96;
int g_secondsSinceFetch = kRefreshSeconds;
int g_missingCodexChecks = 0;

int Scale(int value) {
    return MulDiv(value, g_dpi, 96);
}

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

Gdiplus::Color Mix(const Gdiplus::Color& first, const Gdiplus::Color& second, float amount) {
    const auto channel = [amount](BYTE a, BYTE b) {
        return static_cast<BYTE>(std::clamp(a * (1.0f - amount) + b * amount, 0.0f, 255.0f));
    };
    return Gdiplus::Color(
        channel(first.GetA(), second.GetA()),
        channel(first.GetR(), second.GetR()),
        channel(first.GetG(), second.GetG()),
        channel(first.GetB(), second.GetB()));
}

Theme ReadTheme() {
    DWORD lightValue = 0;
    DWORD size = sizeof(lightValue);
    const bool isLight = RegGetValueW(
        HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &lightValue, &size) == ERROR_SUCCESS
        && lightValue != 0;

    DWORD rawAccent = 0;
    BOOL opaque = FALSE;
    if (FAILED(DwmGetColorizationColor(&rawAccent, &opaque))) {
        rawAccent = 0xFF5B8DEF;
    }
    Gdiplus::Color accent(
        255,
        static_cast<BYTE>((rawAccent >> 16) & 0xFF),
        static_cast<BYTE>((rawAccent >> 8) & 0xFF),
        static_cast<BYTE>(rawAccent & 0xFF));

    const Gdiplus::Color softTarget = isLight
        ? Gdiplus::Color(255, 72, 105, 170)
        : Gdiplus::Color(255, 128, 174, 255);
    accent = Mix(accent, softTarget, 0.38f);

    Theme theme{};
    theme.light = isLight;
    theme.accent = accent;
    if (isLight) {
        theme.background = Gdiplus::Color(222, 247, 248, 251);
        theme.border = Gdiplus::Color(92, 255, 255, 255);
        theme.text = Gdiplus::Color(245, 31, 35, 43);
        theme.muted = Gdiplus::Color(190, 89, 96, 109);
        theme.track = Gdiplus::Color(36, 70, 80, 100);
        theme.pill = Gdiplus::Color(36, 75, 90, 120);
    } else {
        theme.background = Gdiplus::Color(218, 31, 34, 41);
        theme.border = Gdiplus::Color(52, 255, 255, 255);
        theme.text = Gdiplus::Color(246, 245, 247, 251);
        theme.muted = Gdiplus::Color(190, 181, 187, 200);
        theme.track = Gdiplus::Color(45, 220, 226, 239);
        theme.pill = Gdiplus::Color(35, 238, 242, 250);
    }
    return theme;
}

std::unique_ptr<Gdiplus::FontFamily> UiFontFamily() {
    auto family = std::make_unique<Gdiplus::FontFamily>(L"Microsoft YaHei UI");
    if (family->GetLastStatus() != Gdiplus::Ok) {
        family = std::make_unique<Gdiplus::FontFamily>(L"Segoe UI");
    }
    return family;
}

void DrawCentered(Gdiplus::Graphics& graphics, const std::wstring& text,
                  float y, float height, float fontSize, int style,
                  const Gdiplus::Color& color, float left = 0.0f, float width = -1.0f) {
    auto family = UiFontFamily();
    Gdiplus::Font font(family.get(), fontSize, style, Gdiplus::UnitPixel);
    Gdiplus::SolidBrush brush(color);
    Gdiplus::StringFormat format;
    format.SetAlignment(Gdiplus::StringAlignmentCenter);
    format.SetLineAlignment(Gdiplus::StringAlignmentCenter);
    format.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
    const float fullWidth = static_cast<float>(Scale(kLogicalSize));
    if (width < 0.0f) width = fullWidth;
    graphics.DrawString(text.c_str(), -1, &font,
                        Gdiplus::RectF(left, y, width, height), &format, &brush);
}

std::wstring FormatCountdown(std::int64_t resetsAt) {
    const auto now = static_cast<std::int64_t>(std::time(nullptr));
    std::int64_t seconds = std::max<std::int64_t>(0, resetsAt - now);
    const std::int64_t days = seconds / 86400;
    seconds %= 86400;
    const std::int64_t hours = seconds / 3600;
    seconds %= 3600;
    const std::int64_t minutes = seconds / 60;
    wchar_t buffer[48]{};
    swprintf_s(buffer, L"%lld天%lld时%lld分", days, hours, minutes);
    return buffer;
}

void RenderOrb(HWND window) {
    if (!window || !IsWindow(window)) return;

    const int size = Scale(kLogicalSize);
    HDC screen = GetDC(nullptr);
    HDC memory = CreateCompatibleDC(screen);

    BITMAPINFO bitmapInfo{};
    bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmapInfo.bmiHeader.biWidth = size;
    bitmapInfo.bmiHeader.biHeight = -size;
    bitmapInfo.bmiHeader.biPlanes = 1;
    bitmapInfo.bmiHeader.biBitCount = 32;
    bitmapInfo.bmiHeader.biCompression = BI_RGB;

    void* pixels = nullptr;
    HBITMAP dib = CreateDIBSection(screen, &bitmapInfo, DIB_RGB_COLORS, &pixels, nullptr, 0);
    HGDIOBJ oldBitmap = SelectObject(memory, dib);

    Gdiplus::Bitmap bitmap(size, size, PixelFormat32bppPARGB);
    Gdiplus::Graphics graphics(&bitmap);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
    graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
    graphics.Clear(Gdiplus::Color(0, 0, 0, 0));

    const float s = static_cast<float>(size);
    const float shadowInset = static_cast<float>(Scale(2));
    for (int i = 0; i < 4; ++i) {
        const float inset = shadowInset + static_cast<float>(Scale(i));
        Gdiplus::SolidBrush shadow(Gdiplus::Color(static_cast<BYTE>(9 - i), 0, 0, 0));
        graphics.FillEllipse(&shadow, inset, inset + Scale(2), s - 2 * inset, s - 2 * inset);
    }

    const float panelInset = static_cast<float>(Scale(5));
    Gdiplus::SolidBrush panel(g_theme.background);
    graphics.FillEllipse(&panel, panelInset, panelInset,
                         s - 2 * panelInset, s - 2 * panelInset);
    Gdiplus::Pen outline(g_theme.border, static_cast<float>(Scale(1)));
    graphics.DrawEllipse(&outline, panelInset + 0.5f, panelInset + 0.5f,
                         s - 2 * panelInset - 1.0f, s - 2 * panelInset - 1.0f);

    const float ringInset = static_cast<float>(Scale(10));
    const float ringWidth = static_cast<float>(Scale(5));
    Gdiplus::RectF ringRect(ringInset, ringInset, s - 2 * ringInset, s - 2 * ringInset);
    Gdiplus::Pen track(g_theme.track, ringWidth);
    track.SetStartCap(Gdiplus::LineCapRound);
    track.SetEndCap(Gdiplus::LineCapRound);
    graphics.DrawArc(&track, ringRect, -89.9f, 359.8f);

    if (g_quota.valid) {
        Gdiplus::Pen progress(g_theme.accent, ringWidth);
        progress.SetStartCap(Gdiplus::LineCapRound);
        progress.SetEndCap(Gdiplus::LineCapRound);
        const float sweep = std::clamp(g_quota.remainingPercent * 3.6f, 0.1f, 359.8f);
        graphics.DrawArc(&progress, ringRect, -90.0f, sweep);
    }

    const float factor = static_cast<float>(g_dpi) / 96.0f;
    const std::wstring percentage = g_quota.valid
        ? std::to_wstring(g_quota.remainingPercent) + L"%"
        : L"—";
    DrawCentered(graphics, percentage, static_cast<float>(Scale(19)), static_cast<float>(Scale(23)),
                 15.0f * factor, Gdiplus::FontStyleBold, g_theme.text);

    const std::wstring countdown = g_quota.valid
        ? FormatCountdown(g_quota.resetsAt)
        : (g_quota.error.empty() ? L"正在读取" : L"等待 Codex");
    // Keep the two detail rows in separate bands so larger clock-sized text
    // remains legible without touching at high DPI.
    DrawCentered(graphics, countdown, static_cast<float>(Scale(41)), static_cast<float>(Scale(15)),
                 9.0f * factor, Gdiplus::FontStyleRegular, g_theme.muted);

    const std::wstring cardText = L"重置卡 " +
        (g_quota.resetCards >= 0 ? std::to_wstring(g_quota.resetCards) : L"—");
    DrawCentered(graphics, cardText, static_cast<float>(Scale(57)), static_cast<float>(Scale(15)),
                 9.3f * factor, Gdiplus::FontStyleRegular, g_theme.text);

    graphics.Flush(Gdiplus::FlushIntentionSync);

    Gdiplus::BitmapData bitmapData{};
    Gdiplus::Rect lockRect(0, 0, size, size);
    if (bitmap.LockBits(&lockRect, Gdiplus::ImageLockModeRead,
                        PixelFormat32bppPARGB, &bitmapData) == Gdiplus::Ok) {
        const auto* sourceBase = static_cast<const BYTE*>(bitmapData.Scan0);
        auto* destinationBase = static_cast<BYTE*>(pixels);
        for (int y = 0; y < size; ++y) {
            const BYTE* sourceRow = bitmapData.Stride >= 0
                ? sourceBase + y * bitmapData.Stride
                : sourceBase + (size - 1 - y) * (-bitmapData.Stride);
            std::memcpy(destinationBase + y * size * 4, sourceRow,
                        static_cast<std::size_t>(size) * 4);
        }
        bitmap.UnlockBits(&bitmapData);
    }

    POINT source{0, 0};
    SIZE dimensions{size, size};
    RECT windowRect{};
    GetWindowRect(window, &windowRect);
    POINT destination{windowRect.left, windowRect.top};
    BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    UpdateLayeredWindow(window, screen, &destination, &dimensions, memory,
                        &source, 0, &blend, ULW_ALPHA);

    SelectObject(memory, oldBitmap);
    DeleteObject(dib);
    DeleteDC(memory);
    ReleaseDC(nullptr, screen);
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
        std::int64_t reset = 0;
    };
    const auto parseWindow = [&bucket](const char* name) {
        WindowValue window{};
        std::string object;
        if (!ExtractJsonObject(bucket, name, object)) return window;
        window.valid = ExtractJsonNumber(object, "usedPercent", window.used)
            && ExtractJsonInteger(object, "windowDurationMins", window.duration)
            && ExtractJsonInteger(object, "resetsAt", window.reset);
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

    std::string resetCredits;
    double availableCards = -1.0;
    if (ExtractJsonObject(result, "rateLimitResetCredits", resetCredits)) {
        ExtractJsonNumber(resetCredits, "availableCount", availableCards);
    }

    output.valid = true;
    output.usedPercent = static_cast<int>(std::lround(std::clamp(weekly->used, 0.0, 100.0)));
    output.remainingPercent = std::clamp(100 - output.usedPercent, 0, 100);
    output.resetsAt = weekly->reset;
    output.resetCards = availableCards >= 0.0
        ? static_cast<int>(std::lround(availableCards))
        : -1;
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
        "{\"id\":1,\"method\":\"initialize\",\"params\":{\"clientInfo\":{\"name\":\"codex-quota-orb\",\"title\":\"Codex Quota Orb\",\"version\":\"0.1.0\"},\"capabilities\":{\"experimentalApi\":true}}}\n";
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

void BeginQuotaFetch() {
    if (g_fetchInProgress.exchange(true)) return;
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

RECT MonitorWorkArea(HWND window) {
    MONITORINFO info{sizeof(MONITORINFO)};
    HMONITOR monitor = MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
    GetMonitorInfoW(monitor, &info);
    return info.rcWork;
}

void SaveWindowPosition(HWND window) {
    RECT rect{};
    GetWindowRect(window, &rect);
    WriteRegistryDword(L"HasPosition", 1);
    WriteRegistryDword(L"X", static_cast<DWORD>(rect.left));
    WriteRegistryDword(L"Y", static_cast<DWORD>(rect.top));
}

void SnapToEdge(HWND window) {
    RECT rect{};
    GetWindowRect(window, &rect);
    const RECT work = MonitorWorkArea(window);
    const int width = rect.right - rect.left;
    const int height = rect.bottom - rect.top;
    const int margin = Scale(kEdgeMargin);
    const int leftDistance = std::abs(rect.left - (work.left + margin));
    const int rightDistance = std::abs(rect.left - (work.right - width - margin));
    const int topDistance = std::abs(rect.top - (work.top + margin));
    const int bottomDistance = std::abs(rect.top - (work.bottom - height - margin));
    const int nearest = std::min({leftDistance, rightDistance, topDistance, bottomDistance});
    int x = std::clamp(rect.left, work.left + margin, work.right - width - margin);
    int y = std::clamp(rect.top, work.top + margin, work.bottom - height - margin);
    if (nearest == leftDistance) x = work.left + margin;
    else if (nearest == rightDistance) x = work.right - width - margin;
    else if (nearest == topDistance) y = work.top + margin;
    else y = work.bottom - height - margin;
    SetWindowPos(window, nullptr, x, y, 0, 0,
                 SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOZORDER);
    SaveWindowPosition(window);
}

void RestoreOrDefaultPosition(HWND window) {
    MONITORINFO primaryInfo{sizeof(MONITORINFO)};
    const POINT primaryOrigin{0, 0};
    GetMonitorInfoW(MonitorFromPoint(primaryOrigin, MONITOR_DEFAULTTOPRIMARY), &primaryInfo);
    RECT work = primaryInfo.rcWork;
    const int size = Scale(kLogicalSize);
    DWORD hasPosition = 0;
    DWORD storedX = 0;
    DWORD storedY = 0;
    int x = work.right - size - Scale(kEdgeMargin);
    int y = work.top + ((work.bottom - work.top) - size) / 2;
    if (ReadRegistryDword(L"HasPosition", hasPosition) && hasPosition
        && ReadRegistryDword(L"X", storedX) && ReadRegistryDword(L"Y", storedY)) {
        x = static_cast<int>(storedX);
        y = static_cast<int>(storedY);
    }
    x = std::clamp(x, static_cast<int>(work.left), static_cast<int>(work.right) - size);
    y = std::clamp(y, static_cast<int>(work.top), static_cast<int>(work.bottom) - size);
    SetWindowPos(window, g_topmost ? HWND_TOPMOST : HWND_NOTOPMOST,
                 x, y, size, size, SWP_NOACTIVATE);
    if (!hasPosition) SaveWindowPosition(window);
}

void ShowContextMenu(HWND window, POINT screenPoint) {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING | (g_topmost ? MF_CHECKED : 0),
                kMenuTopmost, L"置顶");
    AppendMenuW(menu, MF_STRING, kMenuClose, L"关闭");
    SetForegroundWindow(window);
    const UINT command = TrackPopupMenu(menu,
        TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY,
        screenPoint.x, screenPoint.y, 0, window, nullptr);
    DestroyMenu(menu);
    if (command == kMenuTopmost) {
        g_topmost = !g_topmost;
        WriteRegistryDword(L"TopMost", g_topmost ? 1 : 0);
        SetWindowPos(window, g_topmost ? HWND_TOPMOST : HWND_NOTOPMOST,
                     0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    } else if (command == kMenuClose) {
        DestroyWindow(window);
    }
}

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE:
        SetTimer(window, kTimerClock, 1000, nullptr);
        SetTimer(window, kTimerWatch, 3000, nullptr);
        return 0;
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_NCHITTEST: {
        POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        ScreenToClient(window, &point);
        const int radius = Scale(kLogicalSize) / 2;
        const int dx = point.x - radius;
        const int dy = point.y - radius;
        if (dx * dx + dy * dy > radius * radius) return HTTRANSPARENT;
        return HTCLIENT;
    }
    case WM_LBUTTONDOWN:
        g_dragging = true;
        SetCapture(window);
        GetCursorPos(&g_dragOrigin);
        GetWindowRect(window, &g_dragWindow);
        return 0;
    case WM_MOUSEMOVE:
        if (g_dragging && (wParam & MK_LBUTTON)) {
            POINT cursor{};
            GetCursorPos(&cursor);
            SetWindowPos(window, nullptr,
                g_dragWindow.left + cursor.x - g_dragOrigin.x,
                g_dragWindow.top + cursor.y - g_dragOrigin.y,
                0, 0, SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOZORDER);
        }
        return 0;
    case WM_LBUTTONUP:
        if (g_dragging) {
            g_dragging = false;
            ReleaseCapture();
            SnapToEdge(window);
        }
        return 0;
    case WM_CAPTURECHANGED:
        g_dragging = false;
        return 0;
    case WM_RBUTTONUP: {
        POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        ClientToScreen(window, &point);
        ShowContextMenu(window, point);
        return 0;
    }
    case WM_TIMER:
        if (wParam == kTimerClock) {
            ++g_secondsSinceFetch;
            if (IsWindowVisible(window)) {
                RenderOrb(window);
                if (g_secondsSinceFetch >= kRefreshSeconds) {
                    g_secondsSinceFetch = 0;
                    BeginQuotaFetch();
                }
            }
        } else if (wParam == kTimerWatch && g_watchMode) {
            if (IsCodexRunning()) {
                g_missingCodexChecks = 0;
                if (!IsWindowVisible(window)) {
                    ShowWindow(window, SW_SHOWNOACTIVATE);
                    RenderOrb(window);
                    g_secondsSinceFetch = 0;
                    BeginQuotaFetch();
                }
            } else if (++g_missingCodexChecks >= 3 && IsWindowVisible(window)) {
                ShowWindow(window, SW_HIDE);
            }
        }
        return 0;
    case WM_SETTINGCHANGE:
    case WM_THEMECHANGED:
    case WM_DWMCOLORIZATIONCOLORCHANGED:
        g_theme = ReadTheme();
        RenderOrb(window);
        return 0;
    case WM_DPICHANGED: {
        g_dpi = HIWORD(wParam);
        const RECT* suggested = reinterpret_cast<const RECT*>(lParam);
        const int size = Scale(kLogicalSize);
        SetWindowPos(window, nullptr, suggested->left, suggested->top,
                     size, size, SWP_NOACTIVATE | SWP_NOZORDER);
        RenderOrb(window);
        return 0;
    }
    case kMsgQuotaReady: {
        std::unique_ptr<QuotaData> incoming(reinterpret_cast<QuotaData*>(lParam));
        if (incoming->valid) {
            g_quota = *incoming;
        } else if (!g_quota.valid) {
            g_quota = *incoming;
        } else {
            g_quota.error = incoming->error;
        }
        RenderOrb(window);
        return 0;
    }
    case WM_DESTROY:
        KillTimer(window, kTimerClock);
        KillTimer(window, kTimerWatch);
        g_window = nullptr;
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(window, message, wParam, lParam);
    }
}

bool IsAlreadyRunning() {
    HANDLE mutex = CreateMutexW(nullptr, FALSE, L"Local\\CodexQuotaOrb.SingleInstance");
    return mutex && GetLastError() == ERROR_ALREADY_EXISTS;
}

} // namespace

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, wchar_t* commandLine, int) {
    if (IsAlreadyRunning()) return 0;

    const auto setDpiAwareness = reinterpret_cast<BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT)>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetProcessDpiAwarenessContext"));
    if (setDpiAwareness) setDpiAwareness(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    Gdiplus::GdiplusStartupInput gdiplusInput;
    if (Gdiplus::GdiplusStartup(&g_gdiplusToken, &gdiplusInput, nullptr) != Gdiplus::Ok) return 1;

    g_watchMode = commandLine && wcsstr(commandLine, L"--watch") != nullptr;
    DWORD topmostValue = 1;
    ReadRegistryDword(L"TopMost", topmostValue);
    g_topmost = topmostValue != 0;
    g_theme = ReadTheme();

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.hInstance = instance;
    windowClass.lpfnWndProc = WindowProc;
    windowClass.lpszClassName = kWindowClass;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_HAND);
    RegisterClassExW(&windowClass);

    const int initialSize = kLogicalSize;
    g_window = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOOLWINDOW,
        kWindowClass, L"Codex Quota Orb", WS_POPUP,
        0, 0, initialSize, initialSize,
        nullptr, nullptr, instance, nullptr);
    if (!g_window) {
        Gdiplus::GdiplusShutdown(g_gdiplusToken);
        return 1;
    }

    g_dpi = static_cast<int>(GetDpiForWindow(g_window));
    RestoreOrDefaultPosition(g_window);
    const bool shouldShow = !g_watchMode || IsCodexRunning();
    if (shouldShow) {
        ShowWindow(g_window, SW_SHOWNOACTIVATE);
        RenderOrb(g_window);
        g_secondsSinceFetch = 0;
        BeginQuotaFetch();
    }

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    Gdiplus::GdiplusShutdown(g_gdiplusToken);
    return static_cast<int>(message.wParam);
}
