/* ScanPort - 端口扫描工具 (Win32 GUI, C, 无额外依赖)
 *
 * 布局与功能：起始IP / 结束IP / 端口号(支持 80,139,440-445) /
 * 超时(毫秒) / 线程数 / 扫描结果列表 / 扫描 / 关于 / 退出。
 * TCP connect 扫描，无需管理员权限。源码为 UTF-8，编译前需转 GBK。
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <winsock2.h>
#include <ws2tcpip.h>

/* 无 CRT 构建（-nostdlib）：不用 memset/strtol/atoi/stdio，避免拉入 C 运行时，
   体积才能压到几十 KB。编译见文件末尾注释。 */

#define IDC_STARTIP  101
#define IDC_ENDIP    102
#define IDC_PORTS    103
#define IDC_TIMEOUT  104
#define IDC_THREADS  105
#define IDC_LIST     106
#define IDC_SCAN     107
#define IDC_ABOUT    108
#define IDC_EXIT     109
#define IDC_PROGRESS 110
#define IDC_STOP     111
#define IDT_PROGRESS 1
#define MAX_RESULT_ROWS 10000

#define WM_SCAN_HIT  (WM_USER + 1)
#define WM_SCAN_DONE (WM_USER + 2)

static HWND g_hwnd, g_hStartIp, g_hEndIp, g_hPorts, g_hTimeout, g_hThreads, g_hList, g_hScanBtn, g_hStopBtn, g_hProgress;
static HFONT g_font;
static volatile LONG g_scanning = 0;
static volatile LONG g_stop = 0;
static volatile LONGLONG g_nextTask __attribute__((aligned(8))) = 0;
static volatile LONG g_activeWorkers = 0;
static volatile LONGLONG g_found __attribute__((aligned(8))) = 0;
static volatile LONGLONG g_done __attribute__((aligned(8))) = 0;
static DWORD g_startIp;
static int g_ports[65536];
static int g_nPorts;
static int g_timeout;
static ULONGLONG g_totalTasks;

/* memset 的替代：普通字节循环（配合 -ffreestanding -fno-builtin，不会被优化成 memset 调用） */
static void Zero(void *p, unsigned n) {
    unsigned char *q = (unsigned char *)p;
    while (n--) *q++ = 0;
}

/* 64-bit division by a 32-bit value without pulling in the CRT. */
static DWORD DivMod64By32(ULONGLONG value, DWORD divisor, ULONGLONG *quotient) {
    ULONGLONG q = 0;
    DWORD remainder = 0;
    int i;
    for (i = 3; i >= 0; i--) {
        DWORD part = (DWORD)(value >> (i * 16)) & 0xffff;
        DWORD dividend = (remainder << 16) | part;
        DWORD digit = dividend / divisor;
        remainder = dividend % divisor;
        q = (q << 16) | digit;
    }
    *quotient = q;
    return remainder;
}

/* atoi 的替代：只解析前导十进制数字 */
static int Atoi(const char *s) {
    int v = 0;
    while (*s == ' ' || *s == '\t') s++;
    while (*s >= '0' && *s <= '9') { v = v * 10 + (*s - '0'); s++; }
    return v;
}

/* 解析 "80,139,440-445" -> 去重升序端口表，失败返回 -1 */
static int ParsePorts(const char *s, int *out) {
    static unsigned char seen[65536];
    int i, n = 0;
    const char *p = s;
    Zero(seen, sizeof(seen));
    while (*p) {
        long a, b;
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        if (*p < '0' || *p > '9') return -1;
        a = 0;
        while (*p >= '0' && *p <= '9') {
            a = a * 10 + (*p - '0');
            p++;
            if (a > 65535) return -1;
        }
        b = a;
        if (*p == '-') {
            p++;
            if (*p < '0' || *p > '9') return -1;
            b = 0;
            while (*p >= '0' && *p <= '9') {
                b = b * 10 + (*p - '0');
                p++;
                if (b > 65535) return -1;
            }
        }
        if (a < 1 || a > 65535 || b < 1 || b > 65535 || b < a) return -1;
        for (; a <= b; a++) seen[a] = 1;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == ',') p++;
        else if (*p) return -1;
    }
    for (i = 1; i <= 65535; i++) if (seen[i]) out[n++] = i;
    return n;
}

/* 非阻塞 connect + select 实现带超时的 TCP 连接 */
static int TryConnect(DWORD ip, int port, int timeoutMs) {
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    struct sockaddr_in addr;
    struct sockaddr_in peer;
    u_long nb = 1;
    DWORD started;
    int rc, err, errlen;
    int open = 0;
    if (s == INVALID_SOCKET) return 0;
    if (InterlockedCompareExchange(&g_stop, 0, 0)) {
        closesocket(s);
        return 0;
    }
    if (ioctlsocket(s, FIONBIO, &nb) == SOCKET_ERROR) {
        closesocket(s);
        return 0;
    }
    Zero(&addr, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(ip);
    addr.sin_port = htons((u_short)port);
    rc = connect(s, (struct sockaddr *)&addr, sizeof(addr));
    if (rc == SOCKET_ERROR) {
        int connectError = WSAGetLastError();
        if (connectError != WSAEWOULDBLOCK && connectError != WSAEINPROGRESS)
            goto done;
        started = GetTickCount();
        for (;;) {
            fd_set wf;
            fd_set ef;
            struct timeval tv;
            DWORD elapsed = GetTickCount() - started;
            DWORD waitMs;
            if (InterlockedCompareExchange(&g_stop, 0, 0) || elapsed >= (DWORD)timeoutMs)
                goto done;
            waitMs = (DWORD)timeoutMs - elapsed;
            if (waitMs > 100) waitMs = 100;
            FD_ZERO(&wf);
            FD_ZERO(&ef);
            FD_SET(s, &wf);
            FD_SET(s, &ef);
            tv.tv_sec = waitMs / 1000;
            tv.tv_usec = (waitMs % 1000) * 1000;
            rc = select(0, NULL, &wf, &ef, &tv);
            if (rc == SOCKET_ERROR) goto done;
            if (rc == 0) continue;
            if (FD_ISSET(s, &ef)) goto done;
            if (!FD_ISSET(s, &wf)) continue;
            err = 0;
            errlen = sizeof(err);
            if (getsockopt(s, SOL_SOCKET, SO_ERROR, (char *)&err, &errlen) == SOCKET_ERROR || err != 0)
                goto done;
            break;
        }
    }
    if (InterlockedCompareExchange(&g_stop, 0, 0)) goto done;
    Zero(&peer, sizeof(peer));
    {
        int peerLen = sizeof(peer);
        if (getpeername(s, (struct sockaddr *)&peer, &peerLen) == 0 &&
            peer.sin_family == AF_INET &&
            peer.sin_addr.s_addr == addr.sin_addr.s_addr &&
            peer.sin_port == addr.sin_port)
            open = 1;
    }
done:
    closesocket(s);
    return open;
}

static DWORD WINAPI WorkerProc(LPVOID p) {
    (void)p;
    for (;;) {
        LONGLONG idx;
        ULONGLONG hostIdx;
        int portIdx;
        DWORD ip;
        if (g_stop) break;
        idx = InterlockedIncrement64(&g_nextTask) - 1;
        if ((ULONGLONG)idx >= g_totalTasks) break;
        portIdx = (int)DivMod64By32((ULONGLONG)idx, (DWORD)g_nPorts, &hostIdx);
        ip = g_startIp + (DWORD)hostIdx;
        if (TryConnect(ip, g_ports[portIdx], g_timeout) &&
            !InterlockedCompareExchange(&g_stop, 0, 0)) {
            LONGLONG found = InterlockedIncrement64(&g_found);
            if (found <= MAX_RESULT_ROWS)
                PostMessage(g_hwnd, WM_SCAN_HIT, (WPARAM)ip, (LPARAM)g_ports[portIdx]);
        }
        InterlockedIncrement64(&g_done);
    }
    if (InterlockedDecrement(&g_activeWorkers) == 0)
        PostMessage(g_hwnd, WM_SCAN_DONE, 0, 0);
    return 0;
}

/* IP 控件是否 4 段都填了（有一段空白即不完整） */
static int IpIsComplete(HWND hIp) {
    char buf[64];
    int parts = 0, curLen = 0, i;
    GetWindowText(hIp, buf, sizeof(buf));
    for (i = 0; ; i++) {
        char c = buf[i];
        if (c == '.' || c == '\0') {
            if (curLen == 0) return 0;
            parts++;
            curLen = 0;
            if (c == '\0') break;
        } else {
            curLen++;
        }
    }
    return parts == 4;
}

static void OnScan(HWND hwnd) {
    DWORD startIp = 0, endIp = 0;
    ULONGLONG hosts;
    char buf[64];
    static char portText[4096]; /* static：避免 4KB+ 栈帧引入 __chkstk（无 CRT 时无此函数） */
    int n, timeout, threads, i, created = 0, clearResults;
    ULONGLONG total;
    if (g_scanning) return;
    if (!IpIsComplete(g_hStartIp) || !IpIsComplete(g_hEndIp)) {
        MessageBox(hwnd, "IP填写不完整", "提示", MB_OK | MB_ICONWARNING);
        return;
    }
    SendMessage(g_hStartIp, IPM_GETADDRESS, 0, (LPARAM)&startIp);
    SendMessage(g_hEndIp, IPM_GETADDRESS, 0, (LPARAM)&endIp);
    if (endIp < startIp) {
        MessageBox(hwnd, "结束IP不能小于起始IP", "提示", MB_OK | MB_ICONWARNING);
        return;
    }
    hosts = (ULONGLONG)endIp - startIp + 1;
    GetWindowText(g_hPorts, portText, sizeof(portText));
    n = ParsePorts(portText, g_ports);
    if (n <= 0) {
        MessageBox(hwnd, "端口号格式错误，例如：80,139,440-445", "提示", MB_OK | MB_ICONWARNING);
        return;
    }
    g_nPorts = n;
    GetWindowText(g_hTimeout, buf, sizeof(buf));
    timeout = Atoi(buf);
    if (timeout < 10) timeout = 10;
    if (timeout > 30000) timeout = 30000;
    GetWindowText(g_hThreads, buf, sizeof(buf));
    threads = Atoi(buf);
    if (threads < 1) threads = 1;
    if (threads > 500) threads = 500;
    total = hosts * (ULONGLONG)n;
    clearResults = (int)SendMessage(g_hList, LB_GETCOUNT, 0, 0) == 0;
    if (!clearResults) {
        clearResults = MessageBox(hwnd,
            "已有上次扫描结果，是否清除？\n选择“否”将保留并追加本次扫描结果。",
            "确认扫描", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) == IDYES;
    }
    g_startIp = startIp;
    g_timeout = timeout;
    g_totalTasks = total;
    InterlockedExchange64(&g_nextTask, 0);
    InterlockedExchange64(&g_found, 0);
    InterlockedExchange64(&g_done, 0);
    g_stop = 0;
    if (clearResults)
        SendMessage(g_hList, LB_RESETCONTENT, 0, 0);
    SendMessage(g_hProgress, PBM_SETRANGE32, 0, 1000);
    SendMessage(g_hProgress, PBM_SETPOS, 0, 0);
    g_scanning = 1;
    EnableWindow(g_hScanBtn, FALSE);
    EnableWindow(g_hStopBtn, FALSE);
    g_activeWorkers = threads;
    for (i = 0; i < threads; i++) {
        HANDLE h = CreateThread(NULL, 0, WorkerProc, NULL, 0, NULL);
        if (h) {
            CloseHandle(h);
            created++;
        } else {
            /* 创建失败也要扣计数；扣到 0 时由这里补发 DONE，
               否则按钮会一直灰着（恰好只有一个扣到 0 的地方发 DONE）。 */
            if (InterlockedDecrement(&g_activeWorkers) == 0)
                PostMessage(hwnd, WM_SCAN_DONE, 0, 0);
        }
    }
    if (created == 0) {
        g_scanning = 0;
        EnableWindow(g_hScanBtn, TRUE);
        MessageBox(hwnd, "创建扫描线程失败", "错误", MB_OK | MB_ICONERROR);
    } else {
        SetWindowText(g_hStopBtn, "终止扫描");
        EnableWindow(g_hStopBtn, TRUE);
        SetTimer(hwnd, IDT_PROGRESS, 100, NULL);
    }
}

static HWND MakeCtl(HWND parent, const char *cls, const char *text, DWORD style,
                    int x, int y, int w, int h, int id) {
    HWND ctl = CreateWindowEx(0, cls, text, WS_CHILD | WS_VISIBLE | style,
                            x, y, w, h, parent, (HMENU)(INT_PTR)id,
                            GetModuleHandle(NULL), NULL);
    if (ctl) SendMessage(ctl, WM_SETFONT, (WPARAM)g_font, TRUE);
    return ctl;
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_INTERNET_CLASSES | ICC_PROGRESS_CLASS };
        InitCommonControlsEx(&icc);
        g_font = CreateFont(-12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                            GB2312_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                            DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "宋体");
        if (!g_font) g_font = GetStockObject(DEFAULT_GUI_FONT);
        MakeCtl(hwnd, "BUTTON", "信息设置", BS_GROUPBOX, 8, 8, 212, 196, 0);
        MakeCtl(hwnd, "STATIC", "起始IP:", SS_LEFT, 20, 30, 52, 14, 0);
        g_hStartIp = MakeCtl(hwnd, "SysIPAddress32", NULL, WS_TABSTOP, 76, 28, 132, 20, IDC_STARTIP);
        MakeCtl(hwnd, "STATIC", "结束IP:", SS_LEFT, 20, 58, 52, 14, 0);
        g_hEndIp = MakeCtl(hwnd, "SysIPAddress32", NULL, WS_TABSTOP, 76, 56, 132, 20, IDC_ENDIP);
        MakeCtl(hwnd, "STATIC", "端口号:", SS_LEFT, 20, 86, 52, 14, 0);
        g_hPorts = MakeCtl(hwnd, "EDIT", "80,139,440-445", WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP, 76, 84, 132, 20, IDC_PORTS);
        MakeCtl(hwnd, "STATIC", "超时:", SS_LEFT, 20, 114, 40, 14, 0);
        g_hTimeout = MakeCtl(hwnd, "EDIT", "200", WS_BORDER | ES_AUTOHSCROLL | ES_NUMBER | WS_TABSTOP, 76, 112, 60, 20, IDC_TIMEOUT);
        MakeCtl(hwnd, "STATIC", "毫秒", SS_LEFT, 142, 114, 30, 14, 0);
        MakeCtl(hwnd, "STATIC", "线程数:", SS_LEFT, 20, 142, 52, 14, 0);
        g_hThreads = MakeCtl(hwnd, "EDIT", "10", WS_BORDER | ES_AUTOHSCROLL | ES_NUMBER | WS_TABSTOP, 76, 140, 60, 20, IDC_THREADS);
        MakeCtl(hwnd, "BUTTON", "扫描结果", BS_GROUPBOX, 228, 8, 244, 196, 0);
        g_hList = MakeCtl(hwnd, "LISTBOX", NULL, WS_BORDER | WS_VSCROLL | WS_HSCROLL | LBS_NOINTEGRALHEIGHT | WS_TABSTOP, 236, 28, 228, 164, IDC_LIST);
        g_hScanBtn = MakeCtl(hwnd, "BUTTON", "扫描", BS_DEFPUSHBUTTON | WS_TABSTOP, 52, 222, 72, 26, IDC_SCAN);
        g_hStopBtn = MakeCtl(hwnd, "BUTTON", "终止扫描", WS_TABSTOP, 140, 222, 72, 26, IDC_STOP);
        EnableWindow(g_hStopBtn, FALSE);
        MakeCtl(hwnd, "BUTTON", "关于...", WS_TABSTOP, 228, 222, 72, 26, IDC_ABOUT);
        MakeCtl(hwnd, "BUTTON", "退出", WS_TABSTOP, 316, 222, 72, 26, IDC_EXIT);
        g_hProgress = MakeCtl(hwnd, "msctls_progress32", NULL, 0, 8, 258, 464, 14, IDC_PROGRESS);
        SendMessage(g_hStartIp, IPM_SETADDRESS, 0, MAKEIPADDRESS(192, 168, 1, 1));
        return 0;
    }
    case WM_NOTIFY: {
        /* 起始IP改动时，结束IP自动填充前3段，第4段默认为 254 */
        LPNMHDR nm = (LPNMHDR)lp;
        if (nm->code == IPN_FIELDCHANGED && nm->hwndFrom == g_hStartIp) {
            DWORD startAddr = 0, endAddr = 0, newEnd;
            SendMessage(g_hStartIp, IPM_GETADDRESS, 0, (LPARAM)&startAddr);
            SendMessage(g_hEndIp, IPM_GETADDRESS, 0, (LPARAM)&endAddr);
            newEnd = (startAddr & 0xFFFFFF00) | 254;
            if (newEnd != endAddr)
                SendMessage(g_hEndIp, IPM_SETADDRESS, 0, newEnd);
            return 0;
        }
        break;
    }
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDC_SCAN: OnScan(hwnd); break;
        case IDC_STOP:
            if (g_scanning && InterlockedExchange(&g_stop, 1) == 0) {
                SetWindowText(g_hStopBtn, "正在停止");
                EnableWindow(g_hStopBtn, FALSE);
            }
            break;
        case IDC_ABOUT:
            MessageBox(hwnd, "ScanPort v1.0\n端口扫描工具\n\n作者：FuYun\n邮箱：fuyun@mosx.cn",
                       "关于", MB_OK | MB_ICONINFORMATION);
            break;
        case IDC_EXIT: DestroyWindow(hwnd); break;
        }
        return 0;
    case WM_SCAN_HIT: {
        char line[64];
        DWORD ip = (DWORD)wp;
        wsprintf(line, "%d.%d.%d.%d:%d 开放", (ip >> 24) & 0xff, (ip >> 16) & 0xff, (ip >> 8) & 0xff, ip & 0xff, (int)lp);
        SendMessage(g_hList, LB_ADDSTRING, 0, (LPARAM)line);
        return 0;
    }
    case WM_TIMER:
        if (wp == IDT_PROGRESS) {
            ULONGLONG done = (ULONGLONG)InterlockedCompareExchange64(&g_done, 0, 0);
            ULONGLONG scaledTasks = g_totalTasks;
            ULONGLONG scaledDone = done * 1000;
            int low = 0, high = 1000;
            while (low < high) {
                int mid = (low + high + 1) / 2;
                if (scaledTasks * (ULONGLONG)mid <= scaledDone)
                    low = mid;
                else
                    high = mid - 1;
            }
            int progress = low;
            SendMessage(g_hProgress, PBM_SETPOS, (WPARAM)progress, 0);
        }
        return 0;
    case WM_SCAN_DONE: {
        char line[128];
        ULONGLONG found = (ULONGLONG)InterlockedCompareExchange64(&g_found, 0, 0);
        ULONGLONG done = (ULONGLONG)InterlockedCompareExchange64(&g_done, 0, 0);
        int stopped = InterlockedCompareExchange(&g_stop, 0, 0);
        if (!g_scanning) return 0;
        g_scanning = 0;
        EnableWindow(g_hScanBtn, TRUE);
        EnableWindow(g_hStopBtn, FALSE);
        SetWindowText(g_hStopBtn, "终止扫描");
        KillTimer(hwnd, IDT_PROGRESS);
        if (!stopped) SendMessage(g_hProgress, PBM_SETPOS, 1000, 0);
        if (stopped)
            wsprintf(line, "扫描已终止，已检查 %I64u 项，发现 %I64u 个开放端口", done, found);
        else
            wsprintf(line, "扫描完成，发现 %I64u 个开放端口", found);
        SendMessage(g_hList, LB_ADDSTRING, 0, (LPARAM)line);
        if (found > MAX_RESULT_ROWS)
            SendMessage(g_hList, LB_ADDSTRING, 0, (LPARAM)"仅显示前 10000 条结果");
        return 0;
    }
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        g_stop = 1;
        WSACleanup();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR cmd, int show) {
    WNDCLASSEX wc;
    HWND hwnd;
    MSG m;
    RECT rc = { 0, 0, 480, 280 };
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    WSADATA wsa;
    (void)hPrev; (void)cmd;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    Zero(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.hIcon = LoadIcon(hInst, MAKEINTRESOURCE(1));
    wc.hIconSm = LoadIcon(hInst, MAKEINTRESOURCE(1));
    wc.lpszClassName = "ScanPort";
    RegisterClassEx(&wc);
    AdjustWindowRect(&rc, style, FALSE);
    hwnd = CreateWindow("ScanPort", "ScanPort", style,
                        (GetSystemMetrics(SM_CXSCREEN) - (rc.right - rc.left)) / 2,
                        (GetSystemMetrics(SM_CYSCREEN) - (rc.bottom - rc.top)) / 2,
                        rc.right - rc.left, rc.bottom - rc.top,
                        NULL, NULL, hInst, NULL);
    if (!hwnd) return 1;
    g_hwnd = hwnd;
    ShowWindow(hwnd, show);
    UpdateWindow(hwnd);
    while (GetMessage(&m, NULL, 0, 0)) {
        if (!IsDialogMessage(hwnd, &m)) {
            TranslateMessage(&m);
            DispatchMessage(&m);
        }
    }
    return (int)m.wParam;
}

/* CRT-less 入口：OS 调用入口时不传参数，WinMain 的 4 个参数需自己取。
   之前直接以 WinMain 为入口，hInstance 等参数是栈上垃圾，导致建窗失败、双击无反应。 */
void WINAPI Entry(void) {
    STARTUPINFOA si;
    Zero(&si, sizeof(si));
    si.cb = sizeof(si);
    GetStartupInfoA(&si);
    WinMain(GetModuleHandle(NULL), NULL, GetCommandLineA(),
            (si.dwFlags & STARTF_USESHOWWINDOW) ? si.wShowWindow : SW_SHOWDEFAULT);
    ExitProcess(0);
}
