#include <windows.h>
#include <winuser.h>
#include <vector>
#include <string>
#include "qrcodegen.h"

using std::vector;
using namespace std;

#define    QR_VERSION     L"v0.1.7"
#define    QR_TITLE       L"QR Desktop"
#define    QR_ICON        1
const int  QR_PAGE_SIZE = 2000; // 1个汉字占3个字节


HINSTANCE  g_hInstance = (HINSTANCE)GetModuleHandle(NULL);
HWND       g_hwnd;
HMENU      g_menu;
HHOOK      g_hook;           // Handler of hook
bool       g_show = QR_ICON; // 界面显示状态 默认状态可是否显示图标一致


#define WM_ON_TRAY (WM_USER + 0x100)
#define WM_QR_CODE (WM_USER + 0x110)
#define ID_EXIT     40001

LRESULT CALLBACK KeyboardProc(int nCode, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

void win_Sizing(HWND hwnd);

//剪切板
vector<string> g_pages;
int g_seq    = 0;
int g_index  = -1;
int g_length = 0;
int g_width  = 0;

//设置double buffering
HDC hDC;
HDC memDC;

void log(const char* format, ...)
{
    char buf[1024];

    va_list p;
    va_start(p, format);
    vsprintf(buf, format, p);
    va_end(p);

    const char* path = "log.txt";
    FILE *stream;
    if ((stream = fopen(path, "a+")) == NULL)
    {
        MessageBox(0, L"Could not create/open a file", L"Error", 16);
        return ;
    }
    //fprintf(stream, "%s\n", buffer);
    //fseek(stream, 0L, SEEK_END);
    fprintf(stream, "%s\n", buf);
    fclose(stream);
}

void SetAutoRun()
{
    wchar_t mpath[256];
    HKEY hKey;

    GetModuleFileName(0, mpath, 256); // get self path
    int ret = RegOpenKey(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", &hKey);
    ret = RegSetValueEx(hKey, L"qrcode", 0, REG_SZ, (unsigned char*)wcscat(mpath, L" hide"), 256);
    if(ret == 0)
        RegCloseKey(hKey);
}

bool GetClipboard()
{
    int codePage = CP_UTF8; //系统是 UTF-16, 转换可选 CP_ACP(相当于转GBK) 或 CP_UTF8(无损转换)

    // Try opening the clipboard
    if (!OpenClipboard(NULL))
        return false;

    // Get clipboard last changed
    int seq = GetClipboardSequenceNumber();
    if (seq == g_seq)
    {
        CloseClipboard();
        return false;
    }
    g_seq = seq;

    // Get handle of clipboard object for ANSI text
    HANDLE hData = GetClipboardData(CF_UNICODETEXT);
    if (hData == NULL)
    {
        CloseClipboard();
        return false;
    }

    // Lock the handle to get the actual text pointer
    wchar_t * pwstr = (wchar_t*)GlobalLock(hData);
    if (pwstr == NULL || !*pwstr) // 或剪切板文本为空
    {
        GlobalUnlock(hData);
        CloseClipboard();
        return false;
    }

    // 清空分页
    g_length = wcslen(pwstr);
    g_pages.clear();

    int total = WideCharToMultiByte(codePage, 0, pwstr, -1, 0, 0, NULL, NULL) - 1; // 尾部多一个 \0
    int pages = 1 + (total - 1) / QR_PAGE_SIZE;
    int average = total / pages; //实际估算每页字节数
    int remain  = total % pages; //按每页average计算剩余字节

    // 分页保存文本
    char segment[QR_PAGE_SIZE + 8];
    int wLen = 0;
    int chLen = 0;
    int target = average + (g_pages.size() < remain);
    do {
        //逐个宽字符累计长度
        int c = WideCharToMultiByte(codePage, 0, pwstr++, 1, 0, 0, NULL, NULL);
        chLen += c;
        wLen++;
        if (chLen >= target)
        {
            int c2 = WideCharToMultiByte(codePage, 0, pwstr - wLen, wLen, segment, QR_PAGE_SIZE + 8, NULL, NULL);
            segment[c2] = 0; // c2总是小于QR_PAGE_SIZE+8
            g_pages.push_back(segment);
            target += average + (g_pages.size() < remain); // 前面remain页每页多一个字节，接近平均
            wLen = 0;
        }
    } while (*pwstr);

    //log("\ntotal: %d", total);
    //for(int k=0;k<g_pages.size() ;k++)
    //  log("P%d = %d,", k, g_pages[k].length());

    g_index = 0;

    // Release the lock
    GlobalUnlock(hData);

    // Release the clipboard
    CloseClipboard();

    return true;
}

void OnPaint()
{
    BitBlt(hDC, 0, 0, g_width, g_width, memDC, 0, 0, SRCCOPY);
}

void dc_MakeQr(uint8_t qr[])
{
    int size = qrcodegen_getSize(qr);
    HBRUSH black = CreateSolidBrush(RGB(0, 0, 0));
    HBRUSH white = CreateSolidBrush(RGB(255, 255, 255));

    RECT rc{0, 0, g_width, g_width};
    FillRect(memDC, &rc, white);

    for (int y = 0, ry = 4; y < size; y++, ry += 2) {
        for (int x = 0, rx = 4; x < size; x++, rx += 2) {
            RECT rectSegment{rx, ry, rx + 2, ry + 2};

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

    //绘制二维码
    dc_MakeQr(qr);

    EndPaint(hwnd, &ps);
    DeleteObject(m_hBitMap);
}

void dc_Page(HWND hwnd, int page)
{
    const char* text = page < 0 ? "https://github.com/znsoooo/qr-desktop" : g_pages[page].c_str();
    uint8_t qr[qrcodegen_BUFFER_LEN_MAX];
    uint8_t buf[qrcodegen_BUFFER_LEN_MAX];
    bool ok = qrcodegen_encodeText(text, buf, qr, qrcodegen_Ecc_MEDIUM, qrcodegen_VERSION_MIN, qrcodegen_VERSION_MAX, qrcodegen_Mask_3, true);  // Force mask 3
    g_width = qrcodegen_getSize(qr) * 2 + 4 * 2;

    // 生成窗口标题
    wchar_t info[256];
    if (g_pages.size() == 0)
        wsprintf(info, L"%s", QR_TITLE);
    else if (g_pages.size() == 1)
        wsprintf(info, L"%d - %s", g_length, QR_TITLE);
    else
        wsprintf(info, L"%d (%d/%d) - %s", g_length, page + 1, g_pages.size(), QR_TITLE);
    info[255] = 0;

    dc_Paint(hwnd, qr);               // 绘制DC
    win_Sizing(hwnd);                 // 调整窗口大小
    SetWindowText(hwnd, info);        // 设置窗口标题
    InvalidateRect(hwnd, NULL, TRUE); // 重画窗口
}

BOOL hook_Set()
{
    if (g_hInstance && g_hook)      // Already hooked!
        return TRUE;

    g_hook = SetWindowsHookEx(WH_KEYBOARD_LL, (HOOKPROC)KeyboardProc, g_hInstance, 0);
    if (!g_hook)
    {
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
    nid->uCallbackMessage = WM_ON_TRAY;//自定义的消息 处理托盘图标事件
    nid->hIcon = LoadIcon(g_hInstance, MAKEINTRESOURCE(101));

    g_menu = CreatePopupMenu();//生成托盘菜单
    AppendMenu(g_menu, MF_STRING, ID_EXIT, L"Exit");

    wcscpy_s(nid->szTip, QR_VERSION);//鼠标放在托盘图标上时显示的文字
    Shell_NotifyIcon(NIM_ADD, nid);//在托盘区添加图标
#endif
}

void tray_Delete(NOTIFYICONDATA *nid)
{
#if QR_ICON
    Shell_NotifyIcon(NIM_DELETE, nid);//在托盘中删除图标
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
        dwExStyle, wc_p.lpszClassName, L"QR PARENT", dwStyle, 100, 100,
        300, 200, 0, 0, NULL, 0
    );

    // Make child window. (No icon in status bar)

    WNDCLASS wc = {0};

    wc.lpfnWndProc   = WindowProc;
    wc.hInstance     = GetModuleHandle(NULL);
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = L"QR Code Class";

    RegisterClass(&wc);

    RECT rc = { 0, 0, 192, 192 };
    AdjustWindowRect(&rc, dwStyle, FALSE);

    HWND hwnd = CreateWindowEx(
        dwExStyle, wc.lpszClassName, lpWindowName, dwStyle, CW_USEDEFAULT, CW_USEDEFAULT,
        rc.right-rc.left, rc.bottom-rc.top, p_hwnd, 0, GetModuleHandle(NULL), 0
    );

    return hwnd;
}

void win_Switch(HWND hwnd)
{
    // 切换显示窗口
    g_show = !g_show;
    ShowWindow(hwnd, g_show);
}

void win_Sizing(HWND hwnd)
{
    // 获取当前窗口大小
    RECT rw; GetWindowRect(hwnd, &rw);

    // 计算更新窗口大小
    RECT r{0, 0, g_width, g_width};
    AdjustWindowRect(&r, GetWindowLong(hwnd, GWL_STYLE), FALSE);

    // 居中放大窗口
    SetWindowPos(hwnd, 0,
        ((rw.right + rw.left) - (r.right - r.left)) / 2,
        ((rw.bottom + rw.top) - (r.bottom - r.top)) / 2,
        r.right - r.left,
        r.bottom - r.top,
        SWP_NOZORDER | SWP_NOACTIVATE); // 不捕获窗口热点
}

LRESULT CALLBACK KeyboardProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    KBDLLHOOKSTRUCT* pkh = (KBDLLHOOKSTRUCT*)lParam;

    //HC_ACTION: wParam 和lParam参数包含了键盘按键消息
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
        return 0;

    case WM_ON_TRAY:
        // This is a message that originated with the
        // Notification Tray Icon. The lParam tells use exactly which event
        // it is.
        switch (lParam)
        {
            case WM_LBUTTONDBLCLK:
                win_Switch(hwnd);
                break;

            case WM_RBUTTONDOWN:
                //获取鼠标坐标
                POINT pt; GetCursorPos(&pt);

                //解决在菜单外单击左键菜单不消失的问题
                SetForegroundWindow(hwnd);

                //使菜单某项变灰
                //EnableMenuItem(hMenu, ID_SHOW, MF_GRAYED);

                //显示并获取选中的菜单
                int cmd = TrackPopupMenu(g_menu, TPM_RETURNCMD, pt.x, pt.y, 0, hwnd, 0);
                if (cmd == ID_EXIT)
                    PostMessage(hwnd, WM_DESTROY, 0, 0);
        }
        return 0;

    case WM_CLOSE:
        g_show = 0;
        ShowWindow(hwnd, g_show);
        return 0;

    case WM_QR_CODE:
        if (!g_show)
            return 0;

        // 按左箭头<-键查看前一页
        if (!wParam && 0 < g_index && g_index < g_pages.size())
            dc_Page(hwnd, --g_index);
        // 按右箭头->键查看后一页
        if (wParam && -1 < g_index && g_index + 1 < g_pages.size()) // g_pages.size() - 1 可能向下越界
            dc_Page(hwnd, ++g_index);
        return 0;

    case WM_HOTKEY:
        if(GetClipboard())
        {
            dc_Page(hwnd, 0);
            g_show = 1;
            ShowWindow(hwnd, g_show);
        }
        else
            win_Switch(hwnd);
        return 0;

    case WM_GETMINMAXINFO:
        MINMAXINFO* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
        mmi->ptMinTrackSize.x = 10; // 覆盖默认最小尺寸限制
        return 0;
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR pCmdLine, int nCmdShow)
{
    if (wcscmp(pCmdLine, L"hide") == 0)
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
    while (GetMessage(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    hook_Unset();
    tray_Delete(&nid);
    return 0;
}
