#include <windows.h>
#include <windowsx.h>
#include <winuser.h>
#include <vector>
#include <tuple>
#include "qrcodegen.h"
#include "basewin.h"
#include "resource1.h"
using std::vector;
using std::tuple;
using namespace std;
using namespace qrcodegen;

template <class T> void SafeRelease(T **ppT)
{
    if (*ppT)
    {
        (*ppT)->Release();
        *ppT = NULL;
    }
}

const int  QR_PAGE_SIZE = 600; //300 字节为一页


HINSTANCE  g_hInstance;
HWND       g_hWnd;
UINT       uFormat = (UINT)(-1);
BOOL       fAuto = TRUE;
NOTIFYICONDATA nid;
HMENU      hTrayMenu;

HHOOK      g_Hook;         // Handler of hook


/***********  键盘钩子消息处理 *********************/
LRESULT CALLBACK KeyboardProc(int nCode, WPARAM wParam, LPARAM lParam)
{
	KBDLLHOOKSTRUCT* pkh = (KBDLLHOOKSTRUCT*)lParam;


	//HC_ACTION: wParam 和lParam参数包含了键盘按键消息
	if (nCode == HC_ACTION)
	{
		//判断函数调用时指定虚拟键的状态
		//BOOL bCtrlKey =	::GetAsyncKeyState(VK_CONTROL) & 0x8000;
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
	if (g_hInstance && g_Hook)		// Already hooked!
		return TRUE;

	g_hInstance = (HINSTANCE)::GetModuleHandle(NULL);
	g_Hook = ::SetWindowsHookEx(WH_KEYBOARD_LL, (HOOKPROC)KeyboardProc, g_hInstance, 0);
	if (!g_Hook)
	{
		OutputDebugStringA("set keyboard hook failed.");
		return FALSE;
	}

	return TRUE;								// Hook has been created correctly
}
//取消键盘HOOK
BOOL UnSetHook()
{
	if (g_Hook) {								// Check if hook handler is valid
		::UnhookWindowsHookEx(g_Hook);			// Unhook is done here
		g_Hook = NULL;							// Remove hook handler to avoid to use it again
	}

	return TRUE;								// Hook has been removed
}
/*************************************************/

class MainWindow : public BaseWindow<MainWindow>
{
	HBRUSH                  hBrushBlack = CreateSolidBrush(RGB(0, 0, 0));
	HBRUSH                  hBrushWhite = CreateSolidBrush(RGB(255, 255, 255));

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
	void    PreviousPage();
	void    NextPage();

	char*   GetClipboardText();
	void    GetClipboardTextW(int codePage);
	void    GetClipboardTextW2();
	void    GetClipboardTextW3();
	void    UpdateClientSize();

	//双击托盘图标
    void    OnNotifyLeftButtonDblClick();

	QrCode qrCode = QrCode::encodeText("Hello World!", QrCode::Ecc::LOW);
    
public:

    MainWindow()
    {
    }

    PCWSTR  ClassName() const { return L"QR Code Class"; }
    LRESULT HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam);


    // Prints the given QrCode object to the console.
	// Creates a single QR Code, then prints it to the console.
	void getBasicCode(char* text) {
		//const char *text = "Hello, world!";              // User-supplied text
		const QrCode::Ecc errCorLvl = QrCode::Ecc::LOW;  // Error correction level
		if (text == NULL)
			return;
		// Make and print the QR Code symbol
		qrCode = QrCode::encodeText(text, errCorLvl);
	}

	void  getMaskCode(const char* text, int mask) {

		// Project Nayuki URL
		if (text == NULL)
			return;
		std::vector<QrSegment> segs0 = QrSegment::makeSegments(text);
		if (mask == -1)
			qrCode = QrCode::encodeSegments(segs0, QrCode::Ecc::HIGH, QrCode::MIN_VERSION, QrCode::MAX_VERSION, -1, true);  // Automatic mask
		if (mask == 3)
			qrCode = QrCode::encodeSegments(segs0, QrCode::Ecc::HIGH, QrCode::MIN_VERSION, QrCode::MAX_VERSION, 3, true);  // Force mask 3

																														   // Chinese text as UTF-8
		std::vector<QrSegment> segs1 = QrSegment::makeSegments(text);
		if (mask == 0)
			qrCode = QrCode::encodeSegments(segs1, QrCode::Ecc::MEDIUM, QrCode::MIN_VERSION, QrCode::MAX_VERSION, 0, true);  // Force mask 0
		if (mask == 1)
			qrCode = QrCode::encodeSegments(segs1, QrCode::Ecc::MEDIUM, QrCode::MIN_VERSION, QrCode::MAX_VERSION, 1, true);  // Force mask 1
		if (mask == 5)
			qrCode = QrCode::encodeSegments(segs1, QrCode::Ecc::MEDIUM, QrCode::MIN_VERSION, QrCode::MAX_VERSION, 5, true);  // Force mask 5
		if (mask == 7)
			qrCode = QrCode::encodeSegments(segs1, QrCode::Ecc::MEDIUM, QrCode::MIN_VERSION, QrCode::MAX_VERSION, 7, true);  // Force mask 7
	}

     void printQr(const QrCode &qr,HDC hdc,HDC hMemDC) {

		 int border = 4;

         RECT rc;
         GetClientRect(m_hwnd, &rc);
		 FillRect(hMemDC, &rc, hBrushWhite);

		 int unitX = rc.right / (qr.getSize() + 2 * border);
		 int unitY = rc.bottom / (qr.getSize() + 2 * border);

         //char info[256];
         //sprintf(info, "rc=(%d,%d,%d,%d),qr.size=%d,unitX=%d,unitY=%d\n",rc.left,rc.top,rc.right,rc.bottom,qr.getSize(),unitX,unitY);
         //OutputDebugStringA(info);

		 for (int y = 0; y < qr.getSize(); y ++) {
		     for (int x = 0; x < qr.getSize(); x++) {
				 int rx = x * unitX + (rc.right - unitX * qr.getSize()) / 2;
				 int ry = y * unitY + (rc.bottom - unitY * qr.getSize()) / 2;

				 RECT rectSegment{rx,ry,rx + unitX,ry + unitY };	

				 if (qr.getModule(x, y))
					 FillRect(hMemDC, &rectSegment, hBrushBlack);
                 else
					 FillRect(hMemDC, &rectSegment, hBrushWhite);
             }
         }
		 BitBlt(hdc, 0, 0, rc.right, rc.bottom, hMemDC, 0, 0, SRCCOPY);
    }

	 void printQr2(const QrCode &qr, HDC hdc, HDC hMemDC) {

		 int border = 4;

		 RECT rc;
		 GetClientRect(m_hwnd, &rc);
		 FillRect(hMemDC, &rc, hBrushWhite);

		 int unitX = 2;
		 int unitY = 2;

		 //char info[256];
		 //sprintf(info, "rc=(%d,%d,%d,%d),qr.size=%d,unitX=%d,unitY=%d\n",rc.left,rc.top,rc.right,rc.bottom,qr.getSize(),unitX,unitY);
		 //OutputDebugStringA(info);

		 for (int y = 0; y < qr.getSize(); y++) {
			 for (int x = 0; x < qr.getSize(); x++) {
				 int rx = x * unitX + (rc.right - unitX * qr.getSize()) / 2;
				 int ry = y * unitY + (rc.bottom - unitY * qr.getSize()) / 2;

				 RECT rectSegment{ rx,ry,rx + unitX,ry + unitY };

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
	printQr2(qrCode,hDC,memDC);

	EndPaint(m_hwnd, &ps);
	DeleteObject(m_hBitMap);
	DeleteDC(memDC);
	DeleteDC(hDC);
}

void MainWindow::Resize()
{
	InvalidateRect(m_hwnd, NULL, FALSE);
}

void MainWindow::OnLButtonDown(int pixelX, int pixelY, DWORD flags)
{
}

char* MainWindow::GetClipboardText()
{
    // Try opening the clipboard
    if (!OpenClipboard(nullptr))
        return NULL;

      // Get handle of clipboard object for ANSI text
        HANDLE hData = GetClipboardData(CF_TEXT);
    if (hData == nullptr)
        return NULL;

      // Lock the handle to get the actual text pointer
       // char * pszText = static_cast<char*>(GlobalLock(hData));
    char* wstr = (char*)GlobalLock(hData);
    if (wstr == nullptr)
        return NULL;

    // Save text in a string class instance
    //std::string text(wstr);	

    // Release the lock
    GlobalUnlock(hData);

    // Release the clipboard
    CloseClipboard();

    return wstr;
}


void MainWindow::GetClipboardTextW(int codePage)
{
	//清空分页
	txtPages.clear();

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
	size_t size = GlobalSize(hData);
	
	int totalBytes = WideCharToMultiByte(codePage, 0, pwstr, -1, 0, 0, NULL, NULL);
	int a = totalBytes / QR_PAGE_SIZE;
	int b = (totalBytes % QR_PAGE_SIZE) == 0 ? 0 : 1;
	int avgPageSize = totalBytes / (a + b); //实际估算每页字节数
	int remain = totalBytes % (a + b); //按每页avgPageSize计算，剩余字节，
	//WriteLog("totalBytes=%d,pagecount=%d,avgPageSize=%d,remain=%d", totalBytes, a + b, avgPageSize, remain);
	// 分页保存文本
	int wLen = 0;
	int chLen = 0;
	do {
		//逐个宽字符累计长度
		int c = WideCharToMultiByte(codePage, 0, pwstr++, 1, 0, 0, NULL, NULL);
		chLen += c;
		totalBytes-=c;
		wLen++;
		//前面remain页每页加一个字节，接近平均
		if (chLen >= (txtPages.size() < remain ? avgPageSize+1 : avgPageSize) || totalBytes == 0)
		{
			char* segment = new char[chLen + 1];
			::WideCharToMultiByte(codePage, 0, pwstr - wLen, -1, segment, chLen, NULL, NULL);
			segment[chLen] = 0;
			txtPages.push_back(segment);
			chLen = 0;
			wLen = 0;
		}
	} while (totalBytes > 0);
	
	//for(int k=0;k<txtPages.size() ;k++)
	//	WriteLog("P%d = %d,",k,txtPages[k].length());

	pageIndex = 0;

	// Release the lock
	GlobalUnlock(hData);

	// Release the clipboard
	CloseClipboard();
}

//按左箭头<-键，查看前页二维码
void  MainWindow::PreviousPage()
{
	if (pageIndex > 0 && txtPages.size() > 1)
	{
		pageIndex--;
		getMaskCode(txtPages[pageIndex].c_str(), 0);
	}
}
//按右箭头<-键，查看后页二维码
void  MainWindow::NextPage()
{
	if (pageIndex < txtPages.size() - 1)
	{
		pageIndex++;
		getMaskCode(txtPages[pageIndex].c_str(), 0);
	}
}
//生成托盘
void ToTray(HWND hWnd)
{
    nid.cbSize = (DWORD)sizeof(NOTIFYICONDATA);
    nid.hWnd = hWnd;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_MESSAGE ;
    nid.uCallbackMessage = NOTIFICATION_TRAY_ICON_MSG;//自定义的消息 处理托盘图标事件
	nid.hIcon = static_cast<HICON>(LoadImage(GetModuleHandle(0),
		//TEXT("icon1.ico"),
		MAKEINTRESOURCE(IDI_ICON1),
        IMAGE_ICON,
        0, 0,
        //LR_DEFAULTCOLOR | LR_SHARED | LR_DEFAULTSIZE | LR_LOADFROMFILE));
		LR_DEFAULTSIZE));
	

	hTrayMenu = CreatePopupMenu();//生成托盘菜单
	AppendMenu(hTrayMenu, MF_STRING, ID_EXIT, TEXT("Exit"));

    //wcscpy_s(nid.szTip, "自定义程序名");//鼠标放在托盘图标上时显示的文字
    Shell_NotifyIcon(NIM_ADD, &nid);//在托盘区添加图标
}

void DeleteTray(HWND hWnd)
{
    Shell_NotifyIcon(NIM_DELETE, &nid);//在托盘中删除图标
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow)
{
    MainWindow win;

    if (!win.Create(L"QR CODE", WS_OVERLAPPED | WS_SYSMENU))// WS_CAPTION | WS_POPUP WS_OVERLAPPED | WS_THICKFRAME | WS_SYSMENU
    {
        return 0;
    }

	g_hWnd = win.Window();
    SetWindowPos(win.Window(), HWND_TOPMOST, 200, 200, 0, 0, SWP_NOMOVE | SWP_NOSIZE); //| 

    HICON hIcon = static_cast<HICON>(LoadImage(GetModuleHandle(0),
		MAKEINTRESOURCE(IDI_ICON1),
        IMAGE_ICON,
        0, 0,
        LR_DEFAULTSIZE));

    SendMessage(win.Window(), WM_SETICON, ICON_BIG, (LPARAM)hIcon);
    //SendMessage(win.Window(), WM_SETICON, ICON_SMALL, (LPARAM)hIcon);

	if (!RegisterHotKey(win.Window(), 1, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 0x51))
		MessageBox(win.Window(), L"注册热键失败", L"提示", MB_OK);

    if (!SetHook())
        ;

    ShowWindow(win.Window(), nCmdShow);

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

void MainWindow::OnNotifyLeftButtonDblClick()
{
    /*MessageBox(
        NULL,
        (LPCWSTR)L"ok",
        (LPCWSTR)L"Account Details",
        MB_DEFBUTTON2
    );*/
	//双击托盘图标显示托盘
    ShowWindow(this->m_hwnd, SW_SHOW);
}

void MainWindow::UpdateClientSize()
{
	//根据文本大小调整窗口大小
	RECT rcWindow;                //整个窗口的大小
	RECT rcClient;                //客户区大小
	int borderWidth, borderHeight;//非客户区大小

	GetWindowRect(m_hwnd, &rcWindow);
	GetClientRect(m_hwnd, &rcClient);
	borderWidth = (rcWindow.right - rcWindow.left)
		- (rcClient.right - rcClient.left);
	borderHeight = (rcWindow.bottom - rcWindow.top)
		- (rcClient.bottom - rcClient.top);

	int width = qrCode.getSize() * 2 + 4 * 2;
	/*char info[256];
	sprintf(info, "win=(%d,%d,%d,%d),win=(%d,%d,%d,%d),width=%d,border_width=%d,border_height=%d,qrsize=%d",
		rcWindow.left, rcWindow.top, rcWindow.right, rcWindow.bottom,
		rcClient.left, rcClient.top, rcClient.right, rcClient.bottom,
		width,borderWidth,borderHeight,
		qrCode.getSize());
	OutputDebugStringA(info);*/
	RECT r{ 0,0,width,width };
	AdjustWindowRect(&r, GetWindowLong(m_hwnd, GWL_STYLE), FALSE);

	SetWindowPos(m_hwnd, 0, r.left, r.top,
		r.right - r.left,
		r.bottom - r.top, SWP_NOMOVE | SWP_NOZORDER);
}

LRESULT MainWindow::HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    static HWND hwndNextViewer;
	int width;
	RECT rc{ 0,0,0,0 };

    switch (uMsg)
    {
    case WM_CREATE:
        // Add the window to the clipboard viewer chain. 
        hwndNextViewer = SetClipboardViewer(this->m_hwnd);
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
        DeleteTray(this->m_hwnd);
        ChangeClipboardChain(this->m_hwnd, hwndNextViewer);
        PostQuitMessage(0);
        return 0;

    case WM_DRAWCLIPBOARD:  // clipboard contents changed.

		//系统是UTF-16，转换可选CP_ACP（相当于转GBK） 或 CP_UTF8（无损转换)
		GetClipboardTextW(CP_ACP);
		//文本转二维码过程	
		if(txtPages.size() > 0)
			getMaskCode(txtPages[0].c_str(), 0);
        
		UpdateClientSize();

        InvalidateRect(this->m_hwnd, NULL, TRUE);
        
        BringWindowToTop(this->m_hwnd);
        SendMessage(hwndNextViewer, uMsg, wParam, lParam);
        break;

    case WM_PAINT:
        OnPaint();
        return 0;

    case WM_SIZE:
        Resize();
        if (wParam == SIZE_MINIMIZED) {
            ToTray(this->m_hwnd);
            ShowWindow(this->m_hwnd, SW_HIDE);
        }
        return 0;

    case WM_LBUTTONDOWN:
        return 0;

    case WM_LBUTTONUP:
        return 0;

    case WM_MOUSEMOVE:
        //OnMouseMove(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), (DWORD)wParam);
        return 0;
    case NOTIFICATION_TRAY_ICON_MSG:
    {
        // This is a message that originated with the
        // Notification Tray Icon. The lParam tells use exactly which event
        // it is.
        switch (lParam)
        {
			case WM_LBUTTONDBLCLK:
			{
				OnNotifyLeftButtonDblClick();
				break;
			}
			case WM_RBUTTONDOWN:
			{
				//获取鼠标坐标
				POINT pt; GetCursorPos(&pt);

				//解决在菜单外单击左键菜单不消失的问题
				SetForegroundWindow(this->m_hwnd);

				//使菜单某项变灰
				//EnableMenuItem(hMenu, ID_SHOW, MF_GRAYED);	

				//显示并获取选中的菜单
				int cmd = TrackPopupMenu(hTrayMenu, TPM_RETURNCMD, pt.x, pt.y, NULL, this->m_hwnd,
					NULL);
				if (cmd == ID_EXIT)
					PostMessage(this->m_hwnd, WM_DESTROY, NULL, NULL);
			}

			case WM_CONTEXTMENU:
			{
				//ShowContextMenu(hWnd);
				break;
			}
        }
		break;
	case WM_CLOSE:
		ToTray(this->m_hwnd);
		ShowWindow(this->m_hwnd, SW_HIDE);
		return 0;
	case WM_QR_CODE:		
		if (wParam == 0)//前一页
			PreviousPage();
		else //后一页
			NextPage();
		//重画窗口
		InvalidateRect(this->m_hwnd, NULL, TRUE);
		return 0;
	case WM_HOTKEY:
		if (HIWORD(lParam) == 0x51 && LOWORD(lParam) == (MOD_CONTROL | MOD_ALT)) // CTRL + ALT + q
		{
			ShowWindow(g_hWnd, SW_SHOW);
			break;
		}
	case WM_GETMINMAXINFO:
		MINMAXINFO* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
		mmi->ptMinTrackSize.x = 21 * 2 + 4*2*2;//最小宽度 21格加4格边框
		return 0;
    }
    }
    return DefWindowProc(m_hwnd, uMsg, wParam, lParam);
}
