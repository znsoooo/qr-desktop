#define UNICODE
#define WINVER 0x0500

#include <stdio.h>
#include <windows.h>
#include <winuser.h>
#include <commctrl.h>
#include "qrcodegen.h"

char* fileencode2(char *path, char *data, int size);
char* filedecodeW(wchar_t *wstr);


#define    QR_VERSION     L"v0.3.2"
#define    QR_TITLE       L"QR Desktop"
#define    QR_ICON        1
#define    QR_PAGE_SIZE   2000 // 1个汉字占3个字节
#define    QR_PAGE_BUFF   QR_PAGE_SIZE + 32


HINSTANCE  g_hInstance;
HWND       g_hwnd;
HMENU      g_menu;
HHOOK      g_hook;           // Handler of hook
bool       g_show = QR_ICON; // 界面显示状态 默认状态和是否显示图标一致


#define WM_ON_TRAY (WM_USER + 0x100)
#define WM_QR_CODE (WM_USER + 0x110)
#define ID_EXIT     40001

LRESULT CALLBACK KeyboardProc(int nCode, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

void win_Sizing(HWND hwnd);


// 剪切板
typedef struct {
    char str[QR_PAGE_BUFF];
} Seg;

int  g_size  = 0;
Seg* g_pages = NULL;

int g_seq    = 0;
int g_index  = -1;
int g_length = 0;
int g_width  = 0;

// 设置double buffering
HDC hDC;
HDC memDC;

// 气泡提示
TOOLINFO ti = {0};
HWND hwndTT = NULL;


#define expr(v) printf(#v"=%g\n", (float)v)

void OpenConsole()
{
    AllocConsole();
    freopen("CONOUT$", "w", stdout);
    freopen("CONOUT$", "w", stderr);
    printf("Console:\n");
}

void SetAutoRun()
{
    wchar_t mpath[MAX_PATH + 16] = L"\"";
    HKEY hKey;

    GetModuleFileName(0, mpath + 1, MAX_PATH); // get self path
    int ret = RegOpenKey(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", &hKey);
    ret = RegSetValueEx(hKey, L"qrcode", 0, REG_SZ, (char*)wcscat(mpath, L"\" hide"), MAX_PATH + 16);
    if(ret == 0)
        RegCloseKey(hKey);
}

void SetToolTip(HWND hwndParent, const char *text)
{
    wchar_t wtext[QR_PAGE_BUFF];
    MultiByteToWideChar(CP_UTF8, 0, text, -1, wtext, QR_PAGE_BUFF);

    if (!hwndTT) {
        hwndTT = CreateWindowEx(WS_EX_TOPMOST, TOOLTIPS_CLASS, NULL,
                                WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
                                CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                                hwndParent, NULL, g_hInstance, NULL);

        // Fix unknown reason tooltip make main window no longer topmost
        SetWindowPos(hwndParent, HWND_TOPMOST, 200, 200, 0, 0, SWP_NOMOVE | SWP_NOSIZE);

        ti.cbSize   = sizeof(TOOLINFO) - sizeof(void*);
        ti.uFlags   = TTF_SUBCLASS;
        ti.hwnd     = hwndParent;
        ti.hinst    = g_hInstance;

        SendMessage(hwndTT, TTM_SETMAXTIPWIDTH, 0, 600);
        SendMessage(hwndTT, TTM_ADDTOOL, 0, (LPARAM)&ti);
    }
    ti.lpszText = wtext;
    GetClientRect(hwndParent, &ti.rect);
    SendMessage(hwndTT, TTM_SETTOOLINFO, 0, (LPARAM)&ti);
}

char* GetCopiedFile()
{
    HANDLE handle;
    if (handle = GetClipboardData(CF_HDROP)) {
        if (DragQueryFileA(handle, -1, NULL, 0) == 1) {
            int size = DragQueryFileA(handle, 0, 0, 0);
            char *path = calloc(size + 1, 1);
            DragQueryFileA(handle, 0, path, size + 1); // todo: why +1?
            return path;
        }
    }
    return 0;
}

void SelectFile(char *path)
{
    if (path) {
        SetForegroundWindow(g_hwnd);
        char args[strlen(path) + 32];
        sprintf(args, "/select,\"%s\"", path);
        ShellExecuteA(0, "open", "explorer", args, 0, SW_SHOWNORMAL);
    }
}

wchar_t* DecodeData(int codepage, char *data, int size)
{
    int wsize = MultiByteToWideChar(codepage, MB_ERR_INVALID_CHARS, data, size, 0, 0);
    if (wsize) {
        wchar_t *wdata = calloc(wsize + 1, sizeof(wchar_t));
        MultiByteToWideChar(codepage, MB_ERR_INVALID_CHARS, data, size, wdata, wsize);
        return wdata;
    } else {
        return 0;
    }
}

wchar_t* DecodeFile(char *path, int *encode)
{
    // 获取文件名
    char *file = strrchr(path, '\\');
    file = file ? file + 1 : path;
    wchar_t* wfile = DecodeData(CP_ACP, file, strlen(file));

    // 打开二进制文件
    FILE* fp = fopen(path, "rb");
    if (!fp) {
        return wfile;
    }

    // 获取文件大小
    fseek(fp, 0, SEEK_END);
    size_t size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (size == 0 || size > 0x100000) { // empty or more than 1MB
        fclose(fp);
        return wfile;
    }

    // 读取文件到内存中
    unsigned char* data = calloc(size + 8, 1); // 保留BOM长度+字符串尾0字符
    fread(data, size, 1, fp);
    fclose(fp);

    // 用5种编码尝试解码
    wchar_t *wdata = 0;
    if (!memcmp(data, "\xef\xbb\xbf", 3)) {
        // decode as UTF-8-BOM
        wdata = DecodeData(CP_UTF8, data + 3, size);
    } else if (!memcmp(data, "\xff\xfe", 2) && size == 2 * wcslen((wchar_t*)data)) {
        // decode as UTF-16-BOM-LE
        wdata = calloc(size, 1);
        memcpy(wdata, data + 2, size - 2);
    } else if (!memcmp(data, "\xfe\xff", 2) && size == 2 * wcslen((wchar_t*)data)) {
        // decode as UTF-16-BOM-BE
        wdata = calloc(size, 1);
        memcpy(wdata, data + 2, size - 2);
        for (size_t i = 0; wdata[i]; i++)
            wdata[i] = (wdata[i] << 8) | (wdata[i] >> 8);
    } else if (wdata = DecodeData(CP_UTF8, data, size)) {
        // decode as UTF-8
    } else if (wdata = DecodeData(CP_ACP, data, size)) {
        // decode as ANSI
    }

    // 开头添加文件名
    wchar_t *wdata2 = 0;
    if (wdata) {
        wdata2 = calloc(wcslen(wfile) + wcslen(wdata) + 3, sizeof(wchar_t));
        wcscat(wdata2, wfile);
        wcscat(wdata2, L"\n\n");
        wcscat(wdata2, wdata);

        char *file2 = filedecodeW(wdata); // Try to decode
        SelectFile(file2);
        free(file2);
        free(wdata);

    } else {
        char *data2 = fileencode2(path, data, size);
        wdata2 = DecodeData(CP_ACP, data2, -1);
        free(data2);
        *encode = 1;
    }

    free(wfile);
    free(data);
    return wdata2;
}

bool GetClipboard()
{
    int codePage = CP_UTF8; // 系统是 UTF-16, 转换可选 CP_ACP(相当于转GBK) 或 CP_UTF8(无损转换)

    // Try opening the clipboard
    if (!OpenClipboard(NULL))
        return false;

    // Get clipboard last changed
    int seq = GetClipboardSequenceNumber();
    if (seq == g_seq) {
        CloseClipboard();
        return false;
    }
    g_seq = seq;

    // 读取文件或读取文本
    wchar_t *wstr = 0;
    int encode = 0;
    char* file;
    HANDLE handle;
    if (file = GetCopiedFile()) {
        wstr = DecodeFile(file, &encode);
        free(file);
    } else if (handle = GetClipboardData(CF_UNICODETEXT)) {
        wstr = calloc(wcslen(handle) + 1, sizeof(wchar_t));
        wcscpy(wstr, handle);
    }

    // 无有效信息或文本为空
    if (!wstr || !*wstr) {
        CloseClipboard();
        free(wstr);
        return false;
    }

    // 尝试解码
    char *file2 = filedecodeW(wstr);
    SelectFile(file2);
    free(file2);

    // 清空分页
    g_length = wcslen(wstr);
    free(g_pages);

    int total = WideCharToMultiByte(codePage, 0, wstr, -1, 0, 0, NULL, NULL) - 1; // 尾部多一个 \0

    g_size  = 1 + (total - 1) / QR_PAGE_SIZE;
    g_pages = calloc(g_size, sizeof(Seg));

    int average = total / g_size; // 实际估算每页字节数
    int remain  = total % g_size; // 按每页average计算剩余字节

    // 分页保存文本
    int page = -1;
    int chLen = 0;
    int chLen2 = 0;
    int target = 0;
    wchar_t *pwstr = wstr;
    do {
        if (chLen >= target) {
            page++;
            target += average + (page < remain); // 前面remain页每页多一个字节，接近平均
            chLen2 = 0;
        }
        // 逐个宽字符累计长度
        int c = WideCharToMultiByte(codePage, 0, pwstr++, 1, &g_pages[page].str[chLen2], QR_PAGE_BUFF - chLen2, NULL, NULL);
        chLen += c;
        chLen2 += c;
    } while (*pwstr);

    // 添加页码信息
    if (encode) {
        Seg tmp;
        for (int i = 0; i < g_size; i++) {
            sprintf(tmp.str, "%d/%d:%s,", i + 1, g_size, g_pages[i].str);
            strcpy(g_pages[i].str, tmp.str);
        }
    }

    g_index = 0;

    // Release the clipboard
    CloseClipboard();
    free(wstr);

    return true;
}

void OnPaint()
{
    BitBlt(hDC, 0, 0, g_width, g_width, memDC, 0, 0, SRCCOPY);
}

void dc_MakeQr(uint8_t qr[])
{
    int x, rx, y, ry;
    int size = qrcodegen_getSize(qr);
    HBRUSH black = CreateSolidBrush(RGB(0, 0, 0));
    HBRUSH white = CreateSolidBrush(RGB(255, 255, 255));

    RECT rc = {0, 0, g_width, g_width};
    FillRect(memDC, &rc, white);

    for (y = 0, ry = 4; y < size; y++, ry += 2) {
        for (x = 0, rx = 4; x < size; x++, rx += 2) {
            RECT rectSegment = {rx, ry, rx + 2, ry + 2};

            if (qrcodegen_getModule(qr, x, y))
                FillRect(memDC, &rectSegment, black);
            else
                FillRect(memDC, &rectSegment, white);
        }
    }
}

void dc_Paint(HWND hwnd, uint8_t qr[])
{
    hDC = GetDC(hwnd);
    memDC = CreateCompatibleDC(hDC);

    HBITMAP m_hBitMap = CreateCompatibleBitmap(hDC, g_width, g_width);
    SelectObject(memDC, m_hBitMap);

    PAINTSTRUCT ps;
    BeginPaint(hwnd, &ps);

    // 绘制二维码
    dc_MakeQr(qr);

    EndPaint(hwnd, &ps);
    DeleteObject(m_hBitMap);
}

void dc_Page(HWND hwnd, int page)
{
    const char* text = page < 0 ? "https://github.com/znsoooo/qr-desktop" : g_pages[page].str;
    uint8_t qr[qrcodegen_BUFFER_LEN_MAX];
    uint8_t buf[qrcodegen_BUFFER_LEN_MAX];
    bool ok = qrcodegen_encodeText(text, buf, qr, qrcodegen_Ecc_MEDIUM, qrcodegen_VERSION_MIN, qrcodegen_VERSION_MAX, qrcodegen_Mask_3, true);  // Force mask 3
    g_width = qrcodegen_getSize(qr) * 2 + 4 * 2;

    // 生成窗口标题
    wchar_t info[256];
    if (g_size == 0)
        wsprintf(info, L"%s", QR_TITLE);
    else if (g_size == 1)
        wsprintf(info, L"%d - %s", g_length, QR_TITLE);
    else
        wsprintf(info, L"%d (%d/%d) - %s", g_length, page + 1, g_size, QR_TITLE);
    info[255] = 0;

    dc_Paint(hwnd, qr);               // 绘制DC
    win_Sizing(hwnd);                 // 调整窗口大小
    SetWindowText(hwnd, info);        // 设置窗口标题
    SetToolTip(hwnd, text);           // 设置窗口气泡(需在<调整窗口位置>之后)
    InvalidateRect(hwnd, NULL, TRUE); // 重画窗口
}

void dc_Flip(HWND hwnd, int next)
{
    if (!g_show)
        return;

    // 按左箭头<-键查看前一页
    if (!next && 0 < g_index && g_index < g_size)
        dc_Page(hwnd, --g_index);
    // 按右箭头->键查看后一页
    if (next && -1 < g_index && g_index + 1 < g_size) // g_size - 1 可能向下越界
        dc_Page(hwnd, ++g_index);
}

BOOL hook_Set()
{
    if (g_hInstance && g_hook)      // Already hooked!
        return TRUE;

    g_hook = SetWindowsHookEx(WH_KEYBOARD_LL, (HOOKPROC)KeyboardProc, g_hInstance, 0);
    if (!g_hook) {
        OutputDebugStringA("set keyboard hook failed.");
        return FALSE;
    }

    return TRUE;                                // Hook has been created correctly
}

BOOL hook_Unset()
{
    if (g_hook) {                               // Check if hook handler is valid
        UnhookWindowsHookEx(g_hook);            // Unhook is done here
        g_hook = NULL;                          // Remove hook handler to avoid to use it again
    }

    return TRUE;                                // Hook has been removed
}

void tray_Create(HWND hwnd, NOTIFYICONDATA *nid)
{
#if QR_ICON
    nid->cbSize = (DWORD)sizeof(NOTIFYICONDATA);
    nid->hWnd = hwnd;
    nid->uID = 1;
    nid->uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE;
    nid->uCallbackMessage = WM_ON_TRAY; // 自定义的消息 处理托盘图标事件
    nid->hIcon = LoadIcon(g_hInstance, MAKEINTRESOURCE(101));

    g_menu = CreatePopupMenu();         // 生成托盘菜单
    AppendMenu(g_menu, MF_STRING, ID_EXIT, L"Exit");

    wcscpy(nid->szTip, QR_VERSION);     // 鼠标放在托盘图标上时显示的文字
    Shell_NotifyIcon(NIM_ADD, nid);     // 在托盘区添加图标
#endif
}

void tray_Delete(NOTIFYICONDATA *nid)
{
#if QR_ICON
    Shell_NotifyIcon(NIM_DELETE, nid);  // 在托盘中删除图标
#endif
}

HWND win_Create(PCWSTR lpWindowName, DWORD dwStyle, DWORD dwExStyle)
{
    // Make parent window.

    WNDCLASS wc_p = {0};

    wc_p.lpfnWndProc   = DefWindowProc;
    wc_p.hInstance     = GetModuleHandle(NULL);
    wc_p.lpszClassName = L"QR Parent Class";

    RegisterClass(&wc_p);

    HWND p_hwnd = CreateWindowEx(
        dwExStyle, wc_p.lpszClassName, L"QR PARENT", dwStyle,
        100, 100, 300, 300, 0, 0, NULL, 0
    );

    // Make child window. (No icon in status bar)

    WNDCLASS wc = {0};

    wc.lpfnWndProc   = WindowProc;
    wc.hInstance     = GetModuleHandle(NULL);
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = L"QR Code Class";

    RegisterClass(&wc);

    HWND hwnd = CreateWindowEx(
        dwExStyle, wc.lpszClassName, lpWindowName, dwStyle,
        100, 100, 300, 300, p_hwnd, 0, NULL, 0
    );

    return hwnd;
}

void win_Switch(HWND hwnd)
{
    if(GetClipboard()) {
        dc_Page(hwnd, 0);
        g_show = 1;
        ShowWindow(hwnd, g_show);
    } else {
        // 切换显示窗口
        g_show = !g_show;
        ShowWindow(hwnd, g_show);
    }
}

void win_Sizing(HWND hwnd)
{
    // 获取当前窗口大小
    RECT rw; GetWindowRect(hwnd, &rw);

    // 计算更新窗口大小
    RECT r = {0, 0, g_width, g_width};
    AdjustWindowRect(&r, GetWindowLong(hwnd, GWL_STYLE), FALSE);

    // 居中放大窗口
    int x = ((rw.right + rw.left) - (r.right - r.left)) / 2;
    int y = ((rw.bottom + rw.top) - (r.bottom - r.top)) / 2;
    SetWindowPos(hwnd, 0,
        x < 0 ? 0 : x,
        y < 0 ? 0 : y,
        r.right - r.left,
        r.bottom - r.top,
        SWP_NOZORDER | SWP_NOACTIVATE); // 不捕获窗口热点
}

LRESULT CALLBACK KeyboardProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    KBDLLHOOKSTRUCT* pkh = (KBDLLHOOKSTRUCT*)lParam;

    // HC_ACTION: wParam 和 lParam 参数包含了键盘按键消息
    if (nCode == HC_ACTION && wParam != WM_KEYUP) // CTRL: WM_KEYDOWN/WM_KEYUP, ALT: WM_SYSKEYDOWN/WM_KEYUP
    {
        switch (pkh->vkCode)
        {
            case VK_LEFT:
            case VK_UP:
            case VK_PRIOR:
            case VK_LCONTROL:
                SendMessage(g_hwnd, WM_QR_CODE, 0, 0);
                break;

            case VK_RIGHT:
            case VK_DOWN:
            case VK_NEXT:
            case VK_LMENU:
                SendMessage(g_hwnd, WM_QR_CODE, 1, 0);
                break;

            case VK_ESCAPE:
                SendMessage(g_hwnd, WM_CLOSE, 0, 0);
                break;

            case 'Q':
                #define PRESSED(key) (GetAsyncKeyState(key)&0x8000)
                if (PRESSED(VK_CONTROL) && PRESSED(VK_MENU))
                    if (!PRESSED(VK_SHIFT))
                        SendMessage(g_hwnd, WM_HOTKEY, 0, 0);  // Ctrl-Alt-Q -> Switch
                    else
                        SendMessage(g_hwnd, WM_DESTROY, 0, 0); // Ctrl-Alt-Shift-Q -> Exit
        }
    }

    // Call next hook in chain
    return CallNextHookEx(g_hook, nCode, wParam, lParam);
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_CREATE:
        if(GetClipboard())
            dc_Page(hwnd, 0);
        else
            dc_Page(hwnd, -1);
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    case WM_PAINT:
        OnPaint();
        break; // 后续绘制ToolTip

    case WM_NOTIFY: {
        LPNMHDR hdr = (LPNMHDR)lParam;
        if (hdr->hwndFrom == hwndTT) {
            static RECT rc;
            if (hdr->code == TTN_SHOW)
                GetWindowRect(hwndTT, &rc);
            if (hdr->code == NM_CUSTOMDRAW) {
                // 右移1个像素避免鼠标经过导致气泡消失
                SetWindowPos(hwndTT, NULL, rc.left + 1, rc.top, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
                // 超长连续英文字符强制换行
                LPNMTTCUSTOMDRAW nm = (LPNMTTCUSTOMDRAW)lParam;
                nm->nmcd.rc.right = nm->nmcd.rc.left + SendMessage(hwndTT, TTM_GETMAXTIPWIDTH, 0, 0);
                nm->uDrawFlags |= DT_EDITCONTROL;
            }
        }
        return 0;
    }

    case WM_ON_TRAY:
        if (lParam == WM_LBUTTONDBLCLK)
            win_Switch(hwnd);
        if (lParam == WM_RBUTTONDOWN) {
            POINT pt;
            GetCursorPos(&pt);         // 获取鼠标坐标
            SetForegroundWindow(hwnd); // 解决在菜单外单击左键菜单不消失的问题
            int cmd = TrackPopupMenu(g_menu, TPM_RETURNCMD, pt.x, pt.y, 0, hwnd, 0);
            if (cmd == ID_EXIT)
                PostMessage(hwnd, WM_DESTROY, 0, 0);
        }
        return 0;

    case WM_CLOSE:
        g_show = 0;
        ShowWindow(hwnd, g_show);
        return 0;

    case WM_LBUTTONUP:
        dc_Flip(hwnd, 1);
        return 0;

    case WM_RBUTTONUP:
        dc_Flip(hwnd, 0);
        return 0;

    case WM_MBUTTONUP:
        if(GetClipboard())
            dc_Page(hwnd, 0);
        return 0;

    case WM_QR_CODE:
        dc_Flip(hwnd, wParam);
        return 0;

    case WM_HOTKEY:
        win_Switch(hwnd);
        return 0;

    case WM_GETMINMAXINFO: {
        MINMAXINFO* mmi;
        mmi = (MINMAXINFO*)lParam;
        mmi->ptMinTrackSize.x = 10; // 覆盖默认最小尺寸限制
        return 0;
    }
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR pCmdLine, int nCmdShow)
{
    // OpenConsole(); // For debug

    g_hInstance = hInstance;
    if (strcmp(pCmdLine, "hide") == 0)
        g_show = 0;

    HWND hwnd = win_Create(QR_TITLE, WS_CAPTION | WS_SYSMENU, WS_EX_DLGMODALFRAME); // WS_CAPTION | WS_POPUP WS_OVERLAPPED | WS_THICKFRAME | WS_SYSMENU | WS_EX_TOOLWINDOW
    if (!hwnd)
        return 0;

    g_hwnd = hwnd;
    SetWindowPos(hwnd, HWND_TOPMOST, 200, 200, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    ShowWindow(hwnd, g_show);

    NOTIFYICONDATA nid;
    tray_Create(hwnd, &nid);
    hook_Set();
    SetAutoRun();

    // Run the message loop.
    MSG msg = { };
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    hook_Unset();
    tray_Delete(&nid);
    return 0;
}
