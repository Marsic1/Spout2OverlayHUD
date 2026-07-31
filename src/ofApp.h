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

//#define WM_SELECT_SOURCE (WM_USER + 2)

// Monitor info structure
struct MonitorInfo {
	HMONITOR handle;
	std::wstring name;
	RECT rect;
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
		void ApplyScreenSettings(int monitorIndex);
		void SaveScreenPreference();
		void LoadScreenPreference();

		// Pause/Resume toggle
		bool isPaused;
		void TogglePause();

		// Custom size and position
		bool isCustomSizeMode;
		int customWindowWidth;
		int customWindowHeight;
		int savedWindowX;
		int savedWindowY;
		LONG savedWindowStyle;
		LONG savedWindowExStyle;
		void EnterCustomSizeMode();
		void ExitCustomSizeMode(bool save);
		void FitToScreen();
		void SaveWindowPosition();
		void LoadWindowPosition();
};
