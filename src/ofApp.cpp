/*

	Spout2OverlayHUD V1.0

	Spout 2.007-017
	OpenFrameworks 13 v20260731
	Visual Studio 2026

	=========================================================================
	This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
	=========================================================================
*/

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)
	#define SystemOpenURL(url) std::system("start " url);
#elif __APPLE__
	#define SystemOpenURL(url) std::system("open " url);
#elif __linux__
	#define SystemOpenURL(url) std::system("xdg-open" url);
#else
	#error "Unknown compiler"
#endif

#include "ofApp.h"
#include <windows.h>
#include <shellapi.h>
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include <fstream>
#include <shlobj.h>
#include <map>
#include <algorithm>

//--------------------------------------------------------------
// Globals
//--------------------------------------------------------------
static NOTIFYICONDATA nid;
static HWND hwnd = nullptr;
static WNDPROC originalWndProc = nullptr;
static ofApp * g_appInstance = nullptr;

// Tray menu / hotkey command ids
enum {
	IDM_SELECTSOURCE = 1,
	IDM_ABOUT = 2,
	IDM_EXIT = 3,
	IDM_PAUSE = 4,
	IDM_CUSTOMSIZE = 5,
	IDM_FITTOSCREEN = 6,
	IDM_SMOOTH = 7,
	IDM_HOTKEY_PAUSE_SET = 10,
	IDM_HOTKEY_SCREEN_SET = 11,
	IDM_HOTKEY_CLEAR = 12,
	IDM_SCREEN_BASE = 100, // 100 + monitor index
	HK_PAUSE = 1001,       // RegisterHotKey ids
	HK_SCREEN = 1002,
	IDM_SPECIAL_BASE = 9000 // headers / separators (disabled, never a command)
};

//--------------------------------------------------------------
// Owner-drawn dark tray menu
//--------------------------------------------------------------
struct MenuItemData {
	std::wstring text;        // main label
	std::wstring shortcut;    // right-aligned gray hint (e.g. hotkey)
	std::wstring description; // tooltip shown while the item is highlighted
	bool isHeader = false;
	bool isSeparator = false;
	bool checked = false;
	bool hasSubmenu = false;
};

static std::map<UINT_PTR, MenuItemData> g_menuItems;
static HMENU g_topMenu = nullptr; // top-level menu handle while tracking

static const COLORREF MENU_BG     = RGB(0x20, 0x20, 0x20); // #202020
static const COLORREF MENU_HOVER  = RGB(0x3F, 0x3F, 0x3F); // #3F3F3F
static const COLORREF MENU_FG     = RGB(0xEB, 0xEB, 0xEB); // #EBEBEB
static const COLORREF MENU_GRAY   = RGB(0xA0, 0xA0, 0xA0); // #A0A0A0
static const COLORREF MENU_SEP    = RGB(0x46, 0x46, 0x46); // #464646
static const COLORREF MENU_BORDER = RGB(0x20, 0x20, 0x20); // #202020 matching bg without border

// Per-window DPI (dynamic resolve for compatibility)
static UINT GetWindowDPI(HWND h)
{
	if (h) {
		static UINT(WINAPI * pGetDpiForWindow)(HWND) = nullptr;
		static bool resolved = false;
		if (!resolved) {
			resolved = true;
			HMODULE user32 = GetModuleHandleW(L"user32.dll");
			if (user32)
				pGetDpiForWindow = (UINT(WINAPI *)(HWND))GetProcAddress(user32, "GetDpiForWindow");
		}
		if (pGetDpiForWindow) {
			UINT dpi = pGetDpiForWindow(h);
			if (dpi) return dpi;
		}
	}
	HDC dc = GetDC(nullptr);
	UINT dpi = (UINT)GetDeviceCaps(dc, LOGPIXELSX);
	ReleaseDC(nullptr, dc);
	return dpi ? dpi : 96;
}

// DPI of the monitor where the tray menu opens (cursor position).
// The menu is sized for the screen it appears on, independent of
// where the overlay window sits: this keeps the menu size stable
// even when the overlay and the taskbar are on monitors with a
// different scaling factor.
static UINT g_menuDpi = 0;

static UINT GetCursorMonitorDPI()
{
	POINT pt;
	GetCursorPos(&pt);
	HMONITOR mon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);

	// GetDpiForMonitor (Windows 10 1607+), dynamic resolve
	static HRESULT(WINAPI * pGetDpiForMonitor)(HMONITOR, int, UINT *, UINT *) = nullptr;
	static bool resolved = false;
	if (!resolved) {
		resolved = true;
		HMODULE shcore = LoadLibraryW(L"shcore.dll");
		if (shcore)
			pGetDpiForMonitor = (HRESULT(WINAPI *)(HMONITOR, int, UINT *, UINT *))GetProcAddress(shcore, "GetDpiForMonitor");
	}
	if (pGetDpiForMonitor && mon) {
		UINT dx = 96, dy = 96;
		// MDT_EFFECTIVE_DPI = 0
		if (SUCCEEDED(pGetDpiForMonitor(mon, 0, &dx, &dy)) && dx)
			return dx;
	}
	return GetWindowDPI(hwnd);
}

static int ScaleByDpi(int v, UINT dpi)
{
	return MulDiv(v, (int)dpi, 96);
}

// Segoe UI 9pt, cached per DPI
static HFONT GetMenuFont(UINT dpi)
{
	static std::map<UINT, HFONT> cache;
	auto it = cache.find(dpi);
	if (it != cache.end())
		return it->second;
	HFONT f = CreateFontW(-(int)MulDiv(9, (int)dpi, 72), 0, 0, 0, FW_NORMAL,
		FALSE, FALSE, FALSE, DEFAULT_CHARSET,
		OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
		DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
	cache[dpi] = f;
	return f;
}

// Shared id counter for headers / separators / popup items.
// Reset on every menu build so ids never collide.
static UINT_PTR g_nextSpecialMenuId = IDM_SPECIAL_BASE + 100;

static UINT_PTR AddMenuItem(HMENU hMenu, UINT id, const std::wstring & text,
	bool checked = false, const std::wstring & shortcut = L"",
	const std::wstring & description = L"")
{
	MenuItemData d;
	d.text = text;
	d.shortcut = shortcut;
	d.checked = checked;
	d.description = description;
	g_menuItems[id] = d;
	AppendMenuW(hMenu, MF_OWNERDRAW, id, (LPCWSTR)(UINT_PTR)id);
	return id;
}

static UINT_PTR AddMenuPopupItem(HMENU hMenu, const std::wstring & text, HMENU hSubMenu,
	const std::wstring & description = L"")
{
	UINT_PTR key = g_nextSpecialMenuId++;
	MenuItemData d;
	d.text = text;
	d.hasSubmenu = true;
	d.description = description;
	g_menuItems[key] = d;
	AppendMenuW(hMenu, MF_POPUP | MF_OWNERDRAW, (UINT_PTR)hSubMenu, (LPCWSTR)key);
	return key;
}

static UINT_PTR AddMenuSeparator(HMENU hMenu)
{
	UINT_PTR key = g_nextSpecialMenuId++;
	MenuItemData d;
	d.isSeparator = true;
	g_menuItems[key] = d;
	AppendMenuW(hMenu, MF_OWNERDRAW | MF_DISABLED, (UINT)key, (LPCWSTR)key);
	return key;
}

static UINT_PTR AddMenuHeader(HMENU hMenu, const std::wstring & text)
{
	UINT_PTR key = g_nextSpecialMenuId++;
	MenuItemData d;
	d.text = text;
	d.isHeader = true;
	g_menuItems[key] = d;
	AppendMenuW(hMenu, MF_OWNERDRAW | MF_DISABLED, (UINT)key, (LPCWSTR)key);
	return key;
}

static void MeasureTrayMenuItem(HWND hWnd, LPMEASUREITEMSTRUCT mis)
{
	auto it = g_menuItems.find((UINT_PTR)mis->itemData);
	if (it == g_menuItems.end()) {
		mis->itemHeight = 20;
		mis->itemWidth = 100;
		return;
	}
	const MenuItemData & item = it->second;
	UINT dpi = g_menuDpi ? g_menuDpi : GetWindowDPI(hWnd);

	if (item.isSeparator) {
		mis->itemHeight = ScaleByDpi(7, dpi);
		mis->itemWidth = ScaleByDpi(10, dpi);
		return;
	}

	HDC dc = GetDC(hWnd);
	HGDIOBJ oldFont = SelectObject(dc, GetMenuFont(dpi));
	SIZE szText = { 0, 0 };
	SIZE szShortcut = { 0, 0 };
	GetTextExtentPoint32W(dc, item.text.c_str(), (int)item.text.size(), &szText);
	if (!item.shortcut.empty())
		GetTextExtentPoint32W(dc, item.shortcut.c_str(), (int)item.shortcut.size(), &szShortcut);
	SelectObject(dc, oldFont);
	ReleaseDC(hWnd, dc);

	mis->itemHeight = szText.cy + ScaleByDpi(9, dpi);
	int width = ScaleByDpi(12, dpi) + ScaleByDpi(22, dpi) + szText.cx + ScaleByDpi(24, dpi);
	if (!item.shortcut.empty())
		width += szShortcut.cx + ScaleByDpi(12, dpi);
	if (item.hasSubmenu)
		width += ScaleByDpi(14, dpi);
	// Clamp the width so the menu can never become oversized
	// (guards against first-run measurement glitches)
	int minW = ScaleByDpi(200, dpi);
	int maxW = ScaleByDpi(340, dpi);
	if (width < minW) width = minW;
	if (width > maxW) width = maxW;
	mis->itemWidth = width;
}

static void DrawTrayMenuItem(HWND hWnd, const DRAWITEMSTRUCT * dis)
{
	auto it = g_menuItems.find((UINT_PTR)dis->itemData);
	if (it == g_menuItems.end())
		return;
	const MenuItemData & item = it->second;
	UINT dpi = g_menuDpi ? g_menuDpi : GetWindowDPI(hWnd);

	RECT rc = dis->rcItem;
	bool selected = (dis->itemState & ODS_SELECTED) && !item.isHeader && !item.isSeparator;

	HBRUSH bg = CreateSolidBrush(selected ? MENU_HOVER : MENU_BG);
	FillRect(dis->hDC, &rc, bg);
	DeleteObject(bg);

	if (item.isSeparator) {
		int midY = (rc.top + rc.bottom) / 2;
		RECT line = { rc.left + ScaleByDpi(10, dpi), midY, rc.right - ScaleByDpi(10, dpi), midY + 1 };
		HBRUSH lb = CreateSolidBrush(MENU_SEP);
		FillRect(dis->hDC, &line, lb);
		DeleteObject(lb);
		return;
	}

	SetBkMode(dis->hDC, TRANSPARENT);
	HGDIOBJ oldFont = SelectObject(dis->hDC, GetMenuFont(dpi));
	COLORREF fg = item.isHeader ? MENU_GRAY : MENU_FG;

	// Check glyph for the current selection (toggles / current screen)
	if (item.checked) {
		SetTextColor(dis->hDC, fg);
		RECT checkRect = { rc.left + ScaleByDpi(8, dpi), rc.top, rc.left + ScaleByDpi(30, dpi), rc.bottom };
		DrawTextW(dis->hDC, L"\x2713", -1, &checkRect, DT_VCENTER | DT_SINGLELINE | DT_CENTER); // check mark
	}

	RECT textRect = { rc.left + ScaleByDpi(32, dpi), rc.top, rc.right, rc.bottom };
	if (item.hasSubmenu)
		textRect.right = rc.right - ScaleByDpi(22, dpi); // room for the system submenu arrow
	SetTextColor(dis->hDC, fg);
	DrawTextW(dis->hDC, item.text.c_str(), -1, &textRect, DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

	// Right-aligned gray shortcut hint (e.g. current hotkey)
	if (!item.shortcut.empty()) {
		SetTextColor(dis->hDC, MENU_GRAY);
		int rightPad = item.hasSubmenu ? ScaleByDpi(22, dpi) : ScaleByDpi(10, dpi);
		RECT scRect = textRect;
		scRect.right = rc.right - rightPad;
		DrawTextW(dis->hDC, item.shortcut.c_str(), -1, &scRect, DT_VCENTER | DT_SINGLELINE | DT_RIGHT);
	}

	// No custom submenu arrow: Windows draws its own arrow when the
	// item is highlighted, drawing ours too resulted in mismatched
	// glyphs. Space for the system arrow is reserved via rightPad.

	SelectObject(dis->hDC, oldFont);
}

//--------------------------------------------------------------
// Hotkey string helpers
//--------------------------------------------------------------
static std::wstring VkToWString(UINT vk)
{
	if (vk >= 'A' && vk <= 'Z') return std::wstring(1, (wchar_t)vk);
	if (vk >= '0' && vk <= '9') return std::wstring(1, (wchar_t)vk);
	if (vk >= VK_F1 && vk <= VK_F24) return L"F" + std::to_wstring(vk - VK_F1 + 1);
	switch (vk) {
	case VK_SPACE: return L"Space";
	case VK_RETURN: return L"Enter";
	case VK_TAB: return L"Tab";
	case VK_BACK: return L"Backspace";
	case VK_INSERT: return L"Insert";
	case VK_DELETE: return L"Delete";
	case VK_HOME: return L"Home";
	case VK_END: return L"End";
	case VK_PRIOR: return L"PgUp";
	case VK_NEXT: return L"PgDn";
	case VK_LEFT: return L"Left";
	case VK_RIGHT: return L"Right";
	case VK_UP: return L"Up";
	case VK_DOWN: return L"Down";
	case VK_SNAPSHOT: return L"PrtSc";
	case VK_OEM_3: return L"`";
	case VK_OEM_MINUS: return L"-";
	case VK_OEM_COMMA: return L",";
	case VK_OEM_PERIOD: return L".";
	case VK_OEM_2: return L"/";
	case VK_OEM_1: return L";";
	case VK_OEM_7: return L"'";
	case VK_OEM_4: return L"[";
	case VK_OEM_5: return L"\\";
	case VK_OEM_6: return L"]";
	}
	return L"Key" + std::to_wstring(vk);
}

static std::wstring HotkeyToString(const HotkeyCombo & hk)
{
	// Empty when unset: no hint is displayed in the menu
	if (!hk.IsSet()) return L"";
	std::wstring s;
	if (hk.modifiers & MOD_CONTROL) s += L"Ctrl+";
	if (hk.modifiers & MOD_ALT) s += L"Alt+";
	if (hk.modifiers & MOD_SHIFT) s += L"Shift+";
	if (hk.modifiers & MOD_WIN) s += L"Win+";
	s += VkToWString(hk.vk);
	return s;
}

static std::string HotkeyToStringNarrow(const HotkeyCombo & hk)
{
	// Persistence format: "none" marks an unset hotkey
	std::wstring w = HotkeyToString(hk);
	if (w.empty()) return "none";
	return std::string(w.begin(), w.end());
}

static UINT StringToVk(const std::string & token)
{
	if (token.size() == 1) {
		char c = (char)toupper((unsigned char)token[0]);
		if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) return (UINT)c;
		switch (c) {
		case '`': return VK_OEM_3;
		case '-': return VK_OEM_MINUS;
		case ',': return VK_OEM_COMMA;
		case '.': return VK_OEM_PERIOD;
		case '/': return VK_OEM_2;
		case ';': return VK_OEM_1;
		case '\'': return VK_OEM_7;
		case '[': return VK_OEM_4;
		case '\\': return VK_OEM_5;
		case ']': return VK_OEM_6;
		}
		return 0;
	}
	auto ieq = [](const std::string & a, const char * b) {
		std::string t(b);
		if (a.size() != t.size()) return false;
		for (size_t i = 0; i < a.size(); i++)
			if (tolower((unsigned char)a[i]) != tolower((unsigned char)t[i])) return false;
		return true;
	};
	if (token.size() >= 2 && (token[0] == 'F' || token[0] == 'f')) {
		int n = atoi(token.c_str() + 1);
		if (n >= 1 && n <= 24) return VK_F1 + (UINT)(n - 1);
	}
	if (ieq(token, "Space")) return VK_SPACE;
	if (ieq(token, "Enter")) return VK_RETURN;
	if (ieq(token, "Tab")) return VK_TAB;
	if (ieq(token, "Backspace")) return VK_BACK;
	if (ieq(token, "Insert")) return VK_INSERT;
	if (ieq(token, "Delete")) return VK_DELETE;
	if (ieq(token, "Home")) return VK_HOME;
	if (ieq(token, "End")) return VK_END;
	if (ieq(token, "PgUp")) return VK_PRIOR;
	if (ieq(token, "PgDn")) return VK_NEXT;
	if (ieq(token, "Left")) return VK_LEFT;
	if (ieq(token, "Right")) return VK_RIGHT;
	if (ieq(token, "Up")) return VK_UP;
	if (ieq(token, "Down")) return VK_DOWN;
	if (ieq(token, "PrtSc")) return VK_SNAPSHOT;
	return 0;
}

static std::string TrimString(const std::string & s)
{
	size_t a = s.find_first_not_of(" \t\r\n");
	if (a == std::string::npos) return "";
	size_t b = s.find_last_not_of(" \t\r\n");
	return s.substr(a, b - a + 1);
}

// Parse "Ctrl+Shift+P" (or "none" / "") into a HotkeyCombo.
// Returns false if the string could not be parsed (combo left unset).
static bool ParseHotkeyString(const std::string & str, HotkeyCombo & out)
{
	out = HotkeyCombo();
	std::string s = TrimString(str);
	if (s.empty() || s == "none" || s == "None" || s == "NONE")
		return true; // valid: no hotkey

	UINT mods = 0;
	UINT vk = 0;
	size_t pos = 0;
	std::string last;
	int tokenCount = 0;
	while (pos <= s.size()) {
		size_t next = s.find('+', pos);
		std::string token = TrimString(s.substr(pos, (next == std::string::npos ? s.size() : next) - pos));
		if (!token.empty()) {
			tokenCount++;
			last = token;
		}
		if (next == std::string::npos) break;
		pos = next + 1;
	}
	if (tokenCount < 2) return false; // need at least modifier + key

	// All tokens except the last must be modifiers
	pos = 0;
	for (int i = 0; i < tokenCount - 1; i++) {
		size_t next = s.find('+', pos);
		std::string token = TrimString(s.substr(pos, next - pos));
		if (token == "Ctrl" || token == "ctrl" || token == "CTRL") mods |= MOD_CONTROL;
		else if (token == "Alt" || token == "alt" || token == "ALT") mods |= MOD_ALT;
		else if (token == "Shift" || token == "shift" || token == "SHIFT") mods |= MOD_SHIFT;
		else if (token == "Win" || token == "win" || token == "WIN") mods |= MOD_WIN;
		else return false;
		pos = next + 1;
	}

	vk = StringToVk(last);
	if (!vk) return false;
	out.modifiers = mods;
	out.vk = vk;
	return true;
}

//--------------------------------------------------------------
// Hotkey capture dialog
//--------------------------------------------------------------
static struct {
	HWND dlg = nullptr;
	int target = 0; // 1 = pause hotkey, 2 = screen hotkey
	std::wstring targetName;
} g_hotkeyCapture;

// DwmSetWindowAttribute, resolved once and kept loaded
typedef HRESULT(WINAPI * PFNDwmSetWindowAttribute)(HWND, DWORD, LPCVOID, DWORD);
static PFNDwmSetWindowAttribute GetDwmSetWindowAttribute()
{
	static PFNDwmSetWindowAttribute p = nullptr;
	static bool resolved = false;
	if (!resolved) {
		resolved = true;
		HMODULE dwm = GetModuleHandleW(L"dwmapi.dll");
		if (!dwm) dwm = LoadLibraryW(L"dwmapi.dll");
		if (dwm)
			p = (PFNDwmSetWindowAttribute)GetProcAddress(dwm, "DwmSetWindowAttribute");
		// note: the module is intentionally kept loaded
	}
	return p;
}

// Optional dark title bar and rounded corners (Win10 20H1+ / Win11)
static void TryDarkTitleBar(HWND h)
{
	auto p = GetDwmSetWindowAttribute();
	if (!p) return;
	BOOL dark = TRUE;
	// DWMWA_USE_IMMERSIVE_DARK_MODE = 20 (19 on older builds)
	if (FAILED(p(h, 20, &dark, sizeof(dark))))
		p(h, 19, &dark, sizeof(dark));
	INT pref = 2; // DWMWCP_ROUND
	p(h, 33 /* DWMWA_WINDOW_CORNER_PREFERENCE */, &pref, sizeof(pref));
}

static LRESULT CALLBACK HotkeyDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg) {
	case WM_ERASEBKGND:
		return 1; // painted in WM_PAINT
	case WM_PAINT:
	{
		PAINTSTRUCT ps;
		HDC dc = BeginPaint(hDlg, &ps);
		RECT rc;
		GetClientRect(hDlg, &rc);
		HBRUSH bg = CreateSolidBrush(MENU_BG);
		FillRect(dc, &rc, bg);
		DeleteObject(bg);
		SetBkMode(dc, TRANSPARENT);
		UINT dpi = GetWindowDPI(hDlg);
		HGDIOBJ oldFont = SelectObject(dc, GetMenuFont(dpi));
		int lh = ScaleByDpi(24, dpi);
		RECT line1 = { ScaleByDpi(16, dpi), ScaleByDpi(12, dpi), rc.right, rc.bottom };
		RECT line2 = line1; line2.top += lh; line2.bottom += lh;
		RECT line3 = line1; line3.top += lh * 2; line3.bottom += lh * 2;
		RECT line4 = line1; line4.top += lh * 3; line4.bottom += lh * 3;
		SetTextColor(dc, MENU_FG);
		DrawTextW(dc, (L"Set hotkey for: " + g_hotkeyCapture.targetName).c_str(), -1, &line1, DT_SINGLELINE);
		DrawTextW(dc, L"Press a key combination", -1, &line2, DT_SINGLELINE);
		SetTextColor(dc, MENU_GRAY);
		DrawTextW(dc, L"Must include Ctrl, Alt or Win", -1, &line3, DT_SINGLELINE);
		DrawTextW(dc, L"Esc = cancel      Backspace = clear binding", -1, &line4, DT_SINGLELINE);
		SelectObject(dc, oldFont);
		EndPaint(hDlg, &ps);
		return 0;
	}
	case WM_KEYDOWN:
	case WM_SYSKEYDOWN:
	{
		UINT vk = (UINT)wParam;
		if (vk == VK_ESCAPE) {
			DestroyWindow(hDlg);
			return 0;
		}
		if (vk == VK_BACK) { // clear the binding
			if (g_appInstance) {
				if (g_hotkeyCapture.target == 1) g_appInstance->ApplyPauseHotkey(0, 0);
				else if (g_hotkeyCapture.target == 2) g_appInstance->ApplyScreenHotkey(0, 0);
			}
			DestroyWindow(hDlg);
			return 0;
		}
		// Ignore lone modifier presses, wait for the actual key
		if (vk == VK_CONTROL || vk == VK_SHIFT || vk == VK_MENU || vk == VK_LWIN || vk == VK_RWIN)
			return 0;
		UINT mods = 0;
		if (GetKeyState(VK_CONTROL) & 0x8000) mods |= MOD_CONTROL;
		if (GetKeyState(VK_SHIFT) & 0x8000) mods |= MOD_SHIFT;
		if (GetKeyState(VK_MENU) & 0x8000) mods |= MOD_ALT;
		if ((GetKeyState(VK_LWIN) & 0x8000) || (GetKeyState(VK_RWIN) & 0x8000)) mods |= MOD_WIN;
		if (!(mods & (MOD_CONTROL | MOD_ALT | MOD_WIN))) {
			// A bare key would fire while typing in other applications
			MessageBeep(MB_ICONASTERISK);
			return 0;
		}
		bool ok = false;
		if (g_appInstance) {
			if (g_hotkeyCapture.target == 1) ok = g_appInstance->ApplyPauseHotkey(mods, vk);
			else if (g_hotkeyCapture.target == 2) ok = g_appInstance->ApplyScreenHotkey(mods, vk);
		}
		if (!ok) {
			MessageBoxW(hDlg,
				L"That key combination is already in use by another application.",
				L"Spout2OverlayHUD", MB_OK | MB_ICONWARNING);
		}
		DestroyWindow(hDlg);
		return 0;
	}
	case WM_CLOSE:
		DestroyWindow(hDlg);
		return 0;
	case WM_DESTROY:
		if (g_hotkeyCapture.dlg == hDlg) {
			g_hotkeyCapture.dlg = nullptr;
			g_hotkeyCapture.target = 0;
		}
		// Hotkeys were suspended while capturing, restore them
		if (g_appInstance) g_appInstance->RegisterAllHotkeys();
		return 0;
	}
	return DefWindowProcW(hDlg, msg, wParam, lParam);
}

static void OpenHotkeyCapture(int target, const wchar_t * targetName)
{
	// Only one capture dialog at a time
	if (g_hotkeyCapture.dlg) {
		SetForegroundWindow(g_hotkeyCapture.dlg);
		return;
	}

	static bool classRegistered = false;
	if (!classRegistered) {
		WNDCLASSW wc = {};
		wc.lpfnWndProc = HotkeyDlgProc;
		wc.hInstance = GetModuleHandleW(nullptr);
		wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
		wc.hbrBackground = nullptr;
		wc.lpszClassName = L"Spout2OverlayHUD_HotkeyDlg";
		if (!RegisterClassW(&wc)) return;
		classRegistered = true;
	}

	// Suspend hotkeys so they do not fire while the user presses the new combo
	if (g_appInstance) g_appInstance->UnregisterAllHotkeys();

	g_hotkeyCapture.target = target;
	g_hotkeyCapture.targetName = targetName;

	UINT dpi = GetWindowDPI(hwnd);
	int clientW = ScaleByDpi(380, dpi);
	int clientH = ScaleByDpi(120, dpi);
	RECT wr = { 0, 0, clientW, clientH };
	AdjustWindowRect(&wr, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, FALSE);
	int winW = wr.right - wr.left;
	int winH = wr.bottom - wr.top;

	// Center on the currently selected monitor
	int x = 0, y = 0;
	if (g_appInstance && g_appInstance->selectedMonitorIndex >= 0
		&& g_appInstance->selectedMonitorIndex < (int)g_appInstance->monitors.size()) {
		const RECT & mr = g_appInstance->monitors[g_appInstance->selectedMonitorIndex].rect;
		x = mr.left + ((mr.right - mr.left) - winW) / 2;
		y = mr.top + ((mr.bottom - mr.top) - winH) / 2;
	} else {
		x = (GetSystemMetrics(SM_CXSCREEN) - winW) / 2;
		y = (GetSystemMetrics(SM_CYSCREEN) - winH) / 2;
	}

	HWND hDlg = CreateWindowExW(
		WS_EX_TOPMOST,
		L"Spout2OverlayHUD_HotkeyDlg",
		L"Set Hotkey - Spout2OverlayHUD",
		WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
		x, y, winW, winH,
		hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
	if (!hDlg) {
		if (g_appInstance) g_appInstance->RegisterAllHotkeys();
		g_hotkeyCapture.target = 0;
		return;
	}

	TryDarkTitleBar(hDlg);
	g_hotkeyCapture.dlg = hDlg;
	ShowWindow(hDlg, SW_SHOW);
	UpdateWindow(hDlg);
	SetForegroundWindow(hDlg);
	SetFocus(hDlg);
}

//--------------------------------------------------------------
// Menu item tooltip (description shown while an item is highlighted)
//--------------------------------------------------------------
static HWND g_tipWnd = nullptr;
static std::wstring g_tipText;
static const wchar_t * kTipClassName = L"Spout2OverlayHUD_Tip";

static LRESULT CALLBACK TipWndProc(HWND h, UINT msg, WPARAM wParam, LPARAM lParam)
{
	if (msg == WM_ERASEBKGND)
		return 1; // painted in WM_PAINT
	if (msg == WM_PAINT) {
		PAINTSTRUCT ps;
		HDC dc = BeginPaint(h, &ps);
		RECT rc;
		GetClientRect(h, &rc);
		UINT dpi = g_menuDpi ? g_menuDpi : 96;

		HBRUSH bg = CreateSolidBrush(MENU_BG);
		FillRect(dc, &rc, bg);
		DeleteObject(bg);

		// 1px border matching the menu frame
		HPEN pen = CreatePen(PS_SOLID, 1, MENU_BORDER);
		HGDIOBJ oldPen = SelectObject(dc, pen);
		HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
		Rectangle(dc, 0, 0, rc.right, rc.bottom);
		SelectObject(dc, oldBrush);
		SelectObject(dc, oldPen);
		DeleteObject(pen);

		SetBkMode(dc, TRANSPARENT);
		SetTextColor(dc, MENU_FG);
		HGDIOBJ oldFont = SelectObject(dc, GetMenuFont(dpi));
		RECT tr = rc;
		tr.left += ScaleByDpi(8, dpi);
		tr.top += ScaleByDpi(4, dpi);
		tr.right -= ScaleByDpi(8, dpi);
		tr.bottom -= ScaleByDpi(4, dpi);
		DrawTextW(dc, g_tipText.c_str(), -1, &tr, DT_WORDBREAK | DT_NOPREFIX);
		SelectObject(dc, oldFont);

		EndPaint(h, &ps);
		return 0;
	}
	return DefWindowProcW(h, msg, wParam, lParam);
}

static void HideMenuTip()
{
	if (g_tipWnd && IsWindowVisible(g_tipWnd))
		ShowWindow(g_tipWnd, SW_HIDE);
}

// Show a small dark tooltip with the item description.
// TIP_SIDE    : beside the item (leaf items, which open no submenu)
// TIP_ABOVE   : above (or below) the whole menu. Used for submenu
//               parents: their submenu opens sideways and would cover
//               a side-placed tooltip when the menu is near a screen
//               edge (typical for tray menus, where Windows flips the
//               submenu to the left).
enum TipPlacement { TIP_SIDE, TIP_ABOVE };

static void ShowMenuTip(const std::wstring & text, const RECT & itemRect,
	TipPlacement place, HMENU hMenu = nullptr)
{
	if (text.empty()) {
		HideMenuTip();
		return;
	}

	// Lazy-create the tooltip window
	if (!g_tipWnd) {
		static bool registered = false;
		if (!registered) {
			WNDCLASSW wc = {};
			wc.lpfnWndProc = TipWndProc;
			wc.hInstance = GetModuleHandleW(nullptr);
			wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
			wc.lpszClassName = kTipClassName;
			RegisterClassW(&wc);
			registered = true;
		}
		g_tipWnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
			kTipClassName, L"", WS_POPUP, 0, 0, 100, 40,
			hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
		if (!g_tipWnd)
			return;
	}

	UINT dpi = g_menuDpi ? g_menuDpi : 96;

	// Measure the wrapped text
	HDC dc = GetDC(g_tipWnd);
	HGDIOBJ oldFont = SelectObject(dc, GetMenuFont(dpi));
	RECT calc = { 0, 0, ScaleByDpi(300, dpi), 0 };
	DrawTextW(dc, text.c_str(), -1, &calc, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
	SelectObject(dc, oldFont);
	ReleaseDC(g_tipWnd, dc);

	int w = (calc.right - calc.left) + ScaleByDpi(16, dpi) + 2;
	int h = (calc.bottom - calc.top) + ScaleByDpi(8, dpi) + 2;
	if (w < ScaleByDpi(40, dpi)) w = ScaleByDpi(40, dpi);

	HMONITOR mon = MonitorFromRect(&itemRect, MONITOR_DEFAULTTONEAREST);
	MONITORINFO mi = {};
	mi.cbSize = sizeof(mi);
	GetMonitorInfoW(mon, &mi);
	int gap = ScaleByDpi(8, dpi);

	int x = 0, y = 0;
	if (place == TIP_ABOVE) {
		// Vertical extent of the whole menu from its first/last items
		int menuTop = itemRect.top, menuBottom = itemRect.bottom;
		if (hMenu) {
			int n = GetMenuItemCount(hMenu);
			RECT r0, rN;
			if (n > 0 && GetMenuItemRect(hwnd, hMenu, 0, &r0)
				&& GetMenuItemRect(hwnd, hMenu, n - 1, &rN)) {
				menuTop = r0.top;
				menuBottom = rN.bottom;
			}
		}
		// Above the menu, horizontally aligned with the item
		x = itemRect.left;
		y = menuTop - h - gap;
		if (y < mi.rcWork.top) {
			// No room above: below the menu
			y = menuBottom + gap;
			if (y + h > mi.rcWork.bottom) y = mi.rcWork.top;
		}
	} else {
		// Beside the item, on the side that has room
		x = itemRect.right + gap;
		if (x + w > mi.rcWork.right)
			x = itemRect.left - w - gap; // flip to the other side
		y = (itemRect.top + itemRect.bottom - h) / 2;
	}
	if (x + w > mi.rcWork.right) x = mi.rcWork.right - w;
	if (x < mi.rcWork.left) x = mi.rcWork.left;
	if (y + h > mi.rcWork.bottom) y = mi.rcWork.bottom - h;
	if (y < mi.rcWork.top) y = mi.rcWork.top;

	g_tipText = text;
	SetWindowPos(g_tipWnd, HWND_TOPMOST, x, y, w, h, SWP_NOACTIVATE | SWP_SHOWWINDOW);
	// Rounded corners matching the menu
	{
		int rad = ScaleByDpi(10, dpi);
		if (rad * 2 > w) rad = w / 2;
		if (rad * 2 > h) rad = h / 2;
		HRGN rgn = CreateRoundRectRgn(0, 0, w + 1, h + 1, rad, rad);
		if (rgn) {
			if (SetWindowRgn(g_tipWnd, rgn, TRUE) == 0)
				DeleteObject(rgn);
		}
	}
	InvalidateRect(g_tipWnd, nullptr, FALSE);
	UpdateWindow(g_tipWnd);
}

static void DestroyMenuTip()
{
	if (g_tipWnd) {
		DestroyWindow(g_tipWnd);
		g_tipWnd = nullptr;
	}
}

//--------------------------------------------------------------
// Dark chrome for the system popup menu window (class #32768):
// dark grey border, rounded corners, no DWM flyout chrome (the
// DWM-drawn light contour is disabled via DWMWA_NCRENDERING_ENABLED
// = FALSE and a window region) and no system shadow (the SysShadow
// companion windows are neutralized).
//--------------------------------------------------------------
static HHOOK g_menuCbtHook = nullptr;
static HWINEVENTHOOK g_menuWinEventHook = nullptr;

// Forward declarations
static void KillMenuShadow(HWND h);
static void KillThreadShadows();

// DWM attributes for the menu window. The key calls are disabling
// DWM non-client rendering (attribute 1), which removes the DWM-drawn
// flyout chrome, and clearing the DWM border color (attribute 34 =
// DWMWA_COLOR_NONE), which removes the DWM 1px border around the
// window. Neither can be overdrawn from inside the window.
static void ApplyMenuDwmAttrs(HWND h)
{
	auto pDwm = GetDwmSetWindowAttribute();
	if (!pDwm) return;
	BOOL nc = FALSE;
	pDwm(h, 1 /* DWMWA_NCRENDERING_ENABLED */, &nc, sizeof(nc));
	BOOL dark = TRUE;
	if (FAILED(pDwm(h, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof(dark))))
		pDwm(h, 19, &dark, sizeof(dark));
	COLORREF border = 0xFFFFFFFE; // DWMWA_COLOR_NONE
	pDwm(h, 34 /* DWMWA_BORDER_COLOR */, &border, sizeof(border));
}

// Rounded window region applied to the menu window, inset past the
// non-client frame band: DWM draws its light border exactly there
// (on top of everything, and DWMWA_BORDER_COLOR is ignored for menu
// windows on many builds - verified). Clipping the band away with the
// region is the only reliable way to remove it. The visible window
// becomes the client rect: dark, rounded, borderless (the same look
// as the tooltips).
static void ApplyMenuRegion(HWND h)
{
	RECT wr, cr;
	if (!GetWindowRect(h, &wr) || !GetClientRect(h, &cr)) return;
	POINT org = { 0, 0 };
	ClientToScreen(h, &org);
	int W = wr.right - wr.left;
	int H = wr.bottom - wr.top;
	if (W <= 0 || H <= 0) return;

	// Non-client frame band (window-relative client origin)
	int x0 = org.x - wr.left;
	int y0 = org.y - wr.top;
	int cw = (int)(cr.right - cr.left);
	int ch = (int)(cr.bottom - cr.top);
	if (cw <= 0 || ch <= 0) return;

	UINT dpi = g_menuDpi ? g_menuDpi : 96;
	int rad = ScaleByDpi(12, dpi); // corner radius
	if (rad * 2 > cw) rad = cw / 2;
	if (rad * 2 > ch) rad = ch / 2;

	// +1 on right/bottom: region bounds are exclusive there
	HRGN rgn = CreateRoundRectRgn(x0, y0, x0 + cw + 1, y0 + ch + 1, rad, rad);
	if (rgn) {
		// On success the region becomes owned by the window.
		// bRedraw is FALSE: a region change with bRedraw=TRUE fires a
		// full invalidation into the middle of the menu show/fade
		// animation, and the menu's first complete item paint gets
		// swallowed (the menu then shows only the background erase
		// until an item is hovered). The region takes effect visually
		// without the forced redraw; the post-animation repaint below
		// guarantees the items are painted.
		if (SetWindowRgn(h, rgn, FALSE) == 0)
			DeleteObject(rgn);
	}
}

// Forced-repaint timer id for menu windows (distinct from the small ids
// used by the menu's internal timers, e.g. submenu show delay)
#define S2OH_MENU_REPAINT_TIMER 0x5217

static LRESULT CALLBACK DarkMenuWndProc(HWND h, UINT msg, WPARAM wParam, LPARAM lParam)
{
	WNDPROC orig = (WNDPROC)GetPropW(h, L"S2OHMenuProc");
	switch (msg) {
	case WM_SHOWWINDOW:
		if (wParam) {
			// Window is now visible with its final size: disable the DWM
			// flyout chrome and apply the rounded region
			ApplyMenuDwmAttrs(h);
			ApplyMenuRegion(h);
			// The system can re-show the shadow companion window after the
			// menu becomes visible: sweep again
			KillThreadShadows();
			// The menu show/fade animation can leave the window surface
			// with only the background erase and no items: on affected
			// systems the first complete item paint is swallowed and the
			// menu stays blank until an item is hovered (any later
			// invalidation repaints it correctly). Force one full repaint
			// right after the animation to close that gap.
			SetTimer(h, S2OH_MENU_REPAINT_TIMER, 150, NULL);
		}
		break;
	case WM_TIMER:
		if (wParam == S2OH_MENU_REPAINT_TIMER) {
			KillTimer(h, S2OH_MENU_REPAINT_TIMER);
			RedrawWindow(h, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW);
			return 0;
		}
		break;
	case WM_WINDOWPOSCHANGED:
		{
			// Menu resized (e.g. item stack recalculated): refresh the region
			WINDOWPOS * wp = (WINDOWPOS *)lParam;
			if (wp && !(wp->flags & SWP_NOSIZE))
				ApplyMenuRegion(h);
		}
		break;
	case WM_ERASEBKGND:
		// Fill the client dark so no light background shows through
		{
			HDC dc = (HDC)wParam;
			RECT rc;
			GetClientRect(h, &rc);
			HBRUSH br = CreateSolidBrush(MENU_BG);
			FillRect(dc, &rc, br);
			DeleteObject(br);
			return 1;
		}
	case WM_PRINT:
		// WM_PRINT renders the window into a caller-supplied DC: this is
		// how the menu fade animation captures its image. Pre-fill dark
		// so the light class brush never shows, then let the system draw
		// the frame and the items into the same DC. The frame band it
		// paints falls outside the visible region, so nothing else is
		// needed here - and nothing may touch the item area after the
		// system has drawn it.
		{
			HDC dc = (HDC)wParam;
			if (dc) {
				RECT rc;
				GetClipBox(dc, &rc);
				HBRUSH br = CreateSolidBrush(MENU_BG);
				FillRect(dc, &rc, br);
				DeleteObject(br);
			}
		}
		break;
	case WM_PRINTCLIENT:
		// Client-only capture (animation): dark background first, then
		// the system draws the items into the same DC
		{
			HDC dc = (HDC)wParam;
			if (dc) {
				RECT rc;
				GetClipBox(dc, &rc);
				HBRUSH br = CreateSolidBrush(MENU_BG);
				FillRect(dc, &rc, br);
				DeleteObject(br);
			}
		}
		break;
	case WM_NCDESTROY:
		// Window is being destroyed: restore and clean up
		if (orig) SetWindowLongPtrW(h, GWLP_WNDPROC, (LONG_PTR)orig);
		RemovePropW(h, L"S2OHMenuProc");
		break;
	}
	// All paint messages (WM_PAINT, WM_NCPAINT included) pass through
	// untouched: menus draw their items themselves, any interference
	// risks a blank menu.
	return orig ? CallWindowProcW(orig, h, msg, wParam, lParam)
				: DefWindowProcW(h, msg, wParam, lParam);
}

static void StyleMenuWindow(HWND h)
{
	if (GetPropW(h, L"S2OHMenuProc"))
		return; // already styled

	ApplyMenuDwmAttrs(h);

	// Replace the light COLOR_MENU class background with a dark brush.
	// The class is per-process, so this covers every menu window of
	// this application in every background-erasing path (paint, print,
	// animation capture). The brush stays alive for the process lifetime.
	static HBRUSH darkClassBrush = nullptr;
	if (!darkClassBrush) {
		darkClassBrush = CreateSolidBrush(MENU_BG);
		SetClassLongPtrW(h, GCLP_HBRBACKGROUND, (LONG_PTR)darkClassBrush);
	}

	// Always subclass: the menu border and margins are painted by the
	// system (non-client border, COLOR_MENU class brush) and DWM
	// attributes alone do not recolor them on every build. The
	// overrides in DarkMenuWndProc guarantee the dark look.
	WNDPROC orig = (WNDPROC)SetWindowLongPtrW(h, GWLP_WNDPROC, (LONG_PTR)DarkMenuWndProc);
	if (orig)
		SetPropW(h, L"S2OHMenuProc", (HANDLE)orig);
}

//--------------------------------------------------------------
// Menu shadow companion windows ("SysShadow"): the system creates a
// small layered window behind each popup menu to draw its shadow.
// Around a dark menu this renders as a light contour (white over the
// desktop, grey over dark content). Neutralize every SysShadow of
// this thread: fully transparent alpha plus an empty region, so it
// can never paint even if the system shows it again.
//--------------------------------------------------------------
static void KillMenuShadow(HWND h)
{
	SetLayeredWindowAttributes(h, 0, 0, LWA_ALPHA);
	HRGN empty = CreateRectRgn(0, 0, 0, 0);
	if (empty) {
		if (SetWindowRgn(h, empty, FALSE) == 0)
			DeleteObject(empty);
	}
	ShowWindow(h, SW_HIDE);
}

static BOOL CALLBACK KillShadowEnumProc(HWND h, LPARAM lParam)
{
	wchar_t cls[16] = {};
	if (GetClassNameW(h, cls, 16) && wcscmp(cls, L"SysShadow") == 0) {
		DWORD pid = 0;
		DWORD tid = GetWindowThreadProcessId(h, &pid);
		if (tid == GetCurrentThreadId())
			KillMenuShadow(h);
	}
	return TRUE;
}

static void KillThreadShadows()
{
	EnumWindows(KillShadowEnumProc, 0);
}

static LRESULT CALLBACK MenuCbtProc(int nCode, WPARAM wParam, LPARAM lParam)
{
	if (nCode == HCBT_CREATEWND) {
		HWND h = (HWND)wParam;
		wchar_t cls[16] = {};
		GetClassNameW(h, cls, 16);
		if (wcscmp(cls, L"#32768") == 0)
			StyleMenuWindow(h);
		else if (wcscmp(cls, L"SysShadow") == 0)
			KillMenuShadow(h);
	}
	return CallNextHookEx(g_menuCbtHook, nCode, wParam, lParam);
}

// Safety net: fires for every popup menu start with the menu window
// handle, in case the CBT hook missed the window creation
static void CALLBACK MenuWinEventProc(HWINEVENTHOOK hook, DWORD event, HWND h,
	LONG idObject, LONG idChild, DWORD dwEventThread, DWORD dwmsEventTime)
{
	if (event == EVENT_SYSTEM_MENUPOPUPSTART && h) {
		wchar_t cls[16] = {};
		GetClassNameW(h, cls, 16);
		if (wcscmp(cls, L"#32768") == 0) {
			StyleMenuWindow(h);
			// The shadow companion window is usually created just before
			// the menu becomes visible: sweep for it here as well
			KillThreadShadows();
		}
	}
}

//--------------------------------------------------------------
// Window procedure
//--------------------------------------------------------------
LRESULT CALLBACK WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
	// 1. REMOVE NATIVE OS BORDERS (Fixes the white top bar and right-side clipping)
	if (message == WM_NCCALCSIZE) {
		if (wParam == TRUE) {
			return 0; // Expands client area to fill 100% of the window
		}
	}

	// 2. CUSTOM RESIZE & DRAG REGIONS (Replaces the old WM_LBUTTONDOWN drag logic)
	if (message == WM_NCHITTEST) {
		LRESULT hit = originalWndProc ? CallWindowProc(originalWndProc, hWnd, message, wParam, lParam)
									  : DefWindowProc(hWnd, message, wParam, lParam);

		// Only apply custom hit-testing when in custom size mode
		if (g_appInstance && g_appInstance->isCustomSizeMode && hit == HTCLIENT) {
			POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
			ScreenToClient(hWnd, &pt);

			RECT rc;
			GetClientRect(hWnd, &rc);

			// Define the width of the invisible border used for grabbing/resizing (in pixels)
			const int resizeBorder = 8;

			// Check corners first (diagonal resizing)
			if (pt.x < resizeBorder && pt.y < resizeBorder) return HTTOPLEFT;
			if (pt.x >= rc.right - resizeBorder && pt.y < resizeBorder) return HTTOPRIGHT;
			if (pt.x < resizeBorder && pt.y >= rc.bottom - resizeBorder) return HTBOTTOMLEFT;
			if (pt.x >= rc.right - resizeBorder && pt.y >= rc.bottom - resizeBorder) return HTBOTTOMRIGHT;

			// Check edges (horizontal/vertical resizing)
			if (pt.x < resizeBorder) return HTLEFT;
			if (pt.x >= rc.right - resizeBorder) return HTRIGHT;
			if (pt.y < resizeBorder) return HTTOP;
			if (pt.y >= rc.bottom - resizeBorder) return HTBOTTOM;

			// If the mouse is anywhere else inside the window, treat it as the title bar (makes it draggable)
			return HTCAPTION;
		}
		return hit;
	}

	// Update the window draw while in custom size mode and the window changes posizion/scale
	if (message == WM_WINDOWPOSCHANGED) {
		LRESULT result = originalWndProc ? CallWindowProc(originalWndProc, hWnd, message, wParam, lParam)
										 : DefWindowProc(hWnd, message, wParam, lParam);
		if (g_appInstance && g_appInstance->isCustomSizeMode) {
			WINDOWPOS * pos = (WINDOWPOS *)lParam;

			// Only redraw if the window size actually changed or if it was moved
			if (!(pos->flags & SWP_NOSIZE) || !(pos->flags & SWP_NOMOVE)) {

				ofSetupScreen();
				g_appInstance->update();
				g_appInstance->draw();

				GLFWwindow * glfwWindow = glfwGetCurrentContext();
				if (glfwWindow) {
					glfwSwapBuffers(glfwWindow);
				}
			}
		}
		return result; // do not call the original wndproc twice
	}

	// Handle key presses for custom size mode
	if (message == WM_KEYDOWN) {
		if (g_appInstance && g_appInstance->isCustomSizeMode) {
			if (wParam == VK_RETURN) {
				g_appInstance->ExitCustomSizeMode(true);
				return 0;
			} else if (wParam == VK_ESCAPE) {
				g_appInstance->ExitCustomSizeMode(false);
				return 0;
			}
		}
	}

	// Owner-drawn dark menu: measure and paint
	if (message == WM_MEASUREITEM) {
		LPMEASUREITEMSTRUCT mis = (LPMEASUREITEMSTRUCT)lParam;
		if (mis && mis->CtlType == ODT_MENU) {
			MeasureTrayMenuItem(hWnd, mis);
			return TRUE;
		}
	}
	if (message == WM_DRAWITEM) {
		LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)lParam;
		if (dis && dis->CtlType == ODT_MENU) {
			DrawTrayMenuItem(hWnd, dis);
			return TRUE;
		}
	}

	// Global hotkeys
	if (message == WM_HOTKEY) {
		if (wParam == HK_PAUSE && g_appInstance) {
			g_appInstance->TogglePause();
			return 0;
		} else if (wParam == HK_SCREEN && g_appInstance) {
			g_appInstance->CycleScreen();
			return 0;
		}
	}

	// Menu item descriptions: show/update the tooltip while an item
	// is highlighted during menu tracking
	if (message == WM_MENUSELECT) {
		UINT flags = HIWORD(wParam);
		HMENU hMenu = (HMENU)lParam;

		if (hMenu == nullptr || LOWORD(wParam) == 0xFFFF) {
			HideMenuTip(); // menu dismissed
		} else if (flags & MF_POPUP) {
			// Popup item: LOWORD(wParam) is the item position in the
			// containing menu. Depending on the Windows version lParam
			// is the containing menu or the submenu itself, so try both.
			const MenuItemData * found = nullptr;
			HMENU srcMenu = nullptr;
			UINT pos = LOWORD(wParam);
			MENUITEMINFOW mii = {};
			mii.cbSize = sizeof(mii);
			mii.fMask = MIIM_DATA;
			if (GetMenuItemInfoW(hMenu, pos, TRUE, &mii)) {
				auto it = g_menuItems.find((UINT_PTR)mii.dwItemData);
				if (it != g_menuItems.end() && it->second.hasSubmenu) {
					found = &it->second;
					srcMenu = hMenu;
				}
			}
			if (!found && g_topMenu && g_topMenu != hMenu
				&& GetMenuItemInfoW(g_topMenu, pos, TRUE, &mii)) {
				auto it = g_menuItems.find((UINT_PTR)mii.dwItemData);
				if (it != g_menuItems.end() && it->second.hasSubmenu) {
					found = &it->second;
					srcMenu = g_topMenu;
				}
			}
			RECT itemRect = {};
			if (found && !found->description.empty() && srcMenu
				&& GetMenuItemRect(hWnd, srcMenu, pos, &itemRect)) {
				// Above the menu: the submenu opens sideways and would
				// cover a side-placed tooltip near the screen edge
				ShowMenuTip(found->description, itemRect, TIP_ABOVE, srcMenu);
			} else {
				HideMenuTip();
			}
		} else if (flags & (MF_SEPARATOR | MF_SYSMENU)) {
			HideMenuTip();
		} else {
			UINT id = LOWORD(wParam);
			auto it = g_menuItems.find(id);
			if (it != g_menuItems.end() && !it->second.description.empty()) {
				// Find the item index to get its screen rect
				int idx = -1;
				int n = GetMenuItemCount(hMenu);
				for (int i = 0; i < n; i++) {
					if ((UINT)GetMenuItemID(hMenu, i) == id) { idx = i; break; }
				}
				RECT itemRect = {};
				if (idx >= 0 && GetMenuItemRect(hWnd, hMenu, idx, &itemRect))
					ShowMenuTip(it->second.description, itemRect, TIP_SIDE);
				else
					HideMenuTip();
			} else {
				HideMenuTip();
			}
		}
		// fall through to the original window procedure
	}

	if (message == WM_USER + 1) { // System tray event
		if (LOWORD(lParam) == WM_RBUTTONDOWN || LOWORD(lParam) == WM_CONTEXTMENU) { // Right Click
			if (!g_appInstance) return 0;

			// Re-enumerate monitors so hot-plug changes are reflected immediately
			g_appInstance->EnumerateMonitors();

			// Create the menu
			g_menuItems.clear();
			g_nextSpecialMenuId = IDM_SPECIAL_BASE + 100;
			HMENU hMenu = CreatePopupMenu();

			AddMenuHeader(hMenu, L"Spout2OverlayHUD by Marsic1 V1.0");
			AddMenuSeparator(hMenu);

			AddMenuItem(hMenu, IDM_PAUSE,
				g_appInstance->isPaused ? L"Resume Overlay" : L"Pause Overlay",
				false, HotkeyToString(g_appInstance->hotkeyPause),
				g_appInstance->isPaused
					? L"Show the overlay again and resume receiving"
					: L"Hide the overlay and idle the app");
			AddMenuItem(hMenu, IDM_SMOOTH, L"Smooth Mode", g_appInstance->smoothMode, L"",
				L"Refreshes at twice the monitor rate so no sender frame is skipped. Slightly higher GPU usage");

			AddMenuSeparator(hMenu);

			AddMenuItem(hMenu, IDM_SELECTSOURCE, L"Select Source", false, L"",
				L"Choose which Spout2 sender to display");

			// Screen selection submenu (real monitor names + resolution)
			if (!g_appInstance->monitors.empty()) {
				HMENU hScreenMenu = CreatePopupMenu();
				for (size_t i = 0; i < g_appInstance->monitors.size(); i++) {
					AddMenuItem(hScreenMenu, IDM_SCREEN_BASE + (UINT)i,
						g_appInstance->monitors[i].label,
						((int)i == g_appInstance->selectedMonitorIndex), L"",
						L"Show the overlay on this monitor");
				}
				AddMenuPopupItem(hMenu, L"Select Screen", hScreenMenu,
					L"Move the overlay to another monitor. Size and position are remembered per screen");
			}

			// Hotkeys submenu
			{
				HMENU hKeyMenu = CreatePopupMenu();
				AddMenuItem(hKeyMenu, IDM_HOTKEY_PAUSE_SET, L"Pause Overlay Hotkey",
					false, HotkeyToString(g_appInstance->hotkeyPause),
					L"Click to record a new key combination");
				AddMenuItem(hKeyMenu, IDM_HOTKEY_SCREEN_SET, L"Switch Screen Hotkey",
					false, HotkeyToString(g_appInstance->hotkeyScreen),
					L"Click to record a new key combination");
				AddMenuSeparator(hKeyMenu);
				AddMenuItem(hKeyMenu, IDM_HOTKEY_CLEAR, L"Clear All Hotkeys", false, L"",
					L"Remove both hotkey bindings");
				AddMenuPopupItem(hMenu, L"Hotkeys", hKeyMenu,
					L"Set global key combinations for pause and screen switching");
			}

			AddMenuSeparator(hMenu);

			AddMenuItem(hMenu, IDM_CUSTOMSIZE, L"Custom Size", false, L"",
				L"Move and resize the overlay freely. Enter saves, Esc cancels");
			AddMenuItem(hMenu, IDM_FITTOSCREEN, L"Fit to Screen", false, L"",
				L"Reset the overlay to cover the whole screen");

			AddMenuSeparator(hMenu);

			AddMenuItem(hMenu, IDM_ABOUT, L"About", false, L"",
				L"Open the project page on GitHub");
			AddMenuItem(hMenu, IDM_EXIT, L"Exit", false, L"",
				L"Close Spout2OverlayHUD");

			// Get cursor position
			POINT cursor;
			GetCursorPos(&cursor);

			// Size the menu for the monitor where it will appear,
			// independent of where the overlay window sits
			g_menuDpi = GetCursorMonitorDPI();
			g_topMenu = hMenu;

			// Show the menu in the position of the mouse.
			// A CBT hook styles the popup window while it is shown
			// (dark border, rounded corners) and WM_MENUSELECT drives
			// the item description tooltips.
			SetForegroundWindow(hWnd);
			g_menuCbtHook = SetWindowsHookExW(WH_CBT, MenuCbtProc, nullptr, GetCurrentThreadId());
			int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_RIGHTBUTTON, cursor.x, cursor.y, 0, hWnd, NULL);
			if (g_menuCbtHook) {
				UnhookWindowsHookEx(g_menuCbtHook);
				g_menuCbtHook = nullptr;
			}
			HideMenuTip();
			g_topMenu = nullptr;
			DestroyMenu(hMenu);
			PostMessageW(hWnd, WM_NULL, 0, 0); // required so the menu dismisses when clicking elsewhere

			switch (cmd) {
			case IDM_SELECTSOURCE:
				// Select Source
				// The pin is kept: the Spout SDK adopts the user's panel
				// selection on the next receive (CheckSpoutPanel), after
				// which update() re-pins to the newly selected sender.
				if (g_appInstance) {
					g_appInstance->receiver.SelectSender(ofGetWin32Window());
					//Restore TopMost Position after changing source
					SetWindowPos(hwnd, HWND_TOPMOST, NULL, NULL, NULL, NULL, SWP_NOMOVE | SWP_NOSIZE);
				}
				break;
			case IDM_ABOUT:
				// Open Git
				if (g_appInstance) {
					g_appInstance->launchGit();
				}
				break;
			case IDM_EXIT:
				// Exit
				PostQuitMessage(0);
				break;
			case IDM_PAUSE:
				// Pause/Resume
				if (g_appInstance) {
					g_appInstance->TogglePause();
				}
				break;
			case IDM_SMOOTH:
				// Smooth Mode toggle
				if (g_appInstance) {
					g_appInstance->SetSmoothMode(!g_appInstance->smoothMode);
				}
				break;
			case IDM_CUSTOMSIZE:
				// Custom Size
				if (g_appInstance) {
					g_appInstance->EnterCustomSizeMode();
				}
				break;
			case IDM_FITTOSCREEN:
				// Fit to Screen
				if (g_appInstance) {
					g_appInstance->FitToScreen();
				}
				break;
			case IDM_HOTKEY_PAUSE_SET:
				OpenHotkeyCapture(1, L"Pause Overlay");
				break;
			case IDM_HOTKEY_SCREEN_SET:
				OpenHotkeyCapture(2, L"Switch Screen");
				break;
			case IDM_HOTKEY_CLEAR:
				if (g_appInstance) {
					g_appInstance->ClearAllHotkeys();
				}
				break;
			default:
				// Check if it's a screen selection (100-199)
				if (cmd >= IDM_SCREEN_BASE && cmd < IDM_SCREEN_BASE + 100) {
					int screenIdx = cmd - IDM_SCREEN_BASE;
					if (g_appInstance) {
						g_appInstance->SelectScreen(screenIdx);
						SetWindowPos(hwnd, HWND_TOPMOST, NULL, NULL, NULL, NULL, SWP_NOMOVE | SWP_NOSIZE);
					}
				}
				break;
			}

			g_menuItems.clear(); // menu dismissed, safe to free item data
			return 0;
		}
	}
	if (originalWndProc) {
		return CallWindowProc(originalWndProc, hWnd, message, wParam, lParam);
	}
	return DefWindowProc(hWnd, message, wParam, lParam);
}

//--------------------------------------------------------------
// Monitor enumeration
//--------------------------------------------------------------
static std::wstring GetMonitorFriendlyName(const std::wstring & deviceName)
{
	// Primary: QueryDisplayConfig gives the monitor name from EDID
	UINT32 pathCount = 0, modeCount = 0;
	if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount) == ERROR_SUCCESS && pathCount > 0) {
		std::vector<DISPLAYCONFIG_PATH_INFO> paths(pathCount);
		std::vector<DISPLAYCONFIG_MODE_INFO> modes(modeCount);
		if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pathCount, paths.data(), &modeCount, modes.data(), NULL) == ERROR_SUCCESS) {
			for (UINT32 i = 0; i < pathCount; i++) {
				DISPLAYCONFIG_SOURCE_DEVICE_NAME source = {};
				source.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
				source.header.size = sizeof(source);
				source.header.adapterId = paths[i].sourceInfo.adapterId;
				source.header.id = paths[i].sourceInfo.id;
				if (DisplayConfigGetDeviceInfo(&source.header) == ERROR_SUCCESS
					&& _wcsicmp(source.viewGdiDeviceName, deviceName.c_str()) == 0) {
					DISPLAYCONFIG_TARGET_DEVICE_NAME target = {};
					target.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
					target.header.size = sizeof(target);
					target.header.adapterId = paths[i].targetInfo.adapterId;
					target.header.id = paths[i].targetInfo.id;
					if (DisplayConfigGetDeviceInfo(&target.header) == ERROR_SUCCESS
						&& target.monitorFriendlyDeviceName[0]) {
						return target.monitorFriendlyDeviceName;
					}
				}
			}
		}
	}

	// Fallback: EnumDisplayDevices monitor description
	DISPLAY_DEVICEW dd = {};
	dd.cb = sizeof(dd);
	for (DWORD i = 0; EnumDisplayDevicesW(NULL, i, &dd, 0); i++) {
		if (_wcsicmp(dd.DeviceName, deviceName.c_str()) == 0) {
			DISPLAY_DEVICEW mon = {};
			mon.cb = sizeof(mon);
			if (EnumDisplayDevicesW(dd.DeviceName, 0, &mon, 0) && mon.DeviceString[0])
				return mon.DeviceString;
			break;
		}
	}
	return L"";
}

// Monitor enumeration callback
BOOL CALLBACK MonitorEnumProc(HMONITOR hMonitor, HDC hdcMonitor, LPRECT lprcMonitor, LPARAM dwData) {
	std::vector<MonitorInfo>* pMonitors = reinterpret_cast<std::vector<MonitorInfo>*>(dwData);

	MONITORINFOEXW info;
	info.cbSize = sizeof(MONITORINFOEXW);
	if (GetMonitorInfoW(hMonitor, &info)) {
		MonitorInfo monitor;
		monitor.handle = hMonitor;
		monitor.rect = info.rcMonitor;
		monitor.name = info.szDevice; // szDevice is already wide string

		pMonitors->push_back(monitor);
	}

	return TRUE;
}

//--------------------------------------------------------------
void ofApp::EnumerateMonitors() {
	// Remember the currently selected monitor's device name
	std::wstring prevName;
	if (selectedMonitorIndex >= 0 && selectedMonitorIndex < (int)monitors.size()) {
		prevName = monitors[selectedMonitorIndex].name;
	}

	monitors.clear();
	EnumDisplayMonitors(NULL, NULL, MonitorEnumProc, (LPARAM)&monitors);

	// If no monitors found, use primary screen
	if (monitors.empty()) {
		MonitorInfo primary;
		primary.handle = MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY);
		primary.name = L"Primary Display";
		primary.rect.left = 0;
		primary.rect.top = 0;
		primary.rect.right = ofGetScreenWidth();
		primary.rect.bottom = ofGetScreenHeight();
		monitors.push_back(primary);
	}

	// Resolve friendly names and build menu labels
	for (size_t i = 0; i < monitors.size(); i++) {
		MonitorInfo & m = monitors[i];
		m.friendlyName = GetMonitorFriendlyName(m.name);
		if (m.friendlyName.empty())
			m.friendlyName = L"Monitor " + std::to_wstring(i + 1);
		int w = m.rect.right - m.rect.left;
		int h = m.rect.bottom - m.rect.top;
		m.label = m.friendlyName + L" (" + std::to_wstring(w) + L"x" + std::to_wstring(h) + L")";
	}

	// Preserve selection by device name
	selectedMonitorIndex = 0;
	for (int i = 0; i < (int)monitors.size(); i++) {
		if (monitors[i].name == prevName) {
			selectedMonitorIndex = i;
			break;
		}
	}

	// Keep the primary monitor first so the first run (no saved
	// preference yet) lands on the expected screen
	for (size_t i = 1; i < monitors.size(); i++) {
		MONITORINFO mi = {};
		mi.cbSize = sizeof(mi);
		if (GetMonitorInfoW(monitors[i].handle, &mi)
			&& (mi.dwFlags & MONITORINFOF_PRIMARY)) {
			std::swap(monitors[0], monitors[i]);
			if (selectedMonitorIndex == (int)i)
				selectedMonitorIndex = 0;
			else if (selectedMonitorIndex == 0)
				selectedMonitorIndex = (int)i;
			break;
		}
	}
}

//--------------------------------------------------------------
int ofApp::GetSelectedMonitorHz() const {
	if (selectedMonitorIndex < 0 || selectedMonitorIndex >= (int)monitors.size())
		return 60;
	DEVMODEW dm = {};
	dm.dmSize = sizeof(dm);
	if (EnumDisplaySettingsW(monitors[selectedMonitorIndex].name.c_str(), ENUM_CURRENT_SETTINGS, &dm)) {
		if (dm.dmDisplayFrequency >= 30 && dm.dmDisplayFrequency <= 500)
			return (int)dm.dmDisplayFrequency;
	}
	return 60;
}

//--------------------------------------------------------------
void ofApp::ApplyScreenSettings(int monitorIndex) {
	if (monitorIndex < 0 || monitorIndex >= monitors.size()) {
		return;
	}

	const MonitorInfo& monitor = monitors[monitorIndex];
	int screenW = monitor.rect.right - monitor.rect.left;
	int screenH = monitor.rect.bottom - monitor.rect.top;

	// Set screen size with -2 pixel fix
	ofSetWindowShape(screenW - 2, screenH - 2);

	// Center on the selected screen
	int centerX = monitor.rect.left + (screenW - ofGetWidth()) / 2;
	int centerY = monitor.rect.top + (screenH - ofGetHeight()) / 2;
	ofSetWindowPosition(centerX, centerY);
}

//--------------------------------------------------------------
// Per-screen window geometry: the size/position used on each
// monitor is remembered separately in the config file.
//--------------------------------------------------------------
static std::string SanitizeMonitorKey(const std::wstring & deviceName)
{
	// "\\\\.\\DISPLAY1" -> "DISPLAY1"
	std::string out;
	for (wchar_t c : deviceName) {
		if ((c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9'))
			out += (char)c;
	}
	if (out.empty()) out = "Primary";
	return out;
}

//--------------------------------------------------------------
int ofApp::GetCurrentMonitorIndex() {
	HWND hw = ofGetWin32Window();
	if (!hw) return selectedMonitorIndex;
	HMONITOR hm = MonitorFromWindow(hw, MONITOR_DEFAULTTONEAREST);
	for (size_t i = 0; i < monitors.size(); i++) {
		if (monitors[i].handle == hm) return (int)i;
	}
	// Fallback: monitor with the largest overlap
	RECT wr;
	if (GetWindowRect(hw, &wr)) {
		int best = selectedMonitorIndex;
		long bestArea = -1;
		for (size_t i = 0; i < monitors.size(); i++) {
			RECT inter;
			if (IntersectRect(&inter, &monitors[i].rect, &wr)) {
				long area = (long)(inter.right - inter.left) * (inter.bottom - inter.top);
				if (area > bestArea) { bestArea = area; best = (int)i; }
			}
		}
		return best;
	}
	return selectedMonitorIndex;
}

//--------------------------------------------------------------
bool ofApp::LoadWindowRectForMonitor(int monitorIndex) {
	if (monitorIndex < 0 || monitorIndex >= (int)monitors.size())
		return false;

	std::string key = "ScreenWin_" + SanitizeMonitorKey(monitors[monitorIndex].name);
	std::string value;
	if (!ReadConfigValue(key, value) || value.empty())
		return false;

	int x = 0, y = 0, w = 0, h = 0;
	if (sscanf_s(value.c_str(), "%d,%d,%d,%d", &x, &y, &w, &h) != 4)
		return false;
	if (w <= 50 || h <= 50)
		return false;

	// The saved geometry must belong to that monitor
	RECT saved = { x, y, x + w, y + h };
	RECT inter;
	if (!IntersectRect(&inter, &monitors[monitorIndex].rect, &saved))
		return false;

	ofSetWindowShape(w, h);
	ofSetWindowPosition(x, y);
	return true;
}

//--------------------------------------------------------------
void ofApp::SelectScreen(int monitorIndex) {
	if (monitorIndex < 0 || monitorIndex >= monitors.size()) {
		return;
	}

	// Remember the size/position used on the screen we are leaving
	int current = GetCurrentMonitorIndex();
	if (current != monitorIndex)
		SaveWindowPosition();

	selectedMonitorIndex = monitorIndex;

	// Restore remembered geometry for the new screen, or fit to
	// screen when selecting it for the first time
	if (!LoadWindowRectForMonitor(monitorIndex)) {
		ApplyScreenSettings(monitorIndex);
		SaveWindowPosition(); // remember the fitted geometry for this screen
	}

	SaveScreenPreference();

	// The refresh rate may differ per monitor
	ApplyFrameRateSettings();
}

//--------------------------------------------------------------
void ofApp::CycleScreen() {
	if (monitors.empty()) return;
	SelectScreen((selectedMonitorIndex + 1) % (int)monitors.size());
	SetWindowPos(hwnd, HWND_TOPMOST, NULL, NULL, NULL, NULL, SWP_NOMOVE | SWP_NOSIZE);
}

//--------------------------------------------------------------
// Config helpers
//--------------------------------------------------------------
static int SafeStoi(const std::string & s, int def)
{
	try {
		return std::stoi(TrimString(s));
	} catch (...) {
		return def;
	}
}

std::string ofApp::GetConfigPath() {
	static std::string cachedPath;
	if (!cachedPath.empty()) return cachedPath;

	char appDataPath[MAX_PATH];
	if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, appDataPath))) {
		std::string dir = std::string(appDataPath) + "\\Spout2OverlayHUD";
		CreateDirectoryA(dir.c_str(), NULL);
		cachedPath = dir + "\\config.ini";
	}
	return cachedPath;
}

bool ofApp::ReadConfigValue(const std::string & key, std::string & value) {
	value.clear();
	std::string path = GetConfigPath();
	if (path.empty()) return false;

	std::ifstream configFile(path);
	if (!configFile.is_open()) return false;

	std::string line;
	std::string prefix = key + "=";
	while (std::getline(configFile, line)) {
		if (!line.empty() && line.back() == '\r') line.pop_back();
		if (line.compare(0, prefix.size(), prefix) == 0) {
			value = line.substr(prefix.size());
			return true;
		}
	}
	return false;
}

void ofApp::WriteConfigValue(const std::string & key, const std::string & value) {
	std::string path = GetConfigPath();
	if (path.empty()) return;

	// Read existing content, dropping any previous value of this key
	std::vector<std::string> lines;
	{
		std::ifstream inFile(path);
		std::string line;
		std::string prefix = key + "=";
		while (std::getline(inFile, line)) {
			if (!line.empty() && line.back() == '\r') line.pop_back();
			if (line.compare(0, prefix.size(), prefix) == 0) continue;
			lines.push_back(line);
		}
	}

	lines.push_back(key + "=" + value);

	std::ofstream outFile(path, std::ios::trunc);
	if (outFile.is_open()) {
		for (size_t i = 0; i < lines.size(); i++) {
			outFile << lines[i] << "\n";
		}
	}
}

//--------------------------------------------------------------
void ofApp::SaveScreenPreference() {
	if (selectedMonitorIndex < 0 || selectedMonitorIndex >= (int)monitors.size()) {
		return;
	}
	// Persist by device name (stable when Windows reorders monitors)
	const std::wstring & name = monitors[selectedMonitorIndex].name;
	WriteConfigValue("SelectedMonitorName", std::string(name.begin(), name.end()));
	// Also keep the legacy index for backward compatibility
	WriteConfigValue("SelectedMonitor", std::to_string(selectedMonitorIndex));
}

//--------------------------------------------------------------
void ofApp::LoadScreenPreference() {
	std::string value;
	if (ReadConfigValue("SelectedMonitorName", value) && !value.empty()) {
		std::wstring name(value.begin(), value.end());
		for (int i = 0; i < (int)monitors.size(); i++) {
			if (_wcsicmp(monitors[i].name.c_str(), name.c_str()) == 0) {
				selectedMonitorIndex = i;
				return;
			}
		}
	}
	// Legacy fallback: monitor index
	if (ReadConfigValue("SelectedMonitor", value)) {
		int idx = SafeStoi(value, 0);
		if (idx >= 0 && idx < (int)monitors.size()) {
			selectedMonitorIndex = idx;
		}
	}
}

//--------------------------------------------------------------
// Pause / Smooth Mode / frame pacing
//--------------------------------------------------------------
void ofApp::TogglePause() {
	isPaused = !isPaused;
	ApplyFrameRateSettings();
	UpdateTrayIcon();
}

//--------------------------------------------------------------
void ofApp::SetSmoothMode(bool enabled) {
	if (smoothMode == enabled) return;
	smoothMode = enabled;
	WriteConfigValue("SmoothMode", enabled ? "1" : "0");
	ApplyFrameRateSettings();
}

//--------------------------------------------------------------
void ofApp::ApplyFrameRateSettings() {
	if (isPaused) {
		// True pause: the loop idles at vblank pace presenting an empty
		// (fully transparent) window. Near-zero cost, tray and hotkeys
		// stay responsive.
		ofSetFrameRate(0);
		ofSetVerticalSync(true);
	}
	else if (idleMode) {
		// No sender for a while: low rate polling until it returns
		ofSetFrameRate(10);
		ofSetVerticalSync(true);
	}
	else if (smoothMode) {
		// Smooth Mode: poll the sender at twice the monitor refresh so
		// every sender frame is caught regardless of timing alignment.
		int target = GetSelectedMonitorHz() * 2;
		if (target < 120) target = 120;
		if (target > 480) target = 480;
		ofSetFrameRate(target);
		ofSetVerticalSync(false);
	}
	else {
		// Vsync mode: paced by the monitor refresh
		ofSetFrameRate(0);
		ofSetVerticalSync(true);
	}
}

//--------------------------------------------------------------
// Hotkeys
//--------------------------------------------------------------
void ofApp::LoadHotkeys() {
	std::string value;
	if (ReadConfigValue("HotkeyPause", value)) {
		if (!ParseHotkeyString(value, hotkeyPause))
			hotkeyPause = HotkeyCombo(); // unparsable entry: treat as unset
	}
	if (ReadConfigValue("HotkeyScreen", value)) {
		if (!ParseHotkeyString(value, hotkeyScreen))
			hotkeyScreen = HotkeyCombo();
	}
}

//--------------------------------------------------------------
void ofApp::SaveHotkeys() {
	WriteConfigValue("HotkeyPause", HotkeyToStringNarrow(hotkeyPause));
	WriteConfigValue("HotkeyScreen", HotkeyToStringNarrow(hotkeyScreen));
}

//--------------------------------------------------------------
void ofApp::UnregisterAllHotkeys() {
	UnregisterHotKey(hwnd, HK_PAUSE);
	UnregisterHotKey(hwnd, HK_SCREEN);
}

//--------------------------------------------------------------
void ofApp::RegisterAllHotkeys() {
	UnregisterAllHotkeys();
	if (hotkeyPause.IsSet()) {
		if (!RegisterHotKey(hwnd, HK_PAUSE, hotkeyPause.modifiers | MOD_NOREPEAT, hotkeyPause.vk)) {
			MessageBoxW(hwnd, L"Could not register the Pause Overlay hotkey.\n"
				L"The combination may already be in use by another application.",
				L"Spout2OverlayHUD", MB_OK | MB_ICONWARNING);
		}
	}
	if (hotkeyScreen.IsSet()) {
		if (!RegisterHotKey(hwnd, HK_SCREEN, hotkeyScreen.modifiers | MOD_NOREPEAT, hotkeyScreen.vk)) {
			MessageBoxW(hwnd, L"Could not register the Switch Screen hotkey.\n"
				L"The combination may already be in use by another application.",
				L"Spout2OverlayHUD", MB_OK | MB_ICONWARNING);
		}
	}
}

//--------------------------------------------------------------
bool ofApp::ApplyPauseHotkey(UINT modifiers, UINT vk) {
	UnregisterHotKey(hwnd, HK_PAUSE);
	if (vk == 0) { // clear
		hotkeyPause = HotkeyCombo();
		SaveHotkeys();
		return true;
	}
	if (RegisterHotKey(hwnd, HK_PAUSE, modifiers | MOD_NOREPEAT, vk)) {
		hotkeyPause.modifiers = modifiers;
		hotkeyPause.vk = vk;
		SaveHotkeys();
		return true;
	}
	// Failed: restore the previous binding
	if (hotkeyPause.IsSet())
		RegisterHotKey(hwnd, HK_PAUSE, hotkeyPause.modifiers | MOD_NOREPEAT, hotkeyPause.vk);
	return false;
}

//--------------------------------------------------------------
bool ofApp::ApplyScreenHotkey(UINT modifiers, UINT vk) {
	UnregisterHotKey(hwnd, HK_SCREEN);
	if (vk == 0) { // clear
		hotkeyScreen = HotkeyCombo();
		SaveHotkeys();
		return true;
	}
	if (RegisterHotKey(hwnd, HK_SCREEN, modifiers | MOD_NOREPEAT, vk)) {
		hotkeyScreen.modifiers = modifiers;
		hotkeyScreen.vk = vk;
		SaveHotkeys();
		return true;
	}
	// Failed: restore the previous binding
	if (hotkeyScreen.IsSet())
		RegisterHotKey(hwnd, HK_SCREEN, hotkeyScreen.modifiers | MOD_NOREPEAT, hotkeyScreen.vk);
	return false;
}

//--------------------------------------------------------------
void ofApp::ClearAllHotkeys() {
	UnregisterAllHotkeys();
	hotkeyPause = HotkeyCombo();
	hotkeyScreen = HotkeyCombo();
	SaveHotkeys();
}

//--------------------------------------------------------------
// Tray icon states
//--------------------------------------------------------------
// Build a copy of the base icon with white pause bars (black outline)
static HICON CreatePauseIcon(HICON base)
{
	if (!base) return nullptr;

	ICONINFO ii = {};
	if (!GetIconInfo(base, &ii)) return nullptr;

	int w = 32, h = 32;
	BITMAP bm = {};
	if (ii.hbmColor) {
		GetObjectW(ii.hbmColor, sizeof(bm), &bm);
		w = bm.bmWidth; h = bm.bmHeight;
	}
	else if (ii.hbmMask) {
		GetObjectW(ii.hbmMask, sizeof(bm), &bm);
		w = bm.bmWidth; h = bm.bmHeight / 2; // mask contains AND + XOR masks
	}
	if (w <= 0 || h <= 0) {
		if (ii.hbmColor) DeleteObject(ii.hbmColor);
		if (ii.hbmMask) DeleteObject(ii.hbmMask);
		return nullptr;
	}

	BITMAPINFO bi = {};
	bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	bi.bmiHeader.biWidth = w;
	bi.bmiHeader.biHeight = -h; // top-down
	bi.bmiHeader.biPlanes = 1;
	bi.bmiHeader.biBitCount = 32;
	bi.bmiHeader.biCompression = BI_RGB;

	void * bits = nullptr;
	HDC screenDC = GetDC(nullptr);
	HDC memDC = CreateCompatibleDC(screenDC);
	HBITMAP dib = CreateDIBSection(screenDC, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
	if (!dib || !bits) {
		if (dib) DeleteObject(dib);
		DeleteDC(memDC);
		ReleaseDC(nullptr, screenDC);
		if (ii.hbmColor) DeleteObject(ii.hbmColor);
		if (ii.hbmMask) DeleteObject(ii.hbmMask);
		return nullptr;
	}

	HGDIOBJ oldBmp = SelectObject(memDC, dib);
	memset(bits, 0, (size_t)w * h * 4); // start fully transparent
	DrawIconEx(memDC, 0, 0, base, w, h, 0, NULL, DI_NORMAL);
	SelectObject(memDC, oldBmp);
	DeleteDC(memDC);

	// GDI may draw the icon without writing the alpha channel.
	// If no pixel has alpha, reconstruct it from the icon's own mask:
	// opaque where the mask bit is 0, transparent where it is 1.
	DWORD * px = (DWORD *)bits;
	bool anyAlpha = false;
	for (int i = 0; i < w * h; i++) {
		if ((px[i] & 0xFF000000) != 0) { anyAlpha = true; break; }
	}
	if (!anyAlpha) {
		std::vector<BYTE> maskBits;
		if (ii.hbmMask) {
			// 1bpp top-down DIB, rows padded to 4 bytes
			int stride = ((w + 31) / 32) * 4;
			maskBits.resize((size_t)stride * h, 0xFF);
			BITMAPINFO mbmi = {};
			mbmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
			mbmi.bmiHeader.biWidth = w;
			mbmi.bmiHeader.biHeight = -h;
			mbmi.bmiHeader.biPlanes = 1;
			mbmi.bmiHeader.biBitCount = 1;
			mbmi.bmiHeader.biCompression = BI_RGB;
			HDC infoDC = GetDC(nullptr);
			GetDIBits(infoDC, ii.hbmMask, 0, h, maskBits.data(), &mbmi, DIB_RGB_COLORS);
			ReleaseDC(nullptr, infoDC);
			for (int y = 0; y < h; y++) {
				for (int x = 0; x < w; x++) {
					BYTE byteVal = maskBits[(size_t)y * stride + x / 8];
					bool transparent = (byteVal & (0x80 >> (x % 8))) != 0;
					if (!transparent)
						px[(size_t)y * w + x] |= 0xFF000000;
				}
			}
		}
		else {
			for (int i = 0; i < w * h; i++) {
				if ((px[i] & 0x00FFFFFF) != 0) px[i] |= 0xFF000000;
			}
		}
	}
	ReleaseDC(nullptr, screenDC);

	// Overlay two pause bars: black outline, white fill
	int barW = std::max(2, w / 8);
	int barH = std::max(6, h / 2);
	int gap = std::max(2, w / 8);
	int x1 = (w - (barW * 2 + gap)) / 2;
	int x2 = x1 + barW + gap;
	int y0 = (h - barH) / 2;
	const DWORD black = 0xFF000000;
	const DWORD white = 0xFFFFFFFF;
	auto setPx = [&](int x, int y, DWORD c) {
		if (x >= 0 && x < w && y >= 0 && y < h) px[(size_t)y * w + x] = c;
	};
	for (int pass = 0; pass < 2; pass++) {
		int inset = (pass == 0) ? 1 : 0; // pass 0 = outline ring, pass 1 = fill
		for (int x = x1 - inset; x < x1 + barW + inset; x++)
			for (int y = y0 - inset; y < y0 + barH + inset; y++)
				setPx(x, y, pass == 0 ? black : white);
		for (int x = x2 - inset; x < x2 + barW + inset; x++)
			for (int y = y0 - inset; y < y0 + barH + inset; y++)
				setPx(x, y, pass == 0 ? black : white);
	}

	// Build the icon (alpha channel controls transparency).
	// Use a zeroed 1bpp DIB section as the mask so the bits are
	// never uninitialized (all zero = opaque, alpha decides).
	BITMAPINFO mbi = {};
	mbi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	mbi.bmiHeader.biWidth = w;
	mbi.bmiHeader.biHeight = -h; // top-down
	mbi.bmiHeader.biPlanes = 1;
	mbi.bmiHeader.biBitCount = 1;
	mbi.bmiHeader.biCompression = BI_RGB;
	void * maskBits = nullptr;
	HBITMAP mask = CreateDIBSection(nullptr, &mbi, DIB_RGB_COLORS, &maskBits, nullptr, 0);
	if (mask && maskBits)
		memset(maskBits, 0, (size_t)(((w + 31) / 32) * 4) * h);
	ICONINFO nii = {};
	nii.fIcon = TRUE;
	nii.hbmColor = dib;
	nii.hbmMask = mask;
	HICON result = CreateIconIndirect(&nii);

	DeleteObject(dib);
	if (mask) DeleteObject(mask);
	if (ii.hbmColor) DeleteObject(ii.hbmColor);
	if (ii.hbmMask) DeleteObject(ii.hbmMask);
	return result;
}

//--------------------------------------------------------------
void ofApp::CreateStatusIcons() {
	// Load the custom icon from app resources
	HICON hIcon = (HICON)LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_ICON2),
		IMAGE_ICON, 0, 0, LR_DEFAULTCOLOR);
	if (!hIcon) {
		// Fallback to the default icon
		hIcon = (HICON)LoadIconW(nullptr, IDI_APPLICATION);
	}
	iconBase = hIcon;
	iconPaused = CreatePauseIcon(iconBase);
}

//--------------------------------------------------------------
void ofApp::UpdateTrayIcon() {
	if (!iconBase) return;
	// Pause bars over the tray icon when paused OR not transmitting
	bool transmitting = !isPaused && !idleMode;
	int state = transmitting ? 0 : 1;
	if (state == trayState) return;
	trayState = state;
	nid.hIcon = (state == 1 && iconPaused) ? iconPaused : iconBase;
	Shell_NotifyIconW(NIM_MODIFY, &nid);
}

//--------------------------------------------------------------
void ofApp::DestroyStatusIcons() {
	if (iconPaused) { DestroyIcon(iconPaused); iconPaused = nullptr; }
	if (iconBase) { DestroyIcon(iconBase); iconBase = nullptr; }
}

//--------------------------------------------------------------
// Custom size mode
//--------------------------------------------------------------
void ofApp::EnterCustomSizeMode() {
	if (isCustomSizeMode) return; // Already in custom size mode

	isCustomSizeMode = true;
	customWindowWidth = ofGetWidth();
	customWindowHeight = ofGetHeight();

	// Get current window position
	HWND hWnd = ofGetWin32Window();
	RECT rect;
	GetWindowRect(hWnd, &rect);
	savedWindowX = rect.left;
	savedWindowY = rect.top;

	// Save original window styles
	savedWindowStyle = GetWindowLong(hWnd, GWL_STYLE);
	savedWindowExStyle = GetWindowLongPtr(hWnd, GWL_EXSTYLE);

	// Remove transparency styles to make window interactive
	LONG exStyle = savedWindowExStyle;
	exStyle &= ~(WS_EX_TRANSPARENT | WS_EX_LAYERED | WS_EX_TOPMOST);
	SetWindowLongPtr(hWnd, GWL_EXSTYLE, exStyle);

	// Add window styles to make it movable and resizable (without title bar)
	LONG style = savedWindowStyle;
	style |= WS_THICKFRAME;
	style &= ~(WS_CAPTION | WS_POPUP); // Remove title bar and popup
	SetWindowLong(hWnd, GWL_STYLE, style);

	// Apply window changes
	SetWindowPos(hWnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_FRAMECHANGED);
}

//--------------------------------------------------------------
void ofApp::ExitCustomSizeMode(bool save) {
	if (!isCustomSizeMode) return;

	isCustomSizeMode = false;

	HWND hWnd = ofGetWin32Window();
	RECT rect;
	GetWindowRect(hWnd, &rect);

	if (save) {
		// Get the actual resized window dimensions from Windows rect
		int actualWidth = rect.right - rect.left;
		int actualHeight = rect.bottom - rect.top;
		int actualX = rect.left;
		int actualY = rect.top;

		// Tell OpenFrameworks about the new size using the same logic as ApplyScreenSettings
		ofSetWindowShape(actualWidth, actualHeight);
		ofSetWindowPosition(actualX, actualY);

		// Save the new position/size
		customWindowWidth = actualWidth;
		customWindowHeight = actualHeight;
		savedWindowX = actualX;
		savedWindowY = actualY;
		SaveWindowPosition();
	} else {
		// Restore previous size using the same logic as ApplyScreenSettings
		ofSetWindowShape(customWindowWidth, customWindowHeight);
		ofSetWindowPosition(savedWindowX, savedWindowY);
	}

	// Restore exact window styles from before entering custom size mode
	SetWindowLong(hWnd, GWL_STYLE, savedWindowStyle);
	SetWindowLongPtr(hWnd, GWL_EXSTYLE, savedWindowExStyle);

	SetWindowPos(hWnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_FRAMECHANGED);

	// Force complete window update
	InvalidateRect(hWnd, NULL, FALSE);
	UpdateWindow(hWnd);
	RedrawWindow(hWnd, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW);
}

//--------------------------------------------------------------
void ofApp::FitToScreen() {
	ApplyScreenSettings(selectedMonitorIndex);
	SaveWindowPosition();
}

//--------------------------------------------------------------
void ofApp::SaveWindowPosition() {
	HWND hWnd = ofGetWin32Window();
	RECT rect;
	if (!GetWindowRect(hWnd, &rect)) return;

	// Legacy keys (global fallback)
	WriteConfigValue("WindowX", std::to_string(rect.left));
	WriteConfigValue("WindowY", std::to_string(rect.top));
	WriteConfigValue("WindowWidth", std::to_string(rect.right - rect.left));
	WriteConfigValue("WindowHeight", std::to_string(rect.bottom - rect.top));

	// Per-screen key: remember the geometry for the monitor the
	// window is currently on
	int mon = GetCurrentMonitorIndex();
	if (mon >= 0 && mon < (int)monitors.size()) {
		std::string key = "ScreenWin_" + SanitizeMonitorKey(monitors[mon].name);
		std::string val = std::to_string(rect.left) + "," + std::to_string(rect.top) + ","
			+ std::to_string(rect.right - rect.left) + "," + std::to_string(rect.bottom - rect.top);
		WriteConfigValue(key, val);
	}
}

//--------------------------------------------------------------
void ofApp::LoadWindowPosition() {
	// Prefer the geometry remembered for the selected screen
	if (LoadWindowRectForMonitor(selectedMonitorIndex))
		return;

	// Legacy fallback: global window position
	std::string value;
	int posX = INT_MIN, posY = INT_MIN, width = 0, height = 0;
	if (ReadConfigValue("WindowX", value)) posX = SafeStoi(value, INT_MIN);
	if (ReadConfigValue("WindowY", value)) posY = SafeStoi(value, INT_MIN);
	if (ReadConfigValue("WindowWidth", value)) width = SafeStoi(value, 0);
	if (ReadConfigValue("WindowHeight", value)) height = SafeStoi(value, 0);

	if (posX == INT_MIN || posY == INT_MIN || width <= 0 || height <= 0)
		return;

	// Only apply if the saved window overlaps an existing monitor
	RECT saved = { posX, posY, posX + width, posY + height };
	bool visible = false;
	for (size_t i = 0; i < monitors.size(); i++) {
		RECT inter;
		if (IntersectRect(&inter, &monitors[i].rect, &saved)) { visible = true; break; }
	}
	if (!visible) return;

	ofSetWindowShape(width, height);
	ofSetWindowPosition(posX, posY);
}

//--------------------------------------------------------------
void ofApp::keyPressed(int key) {
	if (!isCustomSizeMode) return;

	if (key == OF_KEY_RETURN) {
		// Save position
		ExitCustomSizeMode(true);
	} else if (key == OF_KEY_ESC) {
		// Cancel - restore previous size
		ExitCustomSizeMode(false);
	}
}

//--------------------------------------------------------------
void ofApp::keyReleased(int key) {
	// Intentionally empty - just needed for consistency
}



void ofApp::setup() {

	ofSetWindowTitle("Spout2OverlayHUD");

	// Render every theme-drawn part of this process dark. The system
	// draws the popup menu border from the visual style theme: in
	// light app mode it is white and no painting hook can recolor it.
	// SetPreferredAppMode (uxtheme ordinal 135, Win10 1809+) is the
	// switch used by dark-mode applications for exactly this.
	{
		HMODULE ux = GetModuleHandleW(L"uxtheme.dll");
		if (!ux) ux = LoadLibraryW(L"uxtheme.dll");
		if (ux) {
			// PreferredAppMode: 0 default, 1 allow dark, 2 force dark
			typedef HRESULT(WINAPI * PFN_SetPreferredAppMode)(int);
			auto pSetPreferredAppMode = (PFN_SetPreferredAppMode)GetProcAddress(ux, (LPCSTR)135);
			if (pSetPreferredAppMode)
				pSetPreferredAppMode(2 /* ForceDark */);
		}
	}

	// Initialize pause state
	isPaused = false;

	// Initialize custom size mode
	isCustomSizeMode = false;
	customWindowWidth = 0;
	customWindowHeight = 0;
	savedWindowX = 0;
	savedWindowY = 0;
	savedWindowStyle = 0;
	savedWindowExStyle = 0;

	// Initialize runtime state
	smoothMode = true;      // Smooth Mode on by default
	idleMode = false;
	senderPinned = false;
	lastSenderSeenTime = ofGetElapsedTimef();
	topmostStage = 0;
	trayState = -1;

	// Enumerate available monitors (with friendly names)
	EnumerateMonitors();

	// Load saved screen preference (by name, legacy index fallback)
	LoadScreenPreference();

	// Apply screen settings
	ApplyScreenSettings(selectedMonitorIndex);

	// Load saved window position if available
	LoadWindowPosition();

	// Load Smooth Mode preference (default on)
	{
		std::string value;
		if (ReadConfigValue("SmoothMode", value))
			smoothMode = (SafeStoi(value, 1) != 0);
	}

	g_appInstance = this;

	// Load a Windows truetype font to avoid dependency on a font file.
	// Arial, Verdana, Tahoma
	LoadWindowsFont(myFont, "Verdana", 12);

	// Optional logs console, comment for Release
	// EnableSpoutLog();


	// Allocate an RGBA texture to receive from the sender
	// It is resized later to match the sender - see Update()
	myTexture.allocate(ofGetWidth(), ofGetHeight(), GL_RGBA);


	hwnd = ofGetWin32Window(); // Get OpenFrameworks window handle


	//  Add Icon to system tray
	LONG style = GetWindowLong(hwnd, GWL_EXSTYLE);
	style &= ~WS_EX_APPWINDOW; // Remove icon from taskbar
	style |= WS_EX_TOOLWINDOW; // Makes sure that icon is only showing in the system tray
	SetWindowLong(hwnd, GWL_EXSTYLE, style);


	// Save the original handler so we don't break OpenFrameworks' native resizing!
	originalWndProc = (WNDPROC)SetWindowLongPtr(hwnd, GWLP_WNDPROC, (LONG_PTR)WindowProc);

	// Tray icon with status variants (pause bars when paused / no sender)
	CreateStatusIcons();

	nid = {};
	nid.cbSize = sizeof(NOTIFYICONDATA);
	nid.hWnd = hwnd;
	nid.uID = 1;
	nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
	nid.uCallbackMessage = WM_USER + 1;
	nid.hIcon = iconBase;
	wcscpy_s(nid.szTip, L"Spout2OverlayHUD");

	Shell_NotifyIconW(NIM_ADD, &nid); // Add icon to the system tray

	// Safety net for dark menu styling: catches popup menu windows
	// through a documented accessibility event in case the CBT hook
	// (installed around TrackPopupMenu) misses them
	g_menuWinEventHook = SetWinEventHook(
		EVENT_SYSTEM_MENUPOPUPSTART, EVENT_SYSTEM_MENUPOPUPSTART,
		nullptr, MenuWinEventProc,
		GetCurrentProcessId(), GetCurrentThreadId(), WINEVENT_OUTOFCONTEXT);

	// Global hotkeys (none by default until the user sets them)
	LoadHotkeys();
	RegisterAllHotkeys();

	// Frame pacing: Smooth Mode (vsync off, 2x monitor refresh) by default
	ApplyFrameRateSettings();
} // end setup


//--------------------------------------------------------------
void ofApp::update() {

	// One-shot topmost re-assertion shortly after startup
	// (fixes occasional loss of topmost position)
	if (topmostStage < 2 && !isCustomSizeMode) {
		double t = ofGetElapsedTimef();
		if (topmostStage == 0 && t >= 0.5) {
			SetWindowPos(hwnd, HWND_TOPMOST, NULL, NULL, NULL, NULL, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
			topmostStage = 1;
		}
		if (topmostStage < 2 && t >= 2.0) {
			SetWindowPos(hwnd, HWND_TOPMOST, NULL, NULL, NULL, NULL, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
			topmostStage = 2;
		}
	}

	// In custom size mode, sync OpenFrameworks with the actual Windows window size every frame
	if (isCustomSizeMode) {
		HWND hWnd = ofGetWin32Window();
		if (hWnd) {
			RECT rect;
			GetWindowRect(hWnd, &rect);
			int actualWidth = rect.right - rect.left;
			int actualHeight = rect.bottom - rect.top;

			// Check if the window size has changed
			if (actualWidth != ofGetWidth() || actualHeight != ofGetHeight()) {
				// Sync OpenFrameworks with the actual window size
				ofSetWindowShape(actualWidth, actualHeight);
				ofSetWindowPosition(rect.left, rect.top);
			}
		}
	}

	// True pause: skip all Spout work while paused
	if (isPaused) {
		return;
	}

	// Pin the connected sender so the overlay never jumps to another
	// sender automatically when it closes. Re-pinned whenever the
	// connected sender changes (e.g. after a Select Source selection).
	if (receiver.IsConnected()) {
		const char * senderName = receiver.GetSenderName();
		if (senderName && *senderName
			&& (!senderPinned || pinnedSenderName != senderName)) {
			pinnedSenderName = senderName;
			receiver.SetReceiverName(senderName);
			senderPinned = true;
		}
	}

	// If IsUpdated() returns true, the sender size has changed
	// and the receiving texture or pixel buffer must be re-sized.
	if (receiver.IsUpdated()) {
		myTexture.allocate(receiver.GetSenderWidth(), receiver.GetSenderHeight(), GL_RGBA);
	}

	// Idle detection: drop to a low poll rate when no sender has been
	// present for 2 seconds, wake up when it returns.
	double now = ofGetElapsedTimef();
	if (receiver.IsConnected()) {
		lastSenderSeenTime = now;
		if (idleMode) {
			idleMode = false;
			ApplyFrameRateSettings();
		}
	}
	else if (!idleMode && (now - lastSenderSeenTime) > 2.0) {
		idleMode = true;
		ApplyFrameRateSettings();
	}

	UpdateTrayIcon();
}

//--------------------------------------------------------------
 void ofApp::draw() {
	// Clean bg with full transparency
	ofClear(0, 0, 0, 0);
	//ofEnableAlphaBlending(); //should not be needed anymore

	// Skip rendering if paused (overlay fully transparent)
	if (isPaused) {
		return;
	}

	// Receive texture and draw scaled to window size.
	// The Spout SDK skips the internal GPU copy when the sender frame
	// is unchanged (frame counting), so polling faster than the sender
	// rate costs almost nothing.
	if (receiver.ReceiveTexture(myTexture.getTextureData().textureID, myTexture.getTextureData().textureTarget)) {
		myTexture.draw(0, 0, ofGetWidth(), ofGetHeight());
	}

	// Draw custom size mode UI
	if (isCustomSizeMode) {
		// Draw red border around the window
		ofSetColor(255, 0, 0, 255);
		ofNoFill();
		ofSetLineWidth(5);
		ofDrawRectangle(0, 0, ofGetWidth(), ofGetHeight());

		// Draw centered text instruction
		ofSetColor(255, 0, 0, 255);
		std::string instructionText = "Press Enter to save position, ESC to cancel";
		if (myFont.isLoaded()) {
			float textWidth = myFont.getStringBoundingBox(instructionText, 0, 0).width;
			int textX = (ofGetWidth() - textWidth) / 2;
			int textY = ofGetHeight() / 2;
			myFont.drawString(instructionText, textX, textY);
		} else {
			ofDrawBitmapString(instructionText, ofGetWidth() / 2 - 100, ofGetHeight() / 2);
		}
		ofSetLineWidth(1);
		ofFill();
		ofSetColor(255);


	}

	// On-screen display for debug
	//showInfo();

	// Disable alpha blending at the end, should not be needed anymore
	//ofDisableAlphaBlending();
}

//  DRAW TEST CUBE TRANSPARENCY FOR DEBUG - DO NOT USE FOR RELEASE
/* void ofApp::draw() {
	ofClear(0, 0, 0, 0); // Mantiene la trasparenza
	ofEnableAlphaBlending();
	ofSetColor(255, 0, 0, 100);
	ofDrawRectangle(100, 100, 200, 200);
	ofDisableAlphaBlending();
}*/

//--------------------------------------------------------------
void ofApp::showInfo() {

	std::string str;
	ofSetColor(255, 255, 255);

	if(receiver.IsConnected()) {
		str = "Receiving [";
		str += receiver.GetSenderName(); // sender name
		str += "] (";
		if (receiver.GetSenderCPU())
			str += "CPU share : "; 
		str += to_string(receiver.GetSenderWidth()); // width
		str += "x";
		str += to_string(receiver.GetSenderHeight()); // height 
		if (receiver.GetSenderFrame() > 0) {
			str += " : fps ";
			str += to_string((int)(round(receiver.GetSenderFps()))); // frames per second
			str += " : frame ";
			str += to_string(receiver.GetSenderFrame()); // frame since the sender started
		}
		str += ") ";
		
		DrawString(str, 10, 20);
	}
	else {
		str = "No sender detected";
		DrawString(str, 10, 20);
	}

}

//--------------------------------------------------------------
void ofApp::exit() {
	UnregisterAllHotkeys();
	if (g_menuWinEventHook) {
		UnhookWinEvent(g_menuWinEventHook);
		g_menuWinEventHook = nullptr;
	}
	Shell_NotifyIconW(NIM_DELETE, &nid);
	DestroyStatusIcons();
	DestroyMenuTip();
	receiver.ReleaseReceiver();
}


//--------------------------------------------------------------
// Load a Windows truetype font
bool ofApp::LoadWindowsFont(ofTrueTypeFont& font, std::string name, int size)
{
	std::string fontfolder;
	char* path = nullptr;
	errno_t err = _dupenv_s(&path, NULL, "WINDIR");
	if (err == 0 && path) {
		fontfolder = path;
		fontfolder += "\\Fonts\\";
		fontfolder += name;
		fontfolder += ".ttf";
		if (_access(fontfolder.c_str(), 0) != -1) {
			return font.load(fontfolder, size, true, true);
		}
	}
	return false;
}

//--------------------------------------------------------------
void ofApp::DrawString(std::string str, int posx, int posy)
{
	if (myFont.isLoaded()) {
		myFont.drawString(str, posx, posy);
	}
	else {
		// This will only happen if the Windows font is not foud
		// Quick fix because the default font is wider
		int x = posx-20;
		if (x <= 0) x = 10;
		ofDrawBitmapString(str, x, posy);
	}
}

void ofApp::launchGit() {
	
	SystemOpenURL("https://github.com/Marsic1/Spout2OverlayHUD");

}


