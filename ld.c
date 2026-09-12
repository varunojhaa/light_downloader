#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <wchar.h>
#include <strsafe.h>

#define MAX_SEGMENTS 16
#define BUF_SIZE (64 * 1024)
#define STATE_MAGIC L"LDC1"

typedef struct {
    uint64_t first, last, completed;
} Segment;

typedef struct {
    wchar_t *url, *output, *part, *state;
    uint64_t size, downloaded;
    int range_supported, connections, retries;
    uint64_t limit;
    Segment segments[MAX_SEGMENTS];
    HANDLE file, mutex;
    volatile LONG stop;
    DWORD error;
    ULONGLONG started;
} Task;

static volatile LONG *g_stop_flag;

static BOOL WINAPI console_handler(DWORD type) {
    if (type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT || type == CTRL_CLOSE_EVENT) {
        if (g_stop_flag) InterlockedExchange((LONG *)g_stop_flag, 1);
        return TRUE;
    }
    return FALSE;
}

static void usage(void) {
    wprintf(L"Light Downloader 1.0\nUsage: light-downloader URL [OUTPUT] [--connections N] [--retries N] [--limit BYTES_PER_SECOND]\n\n"
            L"Ctrl+C pauses safely; run the same command again to resume.\n");
}

static wchar_t *dupw(const wchar_t *s) {
    size_t n = wcslen(s) + 1; wchar_t *p = malloc(n * sizeof(*p));
    if (p) memcpy(p, s, n * sizeof(*p)); return p;
}

static void free_task(Task *t) { free(t->url); free(t->output); free(t->part); free(t->state); }

static wchar_t *with_suffix(const wchar_t *s, const wchar_t *suffix) {
    size_t n = wcslen(s) + wcslen(suffix) + 1; wchar_t *p = malloc(n * sizeof(*p));
    if (p) { StringCchCopyW(p, n, s); StringCchCatW(p, n, suffix); } return p;
}

static int save_state(Task *t) {
    wchar_t temp[MAX_PATH * 2];
    if (StringCchPrintfW(temp, ARRAYSIZE(temp), L"%ls.tmp", t->state) != S_OK) return 0;
    FILE *f = _wfopen(temp, L"w, ccs=UTF-8"); int ok = 0;
    if (!f) return 0;
    fwprintf(f, L"%ls\n%" PRIu64 L"\n%d\n%d\n", STATE_MAGIC, t->size, t->range_supported, t->connections);
    for (int i = 0; i < t->connections; ++i) fwprintf(f, L"%" PRIu64 L" %" PRIu64 L" %" PRIu64 L"\n", t->segments[i].first, t->segments[i].last, t->segments[i].completed);
    ok = !ferror(f); fclose(f);
    if (ok) ok = MoveFileExW(temp, t->state, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    if (!ok) DeleteFileW(temp);
    return ok;
}

static int load_state(Task *t) {
    FILE *f = _wfopen(t->state, L"r, ccs=UTF-8"); wchar_t magic[16]; int range, saved_connections;
    if (!f) return 0;
    if (fwscanf(f, L"%15ls\n%" SCNu64 L"\n%d\n%d\n", magic, &t->size, &range, &saved_connections) != 4 || wcscmp(magic, STATE_MAGIC) != 0 || saved_connections < 1 || saved_connections > MAX_SEGMENTS) { fclose(f); return 0; }
    t->range_supported = range;
    t->connections = saved_connections;
    for (int i = 0; i < t->connections; ++i) {
        if (fwscanf(f, L"%" SCNu64 L" %" SCNu64 L" %" SCNu64 L"\n", &t->segments[i].first, &t->segments[i].last, &t->segments[i].completed) != 3) { fclose(f); return 0; }
        t->downloaded += t->segments[i].completed;
    }
    fclose(f); return 1;
}

static HINTERNET open_request(Task *t, uint64_t first, uint64_t last, BOOL head, HINTERNET *session) {
    URL_COMPONENTS c = { sizeof(c) }; wchar_t host[256], path[2048], range[128];
    c.lpszHostName = host; c.dwHostNameLength = ARRAYSIZE(host); c.lpszUrlPath = path; c.dwUrlPathLength = ARRAYSIZE(path);
    if (!WinHttpCrackUrl(t->url, 0, 0, &c) || c.dwHostNameLength >= ARRAYSIZE(host) || c.dwUrlPathLength >= ARRAYSIZE(path)) return NULL;
    *session = WinHttpOpen(L"LightDownloader/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, NULL, NULL, 0);
    if (!*session) return NULL;
    HINTERNET connect = WinHttpConnect(*session, host, c.nPort, 0);
    if (!connect) return NULL;
    DWORD flags = c.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET req = WinHttpOpenRequest(connect, head ? L"HEAD" : L"GET", path, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!req) return NULL;
    if (!head && t->range_supported) { StringCchPrintfW(range, ARRAYSIZE(range), L"Range: bytes=%" PRIu64 L"-%" PRIu64, first, last); WinHttpAddRequestHeaders(req, range, (DWORD)-1L, WINHTTP_ADDREQ_FLAG_ADD); }
    if (!WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0, NULL, 0, 0, 0) || !WinHttpReceiveResponse(req, NULL)) { WinHttpCloseHandle(req); return NULL; }
    WinHttpCloseHandle(connect); return req;
}

static uint64_t header_number(HINTERNET req, const wchar_t *name) {
    wchar_t b[128]; DWORD n = sizeof(b); if (!WinHttpQueryHeaders(req, WINHTTP_QUERY_CUSTOM, name, b, &n, NULL)) return 0;
    return _wcstoui64(b, NULL, 10);
}

static DWORD response_status(HINTERNET req) {
    DWORD status = 0, n = sizeof(status);
    if (!WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, NULL, &status, &n, NULL)) return 0;
    return status;
}

static int probe(Task *t) {
    HINTERNET s = NULL, r = open_request(t, 0, 0, TRUE, &s); if (!r) return 0;
    uint64_t len = header_number(r, L"Content-Length"); wchar_t accept[32]; DWORD n = sizeof(accept);
    t->range_supported = WinHttpQueryHeaders(r, WINHTTP_QUERY_CUSTOM, L"Accept-Ranges", accept, &n, NULL) && _wcsicmp(accept, L"bytes") == 0;
    WinHttpCloseHandle(r); WinHttpCloseHandle(s);
    if (!len) return 0; t->size = len; return 1;
}

static int write_at(Task *t, uint64_t offset, const void *buf, DWORD n) {
    OVERLAPPED ov = {0}; ov.Offset = (DWORD)offset; ov.OffsetHigh = (DWORD)(offset >> 32); DWORD written;
    return WriteFile(t->file, buf, n, &written, &ov) && written == n;
}

static DWORD WINAPI segment_worker(void *arg) {
    struct { Task *t; int index; } *a = arg; Task *t = a->t; Segment *seg = &t->segments[a->index]; free(a);
    int tries = 0; char *buf = malloc(BUF_SIZE); if (!buf) return 1;
    while (!t->stop && seg->completed <= seg->last - seg->first) {
        HINTERNET session = NULL, req = open_request(t, seg->first + seg->completed, seg->last, FALSE, &session);
        if (!req) { if (++tries > t->retries) { t->error = GetLastError(); break; } Sleep(500u << (tries - 1)); continue; }
        if ((t->range_supported && response_status(req) != HTTP_STATUS_PARTIAL_CONTENT) ||
            (!t->range_supported && response_status(req) != HTTP_STATUS_OK)) {
            WinHttpCloseHandle(req); WinHttpCloseHandle(session); t->error = ERROR_INVALID_DATA; break;
        }
        DWORD got; BOOL success = TRUE;
        while (!t->stop && WinHttpReadData(req, buf, BUF_SIZE, &got) && got) {
            if (!write_at(t, seg->first + seg->completed, buf, got)) { success = FALSE; break; }
            WaitForSingleObject(t->mutex, INFINITE); seg->completed += got; t->downloaded += got; save_state(t); ReleaseMutex(t->mutex);
            if (t->limit) Sleep((DWORD)((1000ULL * got) / t->limit));
        }
        WinHttpCloseHandle(req); WinHttpCloseHandle(session);
        if (t->stop) break;
        if (success && seg->completed == seg->last - seg->first + 1) break;
        if (++tries > t->retries) { t->error = ERROR_CONNECTION_ABORTED; break; } Sleep(500u << (tries - 1));
    }
    free(buf); return 0;
}

static int run(Task *t) {
    LARGE_INTEGER li; li.QuadPart = (LONGLONG)t->size; SetFilePointerEx(t->file, li, NULL, FILE_BEGIN); SetEndOfFile(t->file);
    HANDLE threads[MAX_SEGMENTS]; t->started = GetTickCount64();
    for (int i = 0; i < t->connections; ++i) { struct { Task *t; int index; } *a = malloc(sizeof(*a)); a->t = t; a->index = i; threads[i] = CreateThread(NULL, 0, segment_worker, a, 0, NULL); }
    while (1) { int done = 0; for (int i = 0; i < t->connections; ++i) { if (WaitForSingleObject(threads[i], 100) == WAIT_OBJECT_0) done++; } if (done == t->connections || t->stop || t->error) break; double elapsed = (GetTickCount64() - t->started) / 1000.0; wprintf(L"\r%6.2f%%  %" PRIu64 L"/%" PRIu64 L" bytes  %6.1f KB/s", t->size ? 100.0 * t->downloaded / t->size : 0.0, t->downloaded, t->size, elapsed > 0 ? t->downloaded / elapsed / 1024.0 : 0.0); }
    for (int i = 0; i < t->connections; ++i) CloseHandle(threads[i]); save_state(t); wprintf(L"\n"); return !t->error && !t->stop;
}

int wmain(int argc, wchar_t **argv) {
    if (argc < 2 || !_wcsicmp(argv[1], L"--help")) { usage(); return argc < 2; }
    Task t = {0}; t.url = dupw(argv[1]); t.connections = 8; t.retries = 3;
    t.output = argc > 2 && argv[2][0] != L'-' ? dupw(argv[2]) : NULL;
    for (int i = 2; i < argc; ++i) { if (!_wcsicmp(argv[i], L"--connections") && i + 1 < argc) t.connections = _wtoi(argv[++i]); else if (!_wcsicmp(argv[i], L"--retries") && i + 1 < argc) t.retries = _wtoi(argv[++i]); else if (!_wcsicmp(argv[i], L"--limit") && i + 1 < argc) t.limit = _wcstoui64(argv[++i], NULL, 10); }
    if (t.connections < 1) t.connections = 1; if (t.connections > MAX_SEGMENTS) t.connections = MAX_SEGMENTS; if (t.retries < 0) t.retries = 0;
    if (!t.output) { const wchar_t *p = wcsrchr(t.url, L'/'); t.output = dupw(p ? p + 1 : L"download.bin"); if (!*t.output) { free(t.output); t.output = dupw(L"download.bin"); } }
    t.part = with_suffix(t.output, L".part"); t.state = with_suffix(t.output, L".ld");
    if (!t.url || !t.output || !t.part || !t.state) { fwprintf(stderr, L"Out of memory\n"); free_task(&t); return 2; }
    g_stop_flag = &t.stop; SetConsoleCtrlHandler(console_handler, TRUE); t.mutex = CreateMutexW(NULL, FALSE, NULL); t.file = CreateFileW(t.part, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (t.file == INVALID_HANDLE_VALUE) { fwprintf(stderr, L"Cannot open %ls (%lu)\n", t.part, GetLastError()); free_task(&t); return 2; }
    if (!load_state(&t)) { if (!probe(&t)) { fwprintf(stderr, L"Could not obtain file size or connect to server (%lu)\n", GetLastError()); CloseHandle(t.file); free_task(&t); return 1; } t.connections = t.range_supported ? t.connections : 1; uint64_t chunk = (t.size + t.connections - 1) / t.connections; for (int i = 0; i < t.connections; ++i) { t.segments[i].first = i * chunk; t.segments[i].last = (i == t.connections - 1 ? t.size - 1 : (i + 1) * chunk - 1); } save_state(&t); }
    if (!t.range_supported) {
        /* A server without byte ranges cannot resume at an offset safely. */
        if (t.downloaded != 0) {
            t.downloaded = 0;
            SetFilePointer(t.file, 0, NULL, FILE_BEGIN);
            SetEndOfFile(t.file);
            DeleteFileW(t.state);
            t.segments[0].first = 0;
            t.segments[0].last = t.size - 1;
            t.segments[0].completed = 0;
            save_state(&t);
        }
        t.connections = 1;
    }
    wprintf(L"Downloading %ls\n", t.url); int ok = run(&t);
    if (ok) { MoveFileExW(t.part, t.output, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH); DeleteFileW(t.state); wprintf(L"Completed: %ls\n", t.output); } else if (t.stop) wprintf(L"Paused. Run the same command to resume.\n"); else fwprintf(stderr, L"Download failed (%lu).\n", t.error);
    CloseHandle(t.file); CloseHandle(t.mutex); free_task(&t); return ok ? 0 : 1;
}