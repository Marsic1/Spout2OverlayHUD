/*

	Spout2OverlayHUD V0.5 RC

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


// Global variable for system tray icon
NOTIFYICONDATA nid;
HWND hwnd;
WNDPROC originalWndProc = nullptr;
static ofApp * g_appInstance = nullptr;

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

	if (message == WM_USER + 1) { // System tray event
		if (LOWORD(lParam) == WM_RBUTTONDOWN) { // Right Click
			// Create the menu
			HMENU hMenu = CreatePopupMenu();
			AppendMenu(hMenu, MF_STRING | MF_DISABLED, 0, L"--- Spout2OverlayHUD by Marsic1 V0.5 RC ---"); // Tray Title, not clickable
			AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);

			// Add pause/resume toggle as first item
			if (g_appInstance) {
				const wchar_t * pauseText = g_appInstance->isPaused ? L"▶️ Resume Overlay" : L"⏸️ Pause Overlay";
				AppendMenu(hMenu, MF_STRING, 4, pauseText);
			}

			AppendMenu(hMenu, MF_STRING, 1, L"Select Source");

			// Add screen selection submenu
			if (g_appInstance && !g_appInstance->monitors.empty()) {
				HMENU hScreenMenu = CreatePopupMenu();
				for (size_t i = 0; i < g_appInstance->monitors.size(); i++) {
					std::wstring name = g_appInstance->monitors[i].name;
					UINT flags = MF_STRING;
					if (i == g_appInstance->selectedMonitorIndex) {
						flags |= MF_CHECKED;
					}
					AppendMenu(hScreenMenu, flags, 100 + i, name.c_str());
				}
				AppendMenu(hMenu, MF_POPUP, (UINT_PTR)hScreenMenu, L"Select Screen");
			}

			AppendMenu(hMenu, MF_STRING, 2, L"About");
			AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
			AppendMenu(hMenu, MF_STRING, 5, L"Custom Size");
			AppendMenu(hMenu, MF_STRING, 6, L"Fit to Screen");
			AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
			AppendMenu(hMenu, MF_STRING, 3, L"Exit");

			// Get cursor position
			POINT cursor;
			GetCursorPos(&cursor);

			// Show the menu in the position of the mouse
			SetForegroundWindow(hWnd);
			int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_NONOTIFY, cursor.x, cursor.y, 0, hWnd, NULL);
			DestroyMenu(hMenu);

			switch (cmd) {
			case 1:
				// Select Source
				if (g_appInstance) {
					g_appInstance->receiver.SelectSender(ofGetWin32Window());
					//Restore TopMost Position after changing source
					SetWindowPos(hwnd, HWND_TOPMOST, NULL, NULL, NULL, NULL, SWP_NOMOVE | SWP_NOSIZE);
				}
				break;
			case 2:
				// Open Git
				if (g_appInstance) {
					g_appInstance->launchGit();
				}
				break;
			case 3:
				// Exit
				PostQuitMessage(0);
				break;
			case 4:
				// Pause/Resume
				if (g_appInstance) {
					g_appInstance->TogglePause();
				}
				break;
			case 5:
				// Custom Size
				if (g_appInstance) {
					g_appInstance->EnterCustomSizeMode();
				}
				break;
			case 6:
				// Fit to Screen
				if (g_appInstance) {
					g_appInstance->FitToScreen();
				}
				break;
			default:
				// Check if it's a screen selection (100-199)
				if (cmd >= 100 && cmd < 200) {
					int screenIdx = cmd - 100;
					if (g_appInstance) {
						g_appInstance->SelectScreen(screenIdx);
						SetWindowPos(hwnd, HWND_TOPMOST, NULL, NULL, NULL, NULL, SWP_NOMOVE | SWP_NOSIZE);
					}
				}
				break;
			}
		}
	}
	if (originalWndProc) {
		return CallWindowProc(originalWndProc, hWnd, message, wParam, lParam);
	}
	return DefWindowProc(hWnd, message, wParam, lParam);
}

// Monitor enumeration callback
BOOL CALLBACK MonitorEnumProc(HMONITOR hMonitor, HDC hdcMonitor, LPRECT lprcMonitor, LPARAM dwData) {
	std::vector<MonitorInfo>* pMonitors = reinterpret_cast<std::vector<MonitorInfo>*>(dwData);

	MONITORINFOEX info;
	info.cbSize = sizeof(MONITORINFOEX);
	if (GetMonitorInfo(hMonitor, &info)) {
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
	monitors.clear();
	selectedMonitorIndex = 0;
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
void ofApp::SelectScreen(int monitorIndex) {
	if (monitorIndex < 0 || monitorIndex >= monitors.size()) {
		return;
	}

	selectedMonitorIndex = monitorIndex;
	ApplyScreenSettings(monitorIndex);
	SaveScreenPreference();
}

//--------------------------------------------------------------
void ofApp::SaveScreenPreference() {
	// Save to a config file or registry
	// For simplicity, we'll use the LocalAppData folder
	char appDataPath[MAX_PATH];
	if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, appDataPath))) {
		std::string configPath(appDataPath);
		configPath += "\\Spout2OverlayHUD";
		CreateDirectoryA(configPath.c_str(), NULL);

		configPath += "\\config.ini";
		std::ofstream configFile(configPath);
		if (configFile.is_open()) {
			configFile << "SelectedMonitor=" << selectedMonitorIndex << "\n";
			configFile.close();
		}
	}
}

//--------------------------------------------------------------
void ofApp::LoadScreenPreference() {
	selectedMonitorIndex = 0; // Default to first monitor

	char appDataPath[MAX_PATH];
	if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, appDataPath))) {
		std::string configPath(appDataPath);
		configPath += "\\Spout2OverlayHUD\\config.ini";

		std::ifstream configFile(configPath);
		if (configFile.is_open()) {
			std::string line;
			while (std::getline(configFile, line)) {
				if (line.find("SelectedMonitor=") == 0) {
					selectedMonitorIndex = std::stoi(line.substr(16));
					// Clamp to valid range
					if (selectedMonitorIndex >= monitors.size()) {
						selectedMonitorIndex = 0;
					}
				}
			}
			configFile.close();
		}
	}
}

//--------------------------------------------------------------
void ofApp::TogglePause() {
	isPaused = !isPaused;
}

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

	// Restore original window styles
	// Restore exact window styles from before entering custom size mode
	SetWindowLong(hWnd, GWL_STYLE, savedWindowStyle);
	SetWindowLongPtr(hWnd, GWL_EXSTYLE, savedWindowExStyle);

	// Reapply layered window attributes with original settings
	SetLayeredWindowAttributes(hWnd, RGB(0, 0, 0), 255, LWA_COLORKEY);
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
	GetWindowRect(hWnd, &rect);

	char appDataPath[MAX_PATH];
	if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, appDataPath))) {
		std::string configPath(appDataPath);
		configPath += "\\Spout2OverlayHUD";
		CreateDirectoryA(configPath.c_str(), NULL);

		configPath += "\\config.ini";
		std::ofstream configFile(configPath, std::ios::app);
		if (configFile.is_open()) {
			// Read existing content first
			std::ifstream inFile(configPath);
			std::string content, line;
			bool hasWindowPos = false;
			while (std::getline(inFile, line)) {
				if (line.find("WindowX=") != 0 && line.find("WindowY=") != 0 && 
					line.find("WindowWidth=") != 0 && line.find("WindowHeight=") != 0) {
					content += line + "\n";
				} else {
					hasWindowPos = true;
				}
			}
			inFile.close();

			// Write back without window position lines
			configFile.close();
			std::ofstream outFile(configPath);
			outFile << content;
			outFile << "WindowX=" << rect.left << "\n";
			outFile << "WindowY=" << rect.top << "\n";
			outFile << "WindowWidth=" << (rect.right - rect.left) << "\n";
			outFile << "WindowHeight=" << (rect.bottom - rect.top) << "\n";
			outFile.close();
		}
	}
}

//--------------------------------------------------------------
void ofApp::LoadWindowPosition() {
	char appDataPath[MAX_PATH];
	if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, appDataPath))) {
		std::string configPath(appDataPath);
		configPath += "\\Spout2OverlayHUD\\config.ini";

		std::ifstream configFile(configPath);
		if (configFile.is_open()) {
			std::string line;
			int posX = -1, posY = -1, width = -1, height = -1;
			while (std::getline(configFile, line)) {
				if (line.find("WindowX=") == 0) {
					posX = std::stoi(line.substr(8));
				} else if (line.find("WindowY=") == 0) {
					posY = std::stoi(line.substr(8));
				} else if (line.find("WindowWidth=") == 0) {
					width = std::stoi(line.substr(12));
				} else if (line.find("WindowHeight=") == 0) {
					height = std::stoi(line.substr(13));
				}
			}
			configFile.close();

			// Apply saved position and size if available
			if (posX != -1 && posY != -1 && width > 0 && height > 0) {
				ofSetWindowShape(width, height);
				ofSetWindowPosition(posX, posY);
			}
		}
	}
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

	// Enumerate available monitors
	EnumerateMonitors();

	// Load saved screen preference
	LoadScreenPreference();

	// Apply screen settings
	ApplyScreenSettings(selectedMonitorIndex);

	// Load saved window position if available
	LoadWindowPosition();

	g_appInstance = this;

	/*
	//------------OTHER CODE START
	//Other Old code for tests, should be not necessary, i'm keeping it just for reference and future optimization
	HWND hWnd = GetActiveWindow();
	LONG exStyle = GetWindowLong(hWnd, GWL_EXSTYLE);
	SetWindowLong(hWnd, GWL_EXSTYLE, exStyle | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW);
	SetLayeredWindowAttributes(hWnd, RGB(0, 0, 0), 255, LWA_COLORKEY);
		
	//ofSetFullscreen(true);

	// Force Window update
	//SetWindowPos(hWnd, HWND_TOPMOST, 0, 0, 0, 0,		SWP_NOMOVE | SWP_NOSIZE | SWP_FRAMECHANGED);
	//ofBackground(0, 0, 0, 0);
	//------------OTHER CODE END
	*/


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

	

	//SetWindowLongPtr(hwnd, GWLP_WNDPROC, (LONG_PTR)WindowProc);
	// Save the original handler so we don't break OpenFrameworks' native resizing!
	originalWndProc = (WNDPROC)SetWindowLongPtr(hwnd, GWLP_WNDPROC, (LONG_PTR)WindowProc);

	nid.cbSize = sizeof(NOTIFYICONDATA);
	nid.hWnd = hwnd;
	nid.uID = 1;
	nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
	nid.uCallbackMessage = WM_USER + 1;

	// V0.1 Load the custom icon from the file (replace path with your icon file location)
	//HICON hIcon = (HICON)LoadImage(NULL, L"tray.ico", IMAGE_ICON, 0, 0, LR_LOADFROMFILE);

	// V0.2 Load the custom icon from app resources, doesn't need .ico file in the executable path
	HICON hIcon = (HICON)LoadImage(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_ICON2), IMAGE_ICON, 0, 0, LR_DEFAULTCOLOR);


	// Check if the icon was loaded successfully
	if (hIcon) {
		nid.hIcon = hIcon; // Set the custom icon
	} else {
		// If the icon fails to load, fall back to the default icon
		nid.hIcon = (HICON)LoadIcon(NULL, IDI_APPLICATION); // Fallback
	}

	wcscpy_s(nid.szTip, L"Spout2OverlayHUD");

	Shell_NotifyIcon(NIM_ADD, &nid); // Add icon to the system tray
} // end setup


//--------------------------------------------------------------
void ofApp::update() {

	// If IsUpdated() returns true, the sender size has changed
	// and the receiving texture or pixel buffer must be re-sized.
	if (receiver.IsUpdated()) {
		myTexture.allocate(receiver.GetSenderWidth(), receiver.GetSenderHeight(), GL_RGBA);
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
}

//--------------------------------------------------------------
 void ofApp::draw() {
	// Clean bg with full transparency
	ofClear(0, 0, 0, 0);
	//ofEnableAlphaBlending(); //should not be needed anymore

	// Skip rendering if paused
	if (isPaused) {
		return;
	}

	// Receive texture and draw scaled to window size
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
	Shell_NotifyIcon(NIM_DELETE, &nid);
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


