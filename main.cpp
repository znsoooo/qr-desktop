#include <windows.h>
#include <winuser.h>
#include <vector>
#include "qrcodegen.h"
#include "basewin.h"
using std::vector;
using std::tuple;
using namespace std;
using namespace qrcodegen;

#define    QR_TITLE       L"QR Desktop 0.1.1"
const int  QR_PAGE_SIZE = 600; //300 字节为一页


HINSTANCE  g_hInstance;
HWND       g_hWnd;
UINT       uFormat = (UINT)(-1);
BOOL       fAuto = TRUE;
NOTIFYICONDATA nid;
HMENU      hTrayMenu;

HHOOK      g_Hook;         // Handler of hook
bool       g_show = 0;     // 界面显示状态


/***********  键盘钩子消息处理 *********************/
LRESULT CALLBACK KeyboardProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    KBDLLHOOKSTRUCT* pkh = (KBDLLHOOKSTRUCT*)lParam;

    //HC_ACTION: wParam 和lParam参数包含了键盘按键消息
    if (nCode == HC_ACTION)
    {
        //判断函数调用时指定虚拟键的状态
        //BOOL bCtrlKey = ::GetAsyncKeyState(VK_CONTROL) & 0x8000;
        //BOOL bCtrlKey = ::GetAsyncKeyState(VK_CONTROL) >> ((sizeof(SHORT) * 8) - 1);

        //是否按下CTRL 、ALT、SHIFT
        BOOL bFuncKey = (::GetAsyncKeyState(VK_CONTROL) >> ((sizeof(SHORT) * 8) - 1))
            || (pkh->flags & LLKHF_ALTDOWN)
            || (::GetAsyncKeyState(VK_SHIFT) & 0x8000);

        //KEYUP 或 KEYDOWN都触发HC_ACTION，取其中一个处理
        if (!bFuncKey && VK_LEFT == pkh->vkCode && wParam == WM_KEYUP)
            SendMessage(g_hWnd, WM_QR_CODE, 0, 0);

        else if (!bFuncKey && VK_RIGHT == pkh->vkCode  && wParam == WM_KEYUP)
            SendMessage(g_hWnd, WM_QR_CODE, 1, 0);

        /*else if ((::GetAsyncKeyState(VK_CONTROL) >> ((sizeof(SHORT) * 8) - 1))
            && (pkh->flags & LLKHF_ALTDOWN) && ::GetAsyncKeyState(0x51))
            ShowWindow(g_hWnd, SW_SHOW);*/
    }

    // Call next hook in chain
    return ::CallNextHookEx(g_Hook, nCode, wParam, lParam);
}

//设置键盘HOOK
BOOL SetHook()
{
    if (g_hInstance && g_Hook)      // Already hooked!
        return TRUE;

    g_hInstance = (HINSTANCE)::GetModuleHandle(NULL);
    g_Hook = ::SetWindowsHookEx(WH_KEYBOARD_LL, (HOOKPROC)KeyboardProc, g_hInstance, 0);
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

class MainWindow : public BaseWindow<MainWindow>
{
    HBRUSH  hBrushBlack = CreateSolidBrush(RGB(0, 0, 0));
    HBRUSH  hBrushWhite = CreateSolidBrush(RGB(255, 255, 255));

    void    CalculateLayout() { };
    void    OnPaint();
    void    Resize();
    void    OnLButtonDown(int pixelX, int pixelY, DWORD flags);
    void    OnLButtonUp();
    void    OnMouseMove(int pixelX, int pixelY, DWORD flags);

    //剪切板
    std::string  clipboardText;
    vector<string> txtPages;
    int     pageIndex = 0;
    int     txtLen = 0;

    void    GetClipboardTextW(int codePage);
    void    UpdateWindowSize();

    //切换显示状态
    void    SwitchWindow();

    QrCode qrCode = QrCode::encodeText("Hello!", QrCode::Ecc::MEDIUM);

public:

    MainWindow() {}

    PCWSTR  ClassName() const { return L"QR Code Class"; }
    LRESULT HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam);

    void  makeQrPage(int page) {
        const char* text = txtPages[page].c_str();
        qrCode = QrCode::encodeText(text, QrCode::Ecc::MEDIUM);
        UpdateWindowSize();

        // 生成窗口标题
        wchar_t info[256];
        if (txtPages.size() == 1)
            wsprintf(info, L"%d - %s", txtLen, QR_TITLE);
        else
            wsprintf(info, L"%d (%d/%d) - %s", txtLen, page + 1, txtPages.size(), QR_TITLE);
        info[255] = 0;

        // 设置窗口标题
        SetWindowText(m_hwnd, info);
    }

    void printQr(const QrCode &qr, HDC hdc, HDC hMemDC) {

        int border = 4;

        RECT rc;
        GetClientRect(m_hwnd, &rc);
        FillRect(hMemDC, &rc, hBrushWhite);

        int size = qr.getSize();
        int unitX = 2;
        int unitY = 2;

        //char info[256];
        //sprintf(info, "rc=(%d,%d,%d,%d),qr.size=%d,unitX=%d,unitY=%d\n",rc.left,rc.top,rc.right,rc.bottom,qr.getSize(),unitX,unitY);
        //OutputDebugStringA(info);

        for (int y = 0; y < size; y++) {
            for (int x = 0; x < size; x++) {
                int rx = x * unitX + (rc.right  - unitX * size) / 2;
                int ry = y * unitY + (rc.bottom - unitY * size) / 2;

                RECT rectSegment{rx, ry, rx + unitX, ry + unitY};

                if (qr.getModule(x, y))
                    FillRect(hMemDC, &rectSegment, hBrushBlack);
                else
                    FillRect(hMemDC, &rectSegment, hBrushWhite);
            }
        }
        BitBlt(hdc, 0, 0, rc.right, rc.bottom, hMemDC, 0, 0, SRCCOPY);
    }

    void WINAPI SetAutoView(HWND hwnd)
    {
        static UINT auPriorityList[] = {
            CF_TEXT
        };

        uFormat = GetPriorityClipboardFormat(auPriorityList, 1);
        fAuto = TRUE;

        InvalidateRect(hwnd, NULL, TRUE);
        UpdateWindow(hwnd);
    }

};


void MainWindow::OnPaint()
{
    HDC hDC = GetDC(m_hwnd);
    HDC memDC = CreateCompatibleDC(hDC);
    //设置double buffering
    RECT rc;
    GetClientRect(m_hwnd, &rc);
    HBITMAP m_hBitMap = CreateCompatibleBitmap(hDC, rc.right - rc.left, rc.bottom - rc.top);
    SelectObject(memDC, m_hBitMap);

    PAINTSTRUCT ps;
    BeginPaint(m_hwnd, &ps);

    //绘制二维码
    printQr(qrCode, hDC, memDC);

    EndPaint(m_hwnd, &ps);
    DeleteObject(m_hBitMap);
    DeleteDC(memDC);
    DeleteDC(hDC);
}

void MainWindow::Resize()
{
    InvalidateRect(m_hwnd, NULL, FALSE);
}

void MainWindow::GetClipboardTextW(int codePage)
{
    //清空分页
    txtPages.clear();
    pageIndex = -1;
    // Try opening the clipboard
    if (!OpenClipboard(nullptr))
        return;

    // Get handle of clipboard object for ANSI text
    HANDLE hData = GetClipboardData(CF_UNICODETEXT);
    if (hData == nullptr)
    {
        CloseClipboard();
        return;
    }

    // Lock the handle to get the actual text pointer
    // char * pszText = static_cast<char*>(GlobalLock(hData));

    wchar_t * pwstr = (wchar_t*)GlobalLock(hData);
    if (pwstr == nullptr)
    {
        GlobalUnlock(hData);
        CloseClipboard();
        return;
    }
    txtLen = wcslen(pwstr);

    int total = WideCharToMultiByte(codePage, 0, pwstr, -1, 0, 0, NULL, NULL);
    int pages = 1 + (total - 1) / QR_PAGE_SIZE;
    int average = total / pages; //实际估算每页字节数
    int remain  = total % pages; //按每页average计算剩余字节
    //WriteLog("totalBytes=%d,pagecount=%d,avgPageSize=%d,remain=%d", totalBytes, a + b, avgPageSize, remain);
    // 分页保存文本
    int wLen = 0;
    int chLen = 0;
    do {
        //逐个宽字符累计长度
        int c = WideCharToMultiByte(codePage, 0, pwstr++, 1, 0, 0, NULL, NULL);
        total -= c;
        chLen += c;
        wLen++;
        //前面remain页每页多一个字节，接近平均
        if (chLen >= average + (txtPages.size() < remain) || total == 0)
        {
            char segment[chLen + 1];
            ::WideCharToMultiByte(codePage, 0, pwstr - wLen, -1, segment, chLen, NULL, NULL);
            segment[chLen] = 0;
            txtPages.push_back(segment);
            chLen = 0;
            wLen = 0;
        }
    } while (total > 0);

    //for(int k=0;k<txtPages.size() ;k++)
    //  WriteLog("P%d = %d,",k,txtPages[k].length());

    pageIndex = 0;

    // Release the lock
    GlobalUnlock(hData);

    // Release the clipboard
    CloseClipboard();
}

//生成托盘
void ToTray(HWND hWnd)
{
    nid.cbSize = (DWORD)sizeof(NOTIFYICONDATA);
    nid.hWnd = hWnd;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_MESSAGE ;
    nid.uCallbackMessage = NOTIFICATION_TRAY_ICON_MSG;//自定义的消息 处理托盘图标事件
    nid.hIcon = static_cast<HICON>(LoadImage(NULL,
        TEXT("icon1.ico"),
        //MAKEINTRESOURCE(IDI_ICON1),
        IMAGE_ICON,
        0, 0,
        LR_DEFAULTCOLOR | LR_SHARED | LR_DEFAULTSIZE | LR_LOADFROMFILE));
        //LR_DEFAULTSIZE));

    hTrayMenu = CreatePopupMenu();//生成托盘菜单
    AppendMenu(hTrayMenu, MF_STRING, ID_EXIT, TEXT("Exit"));

    //wcscpy_s(nid.szTip, "QR Desktop");//鼠标放在托盘图标上时显示的文字
    Shell_NotifyIcon(NIM_ADD, &nid);//在托盘区添加图标
}

void DeleteTray(HWND hWnd)
{
    Shell_NotifyIcon(NIM_DELETE, &nid);//在托盘中删除图标
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow)
{
    MainWindow win;

    if (!win.Create(QR_TITLE, WS_OVERLAPPED | WS_SYSMENU))// WS_CAPTION | WS_POPUP WS_OVERLAPPED | WS_THICKFRAME | WS_SYSMENU
    {
        return 0;
    }

    g_hWnd = win.Window();
    SetWindowPos(win.Window(), HWND_TOPMOST, 200, 200, 0, 0, SWP_NOMOVE | SWP_NOSIZE);

    HICON hIcon = static_cast<HICON>(LoadImage(NULL,
        TEXT("icon1.ico"),
        //MAKEINTRESOURCE(IDI_ICON1),
        IMAGE_ICON,
        0, 0,
        LR_DEFAULTCOLOR | LR_SHARED | LR_DEFAULTSIZE | LR_LOADFROMFILE));
        //LR_DEFAULTSIZE));

    SendMessage(win.Window(), WM_SETICON, ICON_BIG, (LPARAM)hIcon);
    //SendMessage(win.Window(), WM_SETICON, ICON_SMALL, (LPARAM)hIcon);

    if (!RegisterHotKey(win.Window(), 1, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 'Q'))
        MessageBox(win.Window(), L"regist hotkey failed.", L"Warning", MB_OK);

    if (!SetHook())
        ;

    ShowWindow(win.Window(), nCmdShow);
    g_show = 1;
    ToTray(g_hWnd);

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

void MainWindow::SwitchWindow()
{
    // 切换显示窗口
    g_show = !g_show;
    ShowWindow(m_hwnd, g_show ? SW_SHOW : SW_HIDE);
}

void MainWindow::UpdateWindowSize()
{
    // 获取当前窗口大小
    RECT rw; GetWindowRect(m_hwnd, &rw);

    // 计算更新窗口大小
    int width = qrCode.getSize() * 2 + 4 * 2;
    RECT r{0, 0, width, width};
    AdjustWindowRect(&r, GetWindowLong(m_hwnd, GWL_STYLE), FALSE);

    // 居中放大窗口
    SetWindowPos(m_hwnd, 0,
        ((rw.right + rw.left) - (r.right - r.left)) / 2,
        ((rw.bottom + rw.top) - (r.bottom - r.top)) / 2,
        r.right - r.left,
        r.bottom - r.top,
        SWP_NOZORDER | SWP_NOACTIVATE); // 不捕获窗口热点
}

LRESULT MainWindow::HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    static HWND hwndNextViewer;
    int width;
    RECT rc{0, 0, 0, 0};

    switch (uMsg)
    {
    case WM_CREATE:
        // Add the window to the clipboard viewer chain.
        hwndNextViewer = SetClipboardViewer(m_hwnd);
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
        DeleteTray(m_hwnd);
        ChangeClipboardChain(m_hwnd, hwndNextViewer);
        PostQuitMessage(0);
        return 0;

    case WM_DRAWCLIPBOARD:  // clipboard contents changed.
        //系统是UTF-16，转换可选CP_ACP（相当于转GBK） 或 CP_UTF8（无损转换)
        GetClipboardTextW(CP_ACP);
        //文本转二维码过程
        if(txtPages.size() > 0)
            makeQrPage(0);

        InvalidateRect(m_hwnd, NULL, TRUE);

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
                SwitchWindow();
                break;
            }
            case WM_RBUTTONDOWN:
            {
                //获取鼠标坐标
                POINT pt; GetCursorPos(&pt);

                //解决在菜单外单击左键菜单不消失的问题
                SetForegroundWindow(m_hwnd);

                //使菜单某项变灰
                //EnableMenuItem(hMenu, ID_SHOW, MF_GRAYED);

                //显示并获取选中的菜单
                int cmd = TrackPopupMenu(hTrayMenu, TPM_RETURNCMD, pt.x, pt.y, NULL, m_hwnd, NULL);
                if (cmd == ID_EXIT)
                    PostMessage(m_hwnd, WM_DESTROY, NULL, NULL);
            }

            case WM_CONTEXTMENU:
            {
                //ShowContextMenu(hWnd);
                break;
            }
        }
        break;

    case WM_CLOSE:
        ToTray(m_hwnd);
        ShowWindow(m_hwnd, SW_HIDE);
        g_show = 0;
        return 0;

    case WM_QR_CODE:
        // 按左箭头<-键查看前一页
        if (!wParam && 0 < pageIndex && pageIndex < txtPages.size())
            makeQrPage(--pageIndex);
        // 按右箭头->键查看后一页
        if (wParam && -1 < pageIndex && pageIndex < txtPages.size() - 1)
            makeQrPage(++pageIndex);
        // 重画窗口
        InvalidateRect(m_hwnd, NULL, TRUE);
        return 0;

    case WM_HOTKEY:
        if (LOWORD(lParam) == (MOD_CONTROL | MOD_ALT) && HIWORD(lParam) == 'Q') // CTRL + ALT + Q
        {
            SwitchWindow();
            break;
        }

    case WM_GETMINMAXINFO:
        MINMAXINFO* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
        mmi->ptMinTrackSize.x = 21 * 2 + 4 * 2 * 2; // 最小宽度 21格加4格边框
        return 0;
    }
    return DefWindowProc(m_hwnd, uMsg, wParam, lParam);
}
