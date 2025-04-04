#include "ofApp.h"

/*
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

//--------------------------------------------------------------
// to change options for console window (Visual Studio)
//
// Properties > Linker > System > Subsystem
//    for console : Windows (/SUBSYSTEM:CONSOLE)
//
//    for Window : Windows (/SUBSYSTEM:WINDOWS)
//
// Click APPLY and OK. Then make changes to Main as below
//--------------------------------------------------------------

// for default console
//========================================================================
// int main() {
//
// for window without console
//========================================================================

 int WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR lpCmdLine, _In_ int nShowCmd) {

		// Mutex id for checking running status
		const TCHAR * mutexName = _T("Spout2OverlayHUD");

		// Creating the Mutex
		HANDLE hMutex = CreateMutex(NULL, TRUE, mutexName);

		// Checking if Mutex exists in the system (another instance is already running)
		if (hMutex && GetLastError() == ERROR_ALREADY_EXISTS) {
			int response = MessageBox(NULL, _T("Spout2OverlayHUD is already running. Do you want to open another instance?"), _T("Warning"), MB_YESNO | MB_ICONQUESTION);

			if (response == IDNO) {
				// If User selects no close the app, releasing the mutex
				CloseHandle(hMutex);
				return 0;
			}
		}

        ofGLFWWindowSettings settings;
		settings.transparent = true; // Enable Transparency, requires OF13
		settings.mousePassThrough = true; // Enable Mouse PassThroguh, requires OF13
		settings.floating = true;
		settings.decorated = false;
		settings.windowMode = OF_WINDOW;
		settings.setGLVersion(4, 1); // OpenGL 4.1
        settings.setSize(1920, 1080);
        auto window = ofCreateWindow(settings);


        ofRunApp(window, make_shared<ofApp>());

        ofRunMainLoop();

		// Release the mutex
		CloseHandle(hMutex);
 }

