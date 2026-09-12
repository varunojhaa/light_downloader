#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <string>
#include <vector>
#include <algorithm>
#include <cstdint>
#include <cstdio>

namespace {
constexpr int IDC_URL = 1001, IDC_OUTPUT = 1002, IDC_BROWSE = 1003;
constexpr int IDC_ADD = 1004, IDC_START = 1005, IDC_PAUSE = 1006;
constexpr int IDC_REMOVE = 1007, IDC_LIST = 1008, IDC_STATUS = 1009;
constexpr int IDC_CONNECTIONS = 1010, IDC_RETRIES = 1011, IDC_LIMIT = 1012;
constexpr int IDC_START_ALL = 1013, IDC_PAUSE_ALL = 1014, IDC_CLEAR = 1015;
constexpr int IDC_OPEN = 1016;

struct Task {
    std::wstring url;
    std::wstring output;
    std::wstring part;
    int connections = 8;
    int retries = 3;
    uint64_t limit = 0;
    HANDLE process = nullptr;
    ULONGLONG started = 0;
    uint64_t lastBytes = 0;
    DWORD lastTick = 0;
    double speed = 0;
    bool paused = false;
};

HWND g_window = nullptr, g_url = nullptr, g_output = nullptr, g_connections = nullptr;
HWND g_retries = nullptr, g_limit = nullptr;
HWND g_list = nullptr, g_status = nullptr;
HFONT g_font = nullptr, g_titleFont = nullptr;
HBRUSH g_background = nullptr, g_panel = nullptr;
std::vector<Task> g_tasks;

std::wstring text(HWND control) {
    int n = GetWindowTextLengthW(control);
    std::wstring value(static_cast<size_t>(n) + 1, L'\0');
    if (n) GetWindowTextW(control, value.data(), n + 1);
    value.resize(static_cast<size_t>(n));
    return value;
}

void set_text(HWND control, const std::wstring& value) { SetWindowTextW(control, value.c_str()); }

std::wstring file_name(const std::wstring& path) {
    const size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

std::wstring format_size(uint64_t value) {
    const wchar_t* units[] = {L"B", L"KB", L"MB", L"GB", L"TB"};
    double amount = static_cast<double>(value);
    int unit = 0;
    while (amount >= 1024.0 && unit < 4) { amount /= 1024.0; ++unit; }
    wchar_t buffer[64];
    if (unit == 0) swprintf_s(buffer, L"%llu B", static_cast<unsigned long long>(value));
    else swprintf_s(buffer, L"%.1f %s", amount, units[unit]);
    return buffer;
}

std::wstring format_speed(double bytesPerSecond) {
    return format_size(static_cast<uint64_t>(bytesPerSecond)) + L"/s";
}

bool file_size(const std::wstring& path, uint64_t& result) {
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) return false;
    ULARGE_INTEGER size{};
    size.HighPart = data.nFileSizeHigh;
    size.LowPart = data.nFileSizeLow;
    result = size.QuadPart;
    return true;
}

bool state_size(const std::wstring& output, uint64_t& result) {
    FILE* file = nullptr;
    const std::wstring state = output + L".ld";
    if (_wfopen_s(&file, state.c_str(), L"r, ccs=UTF-8") != 0 || !file) return false;
    wchar_t magic[16] = {};
    unsigned long long size = 0;
    const int fields = fwscanf_s(file, L"%15ls\n%llu", magic, static_cast<unsigned>(sizeof(magic) / sizeof(magic[0])), &size);
    if (fields != 2) {
        rewind(file);
        wchar_t line[256] = {};
        while (fgetws(line, ARRAYSIZE(line), file)) {
            const wchar_t* marker = wcsstr(line, L"\"size\"");
            if (marker) {
                marker = wcschr(marker, L':');
                if (marker) { size = _wcstoui64(marker + 1, nullptr, 10); break; }
            }
        }
    }
    fclose(file);
    if (fields != 2 && size == 0) return false;
    if (fields == 2 && wcscmp(magic, L"LDC1") != 0) return false;
    result = static_cast<uint64_t>(size);
    return true;
}

bool state_progress(const std::wstring& output, uint64_t& completed, uint64_t& total) {
    FILE* file = nullptr;
    const std::wstring state = output + L".ld";
    if (_wfopen_s(&file, state.c_str(), L"r, ccs=UTF-8") != 0 || !file) return false;
    wchar_t magic[16] = {};
    unsigned long long size = 0;
    int range = 0, connections = 0;
    if (fwscanf_s(file, L"%15ls\n%llu\n%d\n%d\n", magic, ARRAYSIZE(magic), &size, &range, &connections) == 4 &&
        wcscmp(magic, L"LDC1") == 0 && connections > 0) {
        for (int i = 0; i < connections; ++i) {
            unsigned long long first = 0, last = 0, done = 0;
            if (fwscanf_s(file, L"%llu %llu %llu\n", &first, &last, &done) != 3) { fclose(file); return false; }
            completed += done;
        }
        fclose(file);
        total = static_cast<uint64_t>(size);
        return true;
    }
    rewind(file);
    wchar_t line[512] = {};
    std::wstring json;
    while (fgetws(line, ARRAYSIZE(line), file)) json += line;
    fclose(file);
    const size_t sizeMarker = json.find(L"\"size\"");
    const size_t segmentsMarker = json.find(L"\"segments\"");
    if (sizeMarker == std::wstring::npos || segmentsMarker == std::wstring::npos) return false;
    const size_t sizeColon = json.find(L':', sizeMarker);
    if (sizeColon == std::wstring::npos) return false;
    total = _wcstoui64(json.c_str() + sizeColon + 1, nullptr, 10);
    size_t cursor = segmentsMarker;
    while ((cursor = json.find(L"\"completed\"", cursor)) != std::wstring::npos) {
        const size_t colon = json.find(L':', cursor);
        if (colon == std::wstring::npos) break;
        completed += _wcstoui64(json.c_str() + colon + 1, nullptr, 10);
        cursor = colon + 1;
    }
    return total != 0;
}

void set_status(const std::wstring& message) { set_text(g_status, message); }

void refresh_list() {
    if (!g_list) return;
    const int selected = ListView_GetNextItem(g_list, -1, LVNI_SELECTED);
    ListView_DeleteAllItems(g_list);
    for (size_t i = 0; i < g_tasks.size(); ++i) {
        Task& task = g_tasks[i];
        uint64_t total = 0, completed = 0;
        file_size(task.output, total);
        if (!total) state_size(task.output, total);
        uint64_t stateTotal = 0, stateCompleted = 0;
        if (state_progress(task.output, stateCompleted, stateTotal)) {
            completed = stateCompleted;
            if (!total) total = stateTotal;
        } else {
            file_size(task.part, completed);
        }
        const bool running = task.process && WaitForSingleObject(task.process, 0) == WAIT_TIMEOUT;
        std::wstring state = task.paused ? L"Paused" : (running ? L"Downloading" : (total ? L"Complete" : L"Ready"));
        wchar_t percent[32];
        const int pct = total ? static_cast<int>((completed * 100) / total) : 0;
        swprintf_s(percent, L"%d%%", pct < 100 ? pct : 100);

        LVITEMW item{}; item.mask = LVIF_TEXT; item.iItem = static_cast<int>(i);
        std::wstring name = file_name(task.output);
        item.pszText = name.data(); ListView_InsertItem(g_list, &item);
        ListView_SetItemText(g_list, item.iItem, 1, const_cast<wchar_t*>(state.c_str()));
        ListView_SetItemText(g_list, item.iItem, 2, percent);
        std::wstring size = total ? format_size(completed) + L" / " + format_size(total) : format_size(completed);
        ListView_SetItemText(g_list, item.iItem, 3, const_cast<wchar_t*>(size.c_str()));
        std::wstring speed = running ? format_speed(task.speed) : L"—";
        ListView_SetItemText(g_list, item.iItem, 4, const_cast<wchar_t*>(speed.c_str()));
        if (static_cast<int>(i) == selected) ListView_SetItemState(g_list, item.iItem, LVIS_SELECTED, LVIS_SELECTED);
    }
    wchar_t summary[128];
    swprintf_s(summary, L"%zu download%s   •   Ready", g_tasks.size(), g_tasks.size() == 1 ? L"" : L"s");
    set_status(summary);
}

std::wstring worker_path() {
    wchar_t module[MAX_PATH];
    GetModuleFileNameW(nullptr, module, MAX_PATH);
    std::wstring path(module);
    const size_t slash = path.find_last_of(L"\\/");
    const std::wstring folder = slash == std::wstring::npos ? L"" : path.substr(0, slash + 1);
    wchar_t backend[32] = {};
    if (GetEnvironmentVariableW(L"LD_BACKEND", backend, ARRAYSIZE(backend)) && _wcsicmp(backend, L"rust") == 0) {
        const std::wstring rust = folder + L"light-downloader-rust.exe";
        if (GetFileAttributesW(rust.c_str()) != INVALID_FILE_ATTRIBUTES) return rust;
    }
    return folder + L"light-downloader.exe";
}

bool start_task(Task& task) {
    if (task.process && WaitForSingleObject(task.process, 0) == WAIT_TIMEOUT) return true;
    std::wstring command = L"\"" + worker_path() + L"\" \"" + task.url + L"\" \"" + task.output +
        L"\" --connections " + std::to_wstring(task.connections) +
        L" --retries " + std::to_wstring(task.retries) +
        L" --limit " + std::to_wstring(task.limit);
    std::vector<wchar_t> commandLine(command.begin(), command.end()); commandLine.push_back(L'\0');
    STARTUPINFOW startup{}; startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW | CREATE_NEW_PROCESS_GROUP, nullptr, nullptr, &startup, &process)) {
        set_status(L"Could not start downloader. Build light-downloader.exe beside LightDownloader.exe.");
        return false;
    }
    CloseHandle(process.hThread);
    task.process = process.hProcess;
    task.paused = false;
    task.started = GetTickCount64();
    task.lastTick = GetTickCount();
    task.lastBytes = 0;
    return true;
}

void pause_task(Task& task) {
    if (!task.process || WaitForSingleObject(task.process, 0) != WAIT_TIMEOUT) return;
    GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, GetProcessId(task.process));
    if (WaitForSingleObject(task.process, 3000) == WAIT_TIMEOUT) TerminateProcess(task.process, 1);
    CloseHandle(task.process); task.process = nullptr; task.paused = true;
}

void update_tasks() {
    for (Task& task : g_tasks) {
        if (!task.process) continue;
        uint64_t bytes = 0; file_size(task.part, bytes);
        const DWORD now = GetTickCount();
        const DWORD elapsed = now - task.lastTick;
        if (elapsed >= 250) {
            task.speed = (bytes >= task.lastBytes) ? (static_cast<double>(bytes - task.lastBytes) * 1000.0 / elapsed) : 0.0;
            task.lastBytes = bytes; task.lastTick = now;
        }
        const DWORD result = WaitForSingleObject(task.process, 0);
        if (result == WAIT_OBJECT_0) {
            DWORD code = 1; GetExitCodeProcess(task.process, &code);
            CloseHandle(task.process); task.process = nullptr;
            task.paused = false;
            if (code == 0) set_status(L"Download completed");
            else set_status(L"Download paused or failed");
        }
    }
    refresh_list();
}

void browse_output() {
    wchar_t path[MAX_PATH] = L"";
    OPENFILENAMEW dialog{}; dialog.lStructSize = sizeof(dialog); dialog.hwndOwner = g_window;
    dialog.lpstrFilter = L"All files\0*.*\0\0"; dialog.lpstrFile = path; dialog.nMaxFile = MAX_PATH;
    dialog.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT;
    if (GetSaveFileNameW(&dialog)) set_text(g_output, path);
}

void add_download() {
    const std::wstring url = text(g_url);
    if (url.find(L"http://") != 0 && url.find(L"https://") != 0) { set_status(L"Enter a valid HTTP or HTTPS URL"); return; }
    std::wstring output = text(g_output);
    if (output.empty()) {
        const size_t slash = url.find_last_of(L'/'); output = slash == std::wstring::npos ? L"download.bin" : url.substr(slash + 1);
        if (output.empty()) output = L"download.bin";
    }
    Task task; task.url = url; task.output = output; task.part = output + L".part";
    task.connections = std::clamp(_wtoi(text(g_connections).c_str()), 1, 16);
    task.retries = std::clamp(_wtoi(text(g_retries).c_str()), 0, 10);
    task.limit = _wcstoui64(text(g_limit).c_str(), nullptr, 10);
    g_tasks.push_back(std::move(task));
    set_text(g_url, L""); set_text(g_output, L""); refresh_list();
    start_task(g_tasks.back()); refresh_list();
}

void remove_selected() {
    const int selected = ListView_GetNextItem(g_list, -1, LVNI_SELECTED);
    if (selected < 0 || selected >= static_cast<int>(g_tasks.size())) return;
    Task& task = g_tasks[static_cast<size_t>(selected)];
    if (task.process) { TerminateProcess(task.process, 1); CloseHandle(task.process); }
    g_tasks.erase(g_tasks.begin() + selected); refresh_list();
}

void start_all() { for (Task& task : g_tasks) start_task(task); refresh_list(); }

void pause_all() { for (Task& task : g_tasks) pause_task(task); refresh_list(); }

void clear_completed() {
    for (size_t i = g_tasks.size(); i-- > 0;) {
        Task& task = g_tasks[i];
        if (!task.process && GetFileAttributesW(task.output.c_str()) != INVALID_FILE_ATTRIBUTES) g_tasks.erase(g_tasks.begin() + static_cast<ptrdiff_t>(i));
    }
    refresh_list();
}

void open_selected() {
    const int selected = ListView_GetNextItem(g_list, -1, LVNI_SELECTED);
    if (selected >= 0 && selected < static_cast<int>(g_tasks.size()))
        ShellExecuteW(g_window, L"open", g_tasks[static_cast<size_t>(selected)].output.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE: {
        g_window = window;
        g_background = CreateSolidBrush(RGB(18, 22, 30)); g_panel = CreateSolidBrush(RGB(28, 34, 45));
        g_font = CreateFontW(-15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        g_titleFont = CreateFontW(-25, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        HWND title = CreateWindowW(L"STATIC", L"Light Downloader", WS_CHILD | WS_VISIBLE, 28, 20, 300, 36, window, nullptr, nullptr, nullptr);
        HWND subtitle = CreateWindowW(L"STATIC", L"Fast, reliable downloads", WS_CHILD | WS_VISIBLE, 30, 55, 300, 24, window, nullptr, nullptr, nullptr);
        CreateWindowW(L"STATIC", L"URL", WS_CHILD | WS_VISIBLE, 28, 78, 100, 18, window, nullptr, nullptr, nullptr);
        g_url = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 28, 96, 510, 34, window, (HMENU)IDC_URL, nullptr, nullptr);
        CreateWindowW(L"STATIC", L"Save to", WS_CHILD | WS_VISIBLE, 28, 124, 100, 18, window, nullptr, nullptr, nullptr);
        g_output = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 28, 142, 390, 34, window, (HMENU)IDC_OUTPUT, nullptr, nullptr);
        CreateWindowW(L"BUTTON", L"Browse", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 428, 142, 110, 34, window, (HMENU)IDC_BROWSE, nullptr, nullptr);
        CreateWindowW(L"STATIC", L"Connections", WS_CHILD | WS_VISIBLE, 560, 101, 100, 22, window, nullptr, nullptr, nullptr);
        g_connections = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"8", WS_CHILD | WS_VISIBLE | ES_NUMBER | ES_CENTER, 665, 96, 55, 34, window, (HMENU)IDC_CONNECTIONS, nullptr, nullptr);
        CreateWindowW(L"STATIC", L"Retries", WS_CHILD | WS_VISIBLE, 28, 188, 70, 22, window, nullptr, nullptr, nullptr);
        g_retries = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"3", WS_CHILD | WS_VISIBLE | ES_NUMBER | ES_CENTER, 92, 183, 55, 30, window, (HMENU)IDC_RETRIES, nullptr, nullptr);
        CreateWindowW(L"STATIC", L"Limit B/s (0 = unlimited)", WS_CHILD | WS_VISIBLE, 165, 188, 165, 22, window, nullptr, nullptr, nullptr);
        g_limit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"0", WS_CHILD | WS_VISIBLE | ES_NUMBER | ES_CENTER, 335, 183, 95, 30, window, (HMENU)IDC_LIMIT, nullptr, nullptr);
        CreateWindowW(L"BUTTON", L"Add download", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, 560, 142, 160, 34, window, (HMENU)IDC_ADD, nullptr, nullptr);
        g_list = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"", WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS, 28, 225, 692, 250, window, (HMENU)IDC_LIST, nullptr, nullptr);
        ListView_SetExtendedListViewStyle(g_list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_GRIDLINES);
        const wchar_t* headers[] = {L"File", L"Status", L"Progress", L"Size", L"Speed"}; const int widths[] = {245, 115, 90, 150, 92};
        for (int i = 0; i < 5; ++i) { LVCOLUMNW column{}; column.mask = LVCF_TEXT | LVCF_WIDTH; column.pszText = const_cast<wchar_t*>(headers[i]); column.cx = widths[i]; ListView_InsertColumn(g_list, i, &column); }
        CreateWindowW(L"BUTTON", L"Start / resume", WS_CHILD | WS_VISIBLE, 28, 494, 140, 36, window, (HMENU)IDC_START, nullptr, nullptr);
        CreateWindowW(L"BUTTON", L"Pause", WS_CHILD | WS_VISIBLE, 178, 494, 110, 36, window, (HMENU)IDC_PAUSE, nullptr, nullptr);
        CreateWindowW(L"BUTTON", L"Start all", WS_CHILD | WS_VISIBLE, 298, 494, 110, 36, window, (HMENU)IDC_START_ALL, nullptr, nullptr);
        CreateWindowW(L"BUTTON", L"Pause all", WS_CHILD | WS_VISIBLE, 418, 494, 110, 36, window, (HMENU)IDC_PAUSE_ALL, nullptr, nullptr);
        CreateWindowW(L"BUTTON", L"Remove", WS_CHILD | WS_VISIBLE, 538, 494, 90, 36, window, (HMENU)IDC_REMOVE, nullptr, nullptr);
        CreateWindowW(L"BUTTON", L"Open", WS_CHILD | WS_VISIBLE, 28, 536, 90, 32, window, (HMENU)IDC_OPEN, nullptr, nullptr);
        CreateWindowW(L"BUTTON", L"Clear completed", WS_CHILD | WS_VISIBLE, 128, 536, 140, 32, window, (HMENU)IDC_CLEAR, nullptr, nullptr);
        g_status = CreateWindowW(L"STATIC", L"0 downloads   •   Ready", WS_CHILD | WS_VISIBLE, 285, 540, 435, 24, window, (HMENU)IDC_STATUS, nullptr, nullptr);
        HWND controls[] = {g_url, g_output, g_connections, g_retries, g_limit, g_list, g_status, subtitle};
        for (HWND control : controls) SendMessageW(control, WM_SETFONT, (WPARAM)g_font, TRUE);
        SendMessageW(title, WM_SETFONT, (WPARAM)g_titleFont, TRUE);
        SendMessageW(GetDlgItem(window, IDC_ADD), WM_SETFONT, (WPARAM)g_font, TRUE);
        SendMessageW(GetDlgItem(window, IDC_START), WM_SETFONT, (WPARAM)g_font, TRUE);
        SendMessageW(GetDlgItem(window, IDC_PAUSE), WM_SETFONT, (WPARAM)g_font, TRUE);
        for (int id : {IDC_REMOVE, IDC_START_ALL, IDC_PAUSE_ALL, IDC_OPEN, IDC_CLEAR}) SendMessageW(GetDlgItem(window, id), WM_SETFONT, (WPARAM)g_font, TRUE);
        SendMessageW(GetWindow(window, GW_CHILD), WM_SETFONT, (WPARAM)g_titleFont, TRUE);
        SetTimer(window, 1, 500, nullptr); return 0;
    }
    case WM_COMMAND:
        switch (LOWORD(wParam)) { case IDC_BROWSE: browse_output(); break; case IDC_ADD: add_download(); break; case IDC_START: { int i = ListView_GetNextItem(g_list, -1, LVNI_SELECTED); if (i >= 0) start_task(g_tasks[i]); refresh_list(); break; } case IDC_PAUSE: { int i = ListView_GetNextItem(g_list, -1, LVNI_SELECTED); if (i >= 0) pause_task(g_tasks[i]); refresh_list(); break; } case IDC_START_ALL: start_all(); break; case IDC_PAUSE_ALL: pause_all(); break; case IDC_REMOVE: remove_selected(); break; case IDC_OPEN: open_selected(); break; case IDC_CLEAR: clear_completed(); break; } return 0;
    case WM_TIMER: update_tasks(); return 0;
    case WM_CTLCOLORSTATIC: { HDC dc = (HDC)wParam; SetTextColor(dc, RGB(218, 224, 235)); SetBkColor(dc, RGB(18, 22, 30)); return (LRESULT)g_background; }
    case WM_CTLCOLOREDIT: { HDC dc = (HDC)wParam; SetTextColor(dc, RGB(235, 240, 248)); SetBkColor(dc, RGB(36, 43, 56)); return (LRESULT)g_panel; }
    case WM_ERASEBKGND: { RECT rect; GetClientRect(window, &rect); FillRect((HDC)wParam, &rect, g_background); return 1; }
    case WM_DESTROY:
        KillTimer(window, 1); for (Task& task : g_tasks) if (task.process) { TerminateProcess(task.process, 1); CloseHandle(task.process); }
        DeleteObject(g_font); DeleteObject(g_titleFont); DeleteObject(g_background); DeleteObject(g_panel); PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    INITCOMMONCONTROLSEX common{sizeof(common), ICC_LISTVIEW_CLASSES}; InitCommonControlsEx(&common);
    WNDCLASSEXW klass{sizeof(klass)}; klass.hInstance = instance; klass.lpfnWndProc = window_proc; klass.lpszClassName = L"LightDownloaderMainWindow";
    klass.hCursor = LoadCursorW(nullptr, IDC_ARROW); klass.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    if (!RegisterClassExW(&klass)) return 1;
    HWND window = CreateWindowExW(0, klass.lpszClassName, L"Light Downloader", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, CW_USEDEFAULT, CW_USEDEFAULT, 770, 635, nullptr, nullptr, instance, nullptr);
    if (!window) return 1;
    ShowWindow(window, show); UpdateWindow(window);
    MSG message{}; while (GetMessageW(&message, nullptr, 0, 0) > 0) { TranslateMessage(&message); DispatchMessageW(&message); }
    return static_cast<int>(message.wParam);
}