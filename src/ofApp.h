/*

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

*/
#pragma once

#include "ofMain.h"
#include "..\\..\\..\\apps\\SpoutGL\\SpoutReceiver.h" // Spout SDK
#include "../resource.h"

#include <string>
#include <vector>

//#define WM_SELECT_SOURCE (WM_USER + 2)

// Monitor info structure
struct MonitorInfo {
	HMONITOR handle = nullptr;
	std::wstring name;         // GDI device name, e.g. \\.\DISPLAY1
	std::wstring friendlyName; // monitor name from EDID, e.g. DELL S2721DGF
	std::wstring label;        // menu label, e.g. "DELL S2721DGF (2560x1440)"
	RECT rect{};
};

// Global hotkey definition
struct HotkeyCombo {
	UINT vk = 0;        // virtual key code, 0 = not set
	UINT modifiers = 0; // MOD_CONTROL / MOD_ALT / MOD_SHIFT / MOD_WIN
	UINT scan = 0;      // scancode at capture time, 0 = unknown (legacy config)
	bool extended = false; // extended-key flag at capture time
	bool IsSet() const { return vk != 0; }
};

class ofApp : public ofBaseApp{
	public:
		void setup();
		void update();
		void draw();
		void exit();
		void launchGit();

		// For Truetype font
		ofTrueTypeFont myFont;
		bool LoadWindowsFont(ofTrueTypeFont& font, std::string name, int size);
		void DrawString(std::string str, int posx, int posy);

		SpoutReceiver receiver; // A Spout receiver object
		ofTexture myTexture; // Receiving texture
		void showInfo();

		// Keyboard input
		void keyPressed(int key);
		void keyReleased(int key);

		// Multi-screen support
		std::vector<MonitorInfo> monitors;
		int selectedMonitorIndex;
		void EnumerateMonitors();
		void SelectScreen(int monitorIndex);
		void CycleScreen();
		void ApplyScreenSettings(int monitorIndex);
		void SaveScreenPreference();
		void LoadScreenPreference();
		int GetSelectedMonitorHz() const;
		int GetCurrentMonitorIndex();
		bool LoadWindowRectForMonitor(int monitorIndex);

		// Pause/Resume toggle
		bool isPaused = false;
		void TogglePause();

		// Smooth Mode: poll the sender faster than the monitor refresh
		// so every sender frame is caught (fixes frame skipping).
		bool smoothMode = true;
		void SetSmoothMode(bool enabled);

		// Frame pacing, derived from smoothMode / isPaused / idleMode
		void ApplyFrameRateSettings();

		// Idle detection: drop to a low poll rate when no sender is present
		bool idleMode = false;
		double lastSenderSeenTime = 0.0;

		// Sender pinning: stay on the current sender and never jump
		// to another one automatically when it closes.
		bool senderPinned = false;
		std::string pinnedSenderName;

		// Global hotkeys
		HotkeyCombo hotkeyPause;
		HotkeyCombo hotkeyScreen;
		void RegisterAllHotkeys();
		void UnregisterAllHotkeys();
		bool ApplyPauseHotkey(UINT modifiers, UINT vk, UINT scan, bool extended);
		bool ApplyScreenHotkey(UINT modifiers, UINT vk, UINT scan, bool extended);
		void ClearAllHotkeys();
		void LoadHotkeys();
		void SaveHotkeys();

		// Tray icon states (receiving / paused-or-no-sender)
		HICON iconBase = nullptr;
		HICON iconPaused = nullptr;
		int trayState = -1; // -1 unknown, 0 receiving, 1 paused or no sender
		void CreateStatusIcons();
		void UpdateTrayIcon();
		void DestroyStatusIcons();

		// Custom size and position
		bool isCustomSizeMode = false;
		int customWindowWidth = 0;
		int customWindowHeight = 0;
		int savedWindowX = 0;
		int savedWindowY = 0;
		LONG savedWindowStyle = 0;
		LONG savedWindowExStyle = 0;
		void EnterCustomSizeMode();
		void ExitCustomSizeMode(bool save);
		void FitToScreen();
		void SaveWindowPosition();
		void LoadWindowPosition();

		// Config helpers (config.ini in %LOCALAPPDATA%\Spout2OverlayHUD)
		static std::string GetConfigPath();
		static bool ReadConfigValue(const std::string& key, std::string& value);
		static void WriteConfigValue(const std::string& key, const std::string& value);

		// One-shot topmost re-assertion after startup
		int topmostStage = 0; // 0 = none, 1 = 500ms done, 2 = finished
};
