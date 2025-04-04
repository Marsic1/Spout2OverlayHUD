/*

	Spout2OverlayHUD V0.2 RC

	Spout 2.007
	OpenFrameworks 13 v20250331
	Visual Studio 2022

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


// Global variable for system tray icon
NOTIFYICONDATA nid;
HWND hwnd;
static ofApp * g_appInstance = nullptr;

LRESULT CALLBACK WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
	if (message == WM_USER + 1) { // System tray event
		if (LOWORD(lParam) == WM_RBUTTONDOWN) { // Right Click
			// Create the menu
			HMENU hMenu = CreatePopupMenu();
			AppendMenu(hMenu, MF_STRING | MF_DISABLED , 0, L"--- Spout2OverlayHUD by Marsic1 ---"); // Tray Title, not clickable
			AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
			AppendMenu(hMenu, MF_STRING, 1, L"Select Source");
			AppendMenu(hMenu, MF_STRING, 2, L"About");
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
			}
		}
	}

	return DefWindowProc(hWnd, message, wParam, lParam);
}



//--------------------------------------------------------------
void ofApp::setup() {

	ofSetWindowTitle("Spout Data Receiverr");
	int screenW = ofGetScreenWidth();
	int screenH = ofGetScreenHeight();
	// Set of screen size, remove 2 pixels per size to not have the window start fullscreen with black background
	ofSetWindowShape(screenW-2, screenH-2); 

	// Centre on the screen
	ofSetWindowPosition((ofGetScreenWidth()-ofGetWidth())/2, (ofGetScreenHeight()-ofGetHeight())/2);

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

	

	SetWindowLongPtr(hwnd, GWLP_WNDPROC, (LONG_PTR)WindowProc);
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
}

//--------------------------------------------------------------
 void ofApp::draw() {
	// Clean bg with transparency
	ofClear(0, 0, 0, 0);
	//ofEnableAlphaBlending(); //should not be needed anymore

	// Receive texture
	if (receiver.ReceiveTexture(myTexture.getTextureData().textureID, myTexture.getTextureData().textureTarget)) {
		myTexture.draw(0, 0, ofGetWidth(), ofGetHeight());
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


