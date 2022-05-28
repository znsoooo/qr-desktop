#include <windows.h>
#include <winuser.h>
#include <vector>
#include "qrcodegen.h"

#define NOTIFICATION_TRAY_ICON_MSG (WM_USER + 0x100)
#define WM_QR_CODE (WM_USER + 0x110)
#define ID_EXIT     40001

LRESULT HandleMessage(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

static LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    return HandleMessage(hwnd, uMsg, wParam, lParam);
}

HWND Create(
    PCWSTR lpWindowName,
    DWORD dwStyle,
    DWORD dwExStyle = 0,
    int x = CW_USEDEFAULT,
    int y = CW_USEDEFAULT,
    int nWidth = CW_USEDEFAULT,
    int nHeight = CW_USEDEFAULT,
    HWND hWndParent = 0,
    HMENU hMenu = 0
    )
{
    PCWSTR lpClassName = L"QR Code Class";

    // Make parent window.

    WNDCLASS wc_p = {0};

    wc_p.lpfnWndProc   = DefWindowProc;
    wc_p.hInstance     = GetModuleHandle(NULL);
    wc_p.lpszClassName = L"QR Parent Class";

    RegisterClass(&wc_p);

    HWND p_hwnd = CreateWindowEx(
        dwExStyle, L"QR Parent Class", L"QR PARENT", dwStyle, 100, 100,
        300, 200, hWndParent, hMenu, NULL, 0
    );

    // Make child window. (No icon in status bar)

    WNDCLASS wc = {0};

    wc.lpfnWndProc   = WindowProc;
    wc.hInstance     = GetModuleHandle(NULL);
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = lpClassName;

    RegisterClass(&wc);

    RECT rc = { 0, 0, 192, 192 };
    AdjustWindowRect(&rc, dwStyle, FALSE);

    HWND hwnd = CreateWindowEx(
        dwExStyle, lpClassName, lpWindowName, dwStyle, x, y,
        rc.right-rc.left,rc.bottom-rc.top, p_hwnd, hMenu, GetModuleHandle(NULL), 0
    );

    return hwnd;
}


void WriteLog(const char* format, ...)
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


using std::vector;
using namespace std;
using namespace qrcodegen;

#define    QR_VERSION     L"v0.1.6"
#define    QR_TITLE       L"QR Desktop"
#define    QR_ICON        1
const int  QR_PAGE_SIZE = 2000; // 1个汉字占3个字节


HINSTANCE  g_hInstance = (HINSTANCE)::GetModuleHandle(NULL);
HWND       g_hwnd;
NOTIFYICONDATA nid;
HMENU      hTrayMenu;

HHOOK      g_Hook;           // Handler of hook
bool       g_show = QR_ICON; // 界面显示状态 默认状态可是否显示图标一致


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

/***********  键盘钩子消息处理 *********************/

#define PRESSED(key) (GetAsyncKeyState(key)&0x8000)

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
                if (PRESSED(VK_CONTROL) && PRESSED(VK_MENU))
                    if (!PRESSED(VK_SHIFT))
                        SendMessage(g_hwnd, WM_HOTKEY, 0, 0);  // Ctrl-Alt-Q -> Switch
                    else
                        SendMessage(g_hwnd, WM_DESTROY, 0, 0); // Ctrl-Alt-Shift-Q -> Exit
        }
    }

    // Call next hook in chain
    return ::CallNextHookEx(g_Hook, nCode, wParam, lParam);
}

//设置键盘HOOK
BOOL SetHook()
{
    if (g_hInstance && g_Hook)      // Already hooked!
        return TRUE;

    g_Hook = SetWindowsHookEx(WH_KEYBOARD_LL, (HOOKPROC)KeyboardProc, g_hInstance, 0);
    if (!g_Hook)
    {
        OutputDebugStringA("set keyboard hook failed.");
        return FALSE;
    }

    return TRUE;                                // Hook has been created correctly
}

//取消键盘HOOK
BOOL UnSetHook()
{
    if (g_Hook) {                               // Check if hook handler is valid
        ::UnhookWindowsHookEx(g_Hook);          // Unhook is done here
        g_Hook = NULL;                          // Remove hook handler to avoid to use it again
    }

    return TRUE;                                // Hook has been removed
}
/*************************************************/

HBRUSH  hBrushBlack = CreateSolidBrush(RGB(0, 0, 0));
HBRUSH  hBrushWhite = CreateSolidBrush(RGB(255, 255, 255));

//剪切板
std::string  clipboardText;
vector<string> txtPages;
int     pageIndex = -1;
int     txtLen = 0;

bool    GetClipboardTextW(int codePage);

//设置double buffering
HDC hDC;
HDC memDC;
int widthDC;

void PaintDC(HWND hwnd);
void OnPaint();
void SwitchWindow(HWND hwnd);
void UpdateWindowSize(HWND hwnd);

QrCode qrCode = QrCode::encodeText("https://github.com/znsoooo/qr-desktop", QrCode::Ecc::MEDIUM);


void initial(HWND hwnd) {
    widthDC = qrCode.getSize() * 2 + 4 * 2;
    PaintDC(hwnd);                    // 绘制DC
    UpdateWindowSize(hwnd);           // 调整窗口大小
    InvalidateRect(hwnd, NULL, TRUE); // 重画窗口
}

void makeQrPage(HWND hwnd, int page) {
    const char* text = txtPages[page].c_str();
    std::vector<QrSegment> segs = QrSegment::makeSegments(text);
    qrCode = QrCode::encodeSegments(segs, QrCode::Ecc::MEDIUM, QrCode::MIN_VERSION, QrCode::MAX_VERSION, 3, true);  // Force mask 3
    widthDC = qrCode.getSize() * 2 + 4 * 2;

    // 生成窗口标题
    wchar_t info[256];
    if (txtPages.size() == 1)
        wsprintf(info, L"%d - %s", txtLen, QR_TITLE);
    else
        wsprintf(info, L"%d (%d/%d) - %s", txtLen, page + 1, txtPages.size(), QR_TITLE);
    info[255] = 0;

    PaintDC(hwnd);                    // 绘制DC
    UpdateWindowSize(hwnd);           // 调整窗口大小
    SetWindowText(hwnd, info);        // 设置窗口标题
    InvalidateRect(hwnd, NULL, TRUE); // 重画窗口
}

void printQr() {
    int size = qrCode.getSize();
    RECT rc{0, 0, widthDC, widthDC};
    FillRect(memDC, &rc, hBrushWhite);

    for (int y = 0, ry = 4; y < size; y++, ry += 2) {
        for (int x = 0, rx = 4; x < size; x++, rx += 2) {
            RECT rectSegment{rx, ry, rx + 2, ry + 2};

            if (qrCode.getModule(x, y))
                FillRect(memDC, &rectSegment, hBrushBlack);
            else
                FillRect(memDC, &rectSegment, hBrushWhite);
        }
    }
}


void PaintDC(HWND hwnd)
{
    hDC = GetDC(hwnd);
    memDC = CreateCompatibleDC(hDC);

    HBITMAP m_hBitMap = CreateCompatibleBitmap(hDC, widthDC, widthDC);
    SelectObject(memDC, m_hBitMap);

    PAINTSTRUCT ps;
    BeginPaint(hwnd, &ps);

    //绘制二维码
    printQr();

    EndPaint(hwnd, &ps);
    DeleteObject(m_hBitMap);
}

void OnPaint()
{
    BitBlt(hDC, 0, 0, widthDC, widthDC, memDC, 0, 0, SRCCOPY);
}

bool GetClipboardTextW(int codePage)
{
    // Try opening the clipboard
    if (!OpenClipboard(nullptr))
        return false;

    // Get handle of clipboard object for ANSI text
    HANDLE hData = GetClipboardData(CF_UNICODETEXT);
    if (hData == nullptr)
    {
        CloseClipboard();
        return false;
    }

    // Lock the handle to get the actual text pointer
    // char * pszText = static_cast<char*>(GlobalLock(hData));

    wchar_t * pwstr = (wchar_t*)GlobalLock(hData);
    if (pwstr == nullptr || !*pwstr) // 或剪切板文本为空
    {
        GlobalUnlock(hData);
        CloseClipboard();
        return false;
    }

    // 清空分页
    txtLen = wcslen(pwstr);
    txtPages.clear();

    int total = WideCharToMultiByte(codePage, 0, pwstr, -1, 0, 0, NULL, NULL) - 1; // 尾部多一个 \0
    int pages = 1 + (total - 1) / QR_PAGE_SIZE;
    int average = total / pages; //实际估算每页字节数
    int remain  = total % pages; //按每页average计算剩余字节

    // 分页保存文本
    char segment[QR_PAGE_SIZE + 8];
    int wLen = 0;
    int chLen = 0;
    int target = average + (txtPages.size() < remain);
    do {
        //逐个宽字符累计长度
        int c = WideCharToMultiByte(codePage, 0, pwstr++, 1, 0, 0, NULL, NULL);
        chLen += c;
        wLen++;
        if (chLen >= target)
        {
            int c2 = WideCharToMultiByte(codePage, 0, pwstr - wLen, wLen, segment, QR_PAGE_SIZE + 8, NULL, NULL);
            segment[c2] = 0; // c2总是小于QR_PAGE_SIZE+8
            txtPages.push_back(segment);
            target += average + (txtPages.size() < remain); // 前面remain页每页多一个字节，接近平均
            wLen = 0;
        }
    } while (*pwstr);

    //WriteLog("\ntotal: %d", total);
    //for(int k=0;k<txtPages.size() ;k++)
    //  WriteLog("P%d = %d,", k, txtPages[k].length());

    pageIndex = 0;

    // Release the lock
    GlobalUnlock(hData);

    // Release the clipboard
    CloseClipboard();

    return true;
}

//生成托盘
void ToTray(HWND hwnd)
{
#if QR_ICON
    nid.cbSize = (DWORD)sizeof(NOTIFYICONDATA);
    nid.hWnd = hwnd;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE;
    nid.uCallbackMessage = NOTIFICATION_TRAY_ICON_MSG;//自定义的消息 处理托盘图标事件
    nid.hIcon = LoadIcon(g_hInstance, MAKEINTRESOURCE(101));

    hTrayMenu = CreatePopupMenu();//生成托盘菜单
    AppendMenu(hTrayMenu, MF_STRING, ID_EXIT, L"Exit");

    wcscpy_s(nid.szTip, QR_VERSION);//鼠标放在托盘图标上时显示的文字
    Shell_NotifyIcon(NIM_ADD, &nid);//在托盘区添加图标
#endif
}

void DeleteTray()
{
#if QR_ICON
    Shell_NotifyIcon(NIM_DELETE, &nid);//在托盘中删除图标
#endif
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR pCmdLine, int nCmdShow)
{
    if (wcscmp(pCmdLine, L"hide") == 0)
        g_show = 0;

    HWND hwnd = Create(QR_TITLE, WS_CAPTION | WS_SYSMENU, WS_EX_DLGMODALFRAME); // WS_CAPTION | WS_POPUP WS_OVERLAPPED | WS_THICKFRAME | WS_SYSMENU | WS_EX_TOOLWINDOW
    if (!hwnd)
        return 0;

    g_hwnd = hwnd;
    SetWindowPos(hwnd, HWND_TOPMOST, 200, 200, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    initial(hwnd);
    ShowWindow(hwnd, g_show);
    ToTray(hwnd);

    SetHook();
    SetAutoRun();

    // Run the message loop.
    MSG msg = { };
    while (GetMessage(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    UnSetHook();
    return 0;
}

void SwitchWindow(HWND hwnd)
{
    // 切换显示窗口
    g_show = !g_show;
    ShowWindow(hwnd, g_show ? SW_SHOW : SW_HIDE);
}

void UpdateWindowSize(HWND hwnd)
{
    // 获取当前窗口大小
    RECT rw; GetWindowRect(hwnd, &rw);

    // 计算更新窗口大小
    RECT r{0, 0, widthDC, widthDC};
    AdjustWindowRect(&r, GetWindowLong(hwnd, GWL_STYLE), FALSE);

    // 居中放大窗口
    SetWindowPos(hwnd, 0,
        ((rw.right + rw.left) - (r.right - r.left)) / 2,
        ((rw.bottom + rw.top) - (r.bottom - r.top)) / 2,
        r.right - r.left,
        r.bottom - r.top,
        SWP_NOZORDER | SWP_NOACTIVATE); // 不捕获窗口热点
}

LRESULT HandleMessage(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    static HWND hwndNextViewer;
    int width;

    switch (uMsg)
    {
    case WM_CREATE:
        // Add the window to the clipboard viewer chain.
        hwndNextViewer = SetClipboardViewer(hwnd);
        return 0;

    case WM_CHANGECBCHAIN:
        // If the next window is closing, repair the chain.
        if ((HWND)wParam == hwndNextViewer)
            hwndNextViewer = (HWND)lParam;
        // Otherwise, pass the message to the next link.
        else if (hwndNextViewer != NULL)
            SendMessage(hwndNextViewer, uMsg, wParam, lParam);
        break;

    case WM_DESTROY:
        DeleteTray();
        ChangeClipboardChain(hwnd, hwndNextViewer);
        PostQuitMessage(0);
        return 0;

    case WM_DRAWCLIPBOARD:  // clipboard contents changed.
        //系统是UTF-16，转换可选CP_ACP（相当于转GBK） 或 CP_UTF8（无损转换)
        if(GetClipboardTextW(CP_UTF8))
            makeQrPage(hwnd, 0);

        SendMessage(hwndNextViewer, uMsg, wParam, lParam);
        break;

    case WM_PAINT:
        OnPaint();
        return 0;

    case NOTIFICATION_TRAY_ICON_MSG:
        // This is a message that originated with the
        // Notification Tray Icon. The lParam tells use exactly which event
        // it is.
        switch (lParam)
        {
            case WM_LBUTTONDBLCLK:
            {
                SwitchWindow(hwnd);
                break;
            }
            case WM_RBUTTONDOWN:
            {
                //获取鼠标坐标
                POINT pt; GetCursorPos(&pt);

                //解决在菜单外单击左键菜单不消失的问题
                SetForegroundWindow(hwnd);

                //使菜单某项变灰
                //EnableMenuItem(hMenu, ID_SHOW, MF_GRAYED);

                //显示并获取选中的菜单
                int cmd = TrackPopupMenu(hTrayMenu, TPM_RETURNCMD, pt.x, pt.y, 0, hwnd, 0);
                if (cmd == ID_EXIT)
                    PostMessage(hwnd, WM_DESTROY, 0, 0);
            }
        }
        break;

    case WM_CLOSE:
        ToTray(hwnd);
        ShowWindow(hwnd, SW_HIDE);
        g_show = 0;
        return 0;

    case WM_QR_CODE:
        if (!g_show)
            return 0;
        // 按左箭头<-键查看前一页
        if (!wParam && 0 < pageIndex && pageIndex < txtPages.size())
            makeQrPage(hwnd, --pageIndex);
        // 按右箭头->键查看后一页
        if (wParam && -1 < pageIndex && pageIndex + 1 < txtPages.size()) // txtPages.size() - 1 可能向下越界
            makeQrPage(hwnd, ++pageIndex);
        return 0;

    case WM_HOTKEY:
        SwitchWindow(hwnd);
        break;

    case WM_GETMINMAXINFO:
        MINMAXINFO* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
        mmi->ptMinTrackSize.x = 10; // 覆盖默认最小尺寸限制
        return 0;
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}
