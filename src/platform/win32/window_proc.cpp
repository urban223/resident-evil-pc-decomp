// WindowProc.cpp - Main window procedure + keyboard input handling
// WindowProc (0x00441170), OnKeyDown (0x00497830)
#include "Globals.h"
#include "game/editor/Editor.h"   // CUSTOM: the in-game editor
#include <mmsystem.h>

// ============================================================================
// OnKeyDown - Handle keyboard input (0x00497830)
//
// F1 (0x70): Cycles g_F1DebugMode through 0-3
// F9 (0x78): Complex escape/return-to-title logic with debounce and state checks
// Any other key: Clears g_displayReturnToTitleScreen_Flag and g_displayExitGameScreen_flag
// ============================================================================
void OnKeyDown(HWND hwnd, WPARAM wparam)
{
	bool bBlocked;
	DWORD now;

	switch (wparam) {
	case VK_F1: // 0x70
		g_F1DebugMode++;
		if (g_F1DebugMode > 3) {
			g_F1DebugMode = 0;
		}
		break;

	case VK_F9: // 0x78
		now = timeGetTime();

		// Debounce/cooldown: block if within 3 seconds of game init
		bBlocked = (now < g_GameInitTime + 3000);
		// Block if within 2ms of last F9 press
		if ((now < g_lastF9PressTime) || (now - g_lastF9PressTime < 2)) {
			bBlocked = true;
		}
		// Don't block if already showing a dialog
		if (g_displayReturnToTitleScreen_Flag != 0 || g_displayExitGameScreen_flag != 0) {
			bBlocked = false;
		}
		// Block during MCI video playback
		if (g_mciVideoDeviceID != 0) {
			bBlocked = true;
		}
		// Block if block flag is set
		if (g_blockF9Flag == 1) {
			bBlocked = true;
		}

		if (!bBlocked) {
			if (g_playingGameFlag == 0) {
				// Title/menu: toggle exit dialog
				if (g_displayExitGameScreen_flag == 0) {
					g_displayExitGameScreen_flag = 1;
				} else {
					g_displayExitGameScreen_flag = 0;
					CleanupVideoConfigAndSaveAllSettings();
					DestroyWindow(g_hWnd);
				}
			} else if (g_displayReturnToTitleScreen_Flag == 0) {
				// In-game: set return-to-title flag
				g_displayReturnToTitleScreen_Flag = 1;
			} else {
				// In-game, already returning: toggle off and trigger reset
				g_displayReturnToTitleScreen_Flag = 0;
				g_pressF9Flag = 0;
				g_resetGameFlag = 1;
			}
		}
		g_lastF9PressTime = now;
		break;
	}

	// Any key other than F9 clears the dialog flags
	if (wparam != VK_F9) {
		g_displayReturnToTitleScreen_Flag = 0;
		g_displayExitGameScreen_flag = 0;
	}
}

// ============================================================================
// WindowProc - Handle window messages (0x00441170)
// ============================================================================
LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    // 0x0044119c: Check if Marni Direct3D handles this message first
    if (g_pMarniDirect3D != NULL) {
        // Call vtable[5] -> HandleWindowMessage (0x00448b60)
        typedef int (*HandleMsgFunc)(void*, HWND, UINT, WPARAM, LPARAM);
        void** vtable = *(void***)g_pMarniDirect3D;
        HandleMsgFunc handler = (HandleMsgFunc)vtable[5];
        int result = handler(g_pMarniDirect3D, hwnd, msg, wParam, lParam);
        if (result == 0) {
            return 0;
        }
    }
    
    // 0x004411d0: Message dispatch
    switch (msg) {
        // --- WM_ACTIVATE ---
        // 0x004411e2: The original writes DAT_004bcb2c (window focused flag) here,
        // NOT g_isPaused (0x004d46ac). g_isPaused is the SideWinder pause-button
        // event flag: main_loop consumes it and injects a synthetic START+bit8
        // (0x900) press, which triggers the Option Mode menu combo. Writing it
        // here made every window focus change open the (stubbed) options menu.
        case WM_ACTIVATE:
            g_bWindowFocused = FALSE;
            if (LOWORD(wParam) == WA_INACTIVE) {
                // CUSTOM: a button released while we are not focused never
                // arrives, and the drag it started would run forever.
                EditorInput_OnFocusLost();
                // 0x004411f4: Window deactivated
                if (!g_bIsSoftwareRendering && g_mciVideoDeviceID == 1) {
                    g_bMCIVideoEvent = TRUE;
                }
                PauseSounds();
            } else {
                // Window activated
                g_bWindowFocused = TRUE;
                ResumePausedSounds();
            }
            break;
        
        // --- WM_CREATE ---
        case WM_CREATE:
            // 0x00441226: Initialize sound system
            if (g_dwSelectedDisplayAdapterID == 0) {
                ProbeWaveOutDevicesAndCacheVolume();
            }
            StartSoundSystemAsync(hwnd);
            break;
        
        // --- WM_DESTROY ---
        case WM_DESTROY:
            // 0x00441250: Cleanup and quit
            CleanupVideoConfigAndSaveAllSettings();
            g_hWnd = NULL;
            if (g_dwSelectedDisplayAdapterID == 0) {
                RestoreWaveOutVolume();
            }
            g_bQuitFlag = TRUE;
            PostQuitMessage(0);
            break;
        
        // --- WM_PAINT ---
        case WM_PAINT:
            {
                PAINTSTRUCT ps;
                BeginPaint(hwnd, &ps);
                EndPaint(hwnd, &ps);
            }
            break;
        
        // --- WM_KEYDOWN ---
        case WM_KEYDOWN:
            // 0x004412c8
            OnKeyDown(hwnd, wParam);
            break;
        
        // --- WM_KEYUP ---
        case WM_KEYUP:
            // 0x004412d2: Check for PrintScreen
            if (wParam == VK_SNAPSHOT) {
                CreateTimestampedLogFile();
            }
            // Not in the original: F8 toggles the collision boundary overlay.
            // Handled on KEYUP like PrintScreen above, because WM_KEYDOWN
            // autorepeats while the key is held and would flip the toggle
            // every repeat. Only while debug features are enabled
            // ([Debug] EnableDebug in config.ini).
            else if (wParam == VK_F8) {
                if (g_debugFeaturesEnabled) {
                    g_bShowCollisionDebug = g_bShowCollisionDebug ? FALSE : TRUE;
                }
            }
            // Not in the original: debug helpers. F6 requests the texture
            // viewer overlay. Also KEYUP (single-fire, no autorepeat); the
            // game loop consumes the flags. Only while debug features are
            // enabled.
            else if (wParam == VK_F6) {
                if (g_debugFeaturesEnabled) {
                    g_debugTextureViewerFlag = 1;
                }
            }
            // Not in the original: F7 re-reads the RAID level file
            // (Data\\raid1.lvl) without leaving the room, so an external
            // editor can be a live one. Consumed by RaidArena_Draw, which is
            // the point in the frame where the room is certainly loaded.
            // KEYUP, like the toggles above, so autorepeat cannot queue a
            // hundred reloads while the key is held.
            else if (wParam == VK_F7) {
                g_raidReloadRequest = 1;
            }
            // Not in the original: F2 toggles the in-game level editor. KEYUP
            // like the rest, so autorepeat cannot flip it every frame the key
            // is held. It only does anything in RAID mode.
            else if (wParam == VK_F2) {
                Editor_Toggle();
            }
            break;

        // --- Mouse ---
        // Not in the original: the game had no mouse handling at all. These
        // four feed the editor, which is the only thing that wants a cursor.
        //
        // GET_X_LPARAM/GET_Y_LPARAM are signed on purpose: with the mouse
        // captured, a drag that leaves the window reports negative client
        // coordinates, and the (unsigned) LOWORD idiom turns those into 65000
        // and sends a gizmo to the other side of the level.
        case WM_MOUSEMOVE:
            EditorInput_OnMouseMove((int)(short)LOWORD(lParam),
                                    (int)(short)HIWORD(lParam));
            break;

        case WM_LBUTTONDOWN: SetCapture(hwnd); EditorInput_OnMouseButton(ED_MB_LEFT,   1); break;
        case WM_RBUTTONDOWN: SetCapture(hwnd); EditorInput_OnMouseButton(ED_MB_RIGHT,  1); break;
        case WM_MBUTTONDOWN: SetCapture(hwnd); EditorInput_OnMouseButton(ED_MB_MIDDLE, 1); break;

        // Release the capture only once NO button is still down: a chorded
        // press (Unreal's "move without rotating" is LMB+RMB) would otherwise
        // drop the capture on the first release and lose the rest of the drag.
        case WM_LBUTTONUP:
        case WM_RBUTTONUP:
        case WM_MBUTTONUP:
            EditorInput_OnMouseButton(msg == WM_LBUTTONUP ? ED_MB_LEFT :
                                      msg == WM_RBUTTONUP ? ED_MB_RIGHT : ED_MB_MIDDLE, 0);
            if ((wParam & (MK_LBUTTON | MK_RBUTTON | MK_MBUTTON)) == 0) {
                ReleaseCapture();
            }
            break;

        case WM_MOUSEWHEEL:
            EditorInput_OnMouseWheel((int)(short)HIWORD(wParam));
            break;

        // Not in the original: typed characters, for the editor's numeric and
        // text fields. A virtual key code is not a character - it is a key
        // position, before the layout has had its say - so the fields read
        // this and the commands read plat_key_state.
        case WM_CHAR:
            EditorInput_OnChar((int)wParam);
            break;
        
        // --- WM_SYSCOMMAND ---
        case WM_SYSCOMMAND:
            // 0x00441300: Handle restore for software mode
            if (g_bIsSoftwareRendering && (wParam == SC_RESTORE)) {
                ShowWindow(hwnd, SW_HIDE);
                ShowWindow(hwnd, SW_SHOW);
                ShowWindow(hwnd, SW_SHOWDEFAULT);
                UpdateWindow(hwnd);
                SetForegroundWindow(hwnd);
            }
            break;
        
        // --- MM_MCINOTIFY (MCI video notification) ---
        case MM_MCINOTIFY:
            // 0x00441330
            if ((wParam == MCI_NOTIFY_SUCCESSFUL) && (g_mciVideoDeviceID == 1)) {
                g_bMCIVideoEvent = TRUE;
            }
            break;
    }
    
    // 0x00441340: Default processing
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}
