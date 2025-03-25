#include <Windows.h>
#include <wchar.h>
#include <CommCtrl.h>
#include <vector>
#include <string>
#include <assert.h>
#include <filesystem>
#include <fstream>

#pragma comment(lib, "comctl32.lib")

////////////////////////////////////////////////////////////

static HWND g_EmojiPickerWindow = NULL;
static HWND g_SearchBox = NULL;
static HWND g_ListBox = NULL;
static std::vector<std::wstring> g_Kaomojis;
static std::vector<std::wstring> g_FilteredKaomojis;
static int g_CurrentIndex = 0;

////////////////////////////////////////////////////////////

std::wstring ConvertUTF8ToWideString(const std::string& utf8Str)
{
    int sizeNeeded = MultiByteToWideChar(CP_UTF8, 0, utf8Str.c_str(), -1, nullptr, 0);
    std::wstring wstr(sizeNeeded, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8Str.c_str(), -1, &wstr[0], sizeNeeded);
    return wstr;
}

////////////////////////////////////////////////////////////

void SelectFirstVisibleEmoji() {
	SendMessage(g_ListBox, LB_SETSEL, TRUE, 0);
    SendMessage(g_ListBox, LB_SETCURSEL, 0, 0);
}

////////////////////////////////////////////////////////////

static bool TryLoadKaomojisFromDisk(const std::filesystem::path& path)
{
	std::ifstream file(path);
	if (!file)
	{
		return false;
	}

	std::string line;
	while (std::getline(file, line))
	{
		if (line.length() > 0)
		{
			g_Kaomojis.push_back(ConvertUTF8ToWideString(line));
		}
	}

	return true;
}

////////////////////////////////////////////////////////////

static void LoadKaomojisFromDisk()
{
	wchar_t pathBuffer[MAX_PATH];
	DWORD length = GetModuleFileNameW(NULL, pathBuffer, MAX_PATH);

	std::filesystem::path exePath(pathBuffer);

	auto path1 = exePath.parent_path().append("emotes.txt");
	if (TryLoadKaomojisFromDisk(path1))
		return;

	MessageBoxW(NULL, L"Failed to find emotes.txt :(", L"Error", MB_OK | MB_ICONERROR);
	exit(1);
}

////////////////////////////////////////////////////////////

static void CopyToClipboard(const std::wstring& text)
{
	if (OpenClipboard(NULL))
	{
		EmptyClipboard();
		size_t size = (text.length() + 1) * sizeof(wchar_t);
		HGLOBAL hGlobal = GlobalAlloc(GMEM_MOVEABLE, size);
		if (hGlobal)
		{
			wchar_t* pGlobal = (wchar_t*)GlobalLock(hGlobal);
			wcscpy_s(pGlobal, text.length() + 1, text.c_str());
			GlobalUnlock(hGlobal);
			SetClipboardData(CF_UNICODETEXT, hGlobal);
		}
		CloseClipboard();
	}
}

////////////////////////////////////////////////////////////

static void CopySelectedAndHide()
{
	int index = SendMessage(g_ListBox, LB_GETCURSEL, 0, 0);
	if (index != LB_ERR && index < (int)g_FilteredKaomojis.size())
	{
		std::wstring emote = g_FilteredKaomojis[index];
		size_t colon = emote.find(L':');
		if (colon != std::wstring::npos)
		{
			emote = emote.substr(colon + 1);
		}

		CopyToClipboard(emote);
		ShowWindow(g_EmojiPickerWindow, SW_HIDE);
	}
}

////////////////////////////////////////////////////////////

static void Reflow()
{
	int padding = 8;

	RECT windowRect;
	GetClientRect(g_EmojiPickerWindow, &windowRect);
	int innerWidth = windowRect.right - windowRect.left;
	int innerHeight = windowRect.bottom - windowRect.top;

	int itemWidth = (innerWidth - padding * 2);

	int searchboxHeight = 40;
	int listboxHeight = innerHeight - searchboxHeight - padding * 3;

	MoveWindow(g_SearchBox, padding, padding,                       itemWidth, searchboxHeight, TRUE);
	MoveWindow(g_ListBox,   padding, searchboxHeight + padding * 2, itemWidth, listboxHeight,   TRUE);


	LONG style = GetWindowLong(g_ListBox, GWL_STYLE);
	SetWindowLong(g_ListBox, GWL_STYLE, style | LBS_NOINTEGRALHEIGHT);
	InvalidateRect(g_ListBox, NULL, TRUE);
	UpdateWindow(g_ListBox);
}

////////////////////////////////////////////////////////////

static void FilterKaomojis(const std::wstring& searchText)
{
	g_FilteredKaomojis.clear();
	for (const auto& kaomoji : g_Kaomojis)
	{
		if (searchText.empty() || kaomoji.find(searchText) != std::wstring::npos)
		{
			g_FilteredKaomojis.push_back(kaomoji);
		}
	}
	
	// Update listbox
	SendMessage(g_ListBox, WM_SETREDRAW, FALSE, 0);
	SendMessage(g_ListBox, LB_RESETCONTENT, 0, 0);
	for (const auto& kaomoji : g_FilteredKaomojis)
	{
		SendMessage(g_ListBox, LB_ADDSTRING, 0, (LPARAM)kaomoji.c_str());
	}
	SendMessage(g_ListBox, WM_SETREDRAW, TRUE, 0);
	InvalidateRect(g_ListBox, NULL, TRUE);

	SelectFirstVisibleEmoji();
}

static void ShowEmojiPicker()
{
	assert(g_EmojiPickerWindow);

	// Get screen dimensions
	int screenWidth = GetSystemMetrics(SM_CXSCREEN);
	int screenHeight = GetSystemMetrics(SM_CYSCREEN);

	RECT windowRect;
	GetWindowRect(g_EmojiPickerWindow, &windowRect);
	int windowWidth = windowRect.right - windowRect.left;
	int windowHeight = windowRect.bottom - windowRect.top;

	int posX = (screenWidth - windowWidth) / 2;
	int posY = (screenHeight - windowHeight) / 2;
	SetWindowPos(g_EmojiPickerWindow, NULL, posX, posY, 0, 0, SWP_NOSIZE);

	ShowWindow(g_EmojiPickerWindow, SW_SHOW);
	SetForegroundWindow(g_EmojiPickerWindow);

	SetWindowText(g_SearchBox, L"");
	SetFocus(g_SearchBox);
}

////////////////////////////////////////////////////////////

static void HideEmojiPicker()
{
	ShowWindow(g_EmojiPickerWindow, SW_HIDE);
}

////////////////////////////////////////////////////////////

static LRESULT CALLBACK SearchBoxWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
	switch (uMsg)
	{
		case WM_KEYDOWN:
		{
			if (wParam == VK_UP || wParam == VK_DOWN || wParam == VK_PRIOR || wParam == VK_NEXT || wParam == VK_RETURN)
			{
			    SendMessage(g_ListBox, WM_KEYDOWN, wParam, 0);
				return 0;
			}

			if (wParam == VK_ESCAPE)
			{
				HideEmojiPicker();
				return 0;
			}

			if (wParam == 'A' && (GetKeyState(VK_CONTROL) & 0x8000))
			{
				SendMessage(hWnd, EM_SETSEL, 0, -1); // Select all
				return 0;
			}
		}
	}

	return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

////////////////////////////////////////////////////////////

static LRESULT CALLBACK ListBoxWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
	switch (uMsg)
	{
		case WM_KEYDOWN:
		{
			if (wParam == VK_RETURN)
			{
				CopySelectedAndHide();
			}
		}
	}

	return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

////////////////////////////////////////////////////////////

static LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message)
	{
		case WM_CREATE:
		{
			g_SearchBox = CreateWindowExW(
				WS_EX_CLIENTEDGE,
				L"EDIT",
				L"",
				WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
				6, 6, 760, 40,
				hWnd, NULL, GetModuleHandle(NULL), NULL);
			SetWindowSubclass(g_SearchBox, &SearchBoxWndProc, 0, 0);

			g_ListBox = CreateWindowExW(
				WS_EX_CLIENTEDGE | LBS_NOINTEGRALHEIGHT,
				L"LISTBOX",
				L"",
				WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY,
				10, 50, 780, 500,
				hWnd, NULL, GetModuleHandle(NULL), NULL);
			SetWindowSubclass(g_ListBox, &ListBoxWndProc, 0, 0);

			HFONT hFont = CreateFontW(32, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
				DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
				DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");

			SendMessage(g_SearchBox, WM_SETFONT, (WPARAM)hFont, TRUE);
			SendMessage(g_ListBox, WM_SETFONT, (WPARAM)hFont, TRUE);

			Reflow();
			return 0;
		}

		case WM_KEYDOWN:
		{
			if (wParam == VK_ESCAPE)
			{
				ShowWindow(hWnd, SW_HIDE);
				return 0;
			}
			break;
		}

		case WM_COMMAND:
		{
			if (HIWORD(wParam) == EN_CHANGE && (HWND)lParam == g_SearchBox)
			{
				wchar_t searchText[256];
				GetWindowTextW(g_SearchBox, searchText, 256);
				FilterKaomojis(searchText);
			}
			else if (HIWORD(wParam) == LBN_DBLCLK && (HWND)lParam == g_ListBox)
			{
				CopySelectedAndHide();
			}
			break;
		}

		case WM_SIZE:
		{
			Reflow();
			return 0;
		}

		case WM_CLOSE:
		{
			ShowWindow(hWnd, SW_HIDE);
			return 0;
		}
	}
	return DefWindowProcW(hWnd, message, wParam, lParam);
}

////////////////////////////////////////////////////////////

int WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR pCmdLine, _In_ int nShowCmd)
{
	// Initialize common controls
	INITCOMMONCONTROLSEX icex = { sizeof(INITCOMMONCONTROLSEX), ICC_STANDARD_CLASSES };
	InitCommonControlsEx(&icex);

	LoadKaomojisFromDisk();

	{ // Register window class:
		WNDCLASSEXW wndclass = {};
		wndclass.cbSize = sizeof(WNDCLASSEXW);
		wndclass.lpszClassName = L"EmojiPicker";
		wndclass.lpfnWndProc = WndProc;
		wndclass.hInstance = hInstance;
		wndclass.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);

		ATOM atom = RegisterClassExW(&wndclass);
		if (!atom)
		{
			MessageBoxW(NULL, L"Failed to register window class :(", L"Error", MB_OK | MB_ICONERROR);
			return 1;
		}
	}

	{ // Create window:
		g_EmojiPickerWindow = CreateWindowExW(
			0,
			L"EmojiPicker",
			L"EMOJI PICKER DELUXE",
			WS_OVERLAPPEDWINDOW,
			CW_USEDEFAULT, CW_USEDEFAULT, 600, 1100,
			nullptr,
			nullptr,
			hInstance,
			nullptr);

		LONG_PTR style = GetWindowLongPtr(g_EmojiPickerWindow, GWL_STYLE); 
		style &= ~WS_MINIMIZEBOX;
		SetWindowLongPtr(g_EmojiPickerWindow, GWL_STYLE, style);

		DWORD err = GetLastError();

		if (!g_EmojiPickerWindow)
		{
			MessageBoxW(NULL, L"Failed to create window :(", L"Error", MB_OK | MB_ICONERROR);
			return 1;
		}
	}

	if (!RegisterHotKey(NULL, 1, MOD_WIN | MOD_SHIFT, 'I'))
	{
		MessageBoxW(NULL, L"Failed to register hotkey :(", L"Error", MB_OK | MB_ICONERROR);
		return 1;
	}

	MSG msg = { 0 };
	while (GetMessage(&msg, NULL, 0, 0) != 0)
	{
		switch (msg.message)
		{
			case WM_HOTKEY:
			{
				ShowEmojiPicker();
				break;
			}
			default:
			{
				TranslateMessage(&msg);
				DispatchMessage(&msg);
			}
		}
	}

	UnregisterHotKey(NULL, 1);
	return 0;
}

////////////////////////////////////////////////////////////
