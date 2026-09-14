// TitleScreen.cpp - Title screen rendering and state management
// All functions decompiled from Ghidra with original addresses
#include "../Globals.h"
#include "../marni/MarniSystem.h"
#include "../marni/PSXTexture.h"
#include "FileLoader.h"
#include "SpriteRenderer.h"
#include "SFXIds.h"
#include "UiAtlas.h"        // CUSTOM: the title menu's own word art
#include "../platform/platform.h"
#include <cstdio>
#include <cstdlib>
#include "../system/AssetPath.h"

extern void logos_state(void);

// ============================================================================
// set_display_resolution (0x00401000)
// ============================================================================
void set_display_resolution(int w, int h, int mode)
{
    g_displayWidth = w;
    g_displayHeight = h;
    g_displayMode = mode;
}

// ============================================================================
// check_save_files_exist (0x00494190)
// Returns 1 if any save files exist, 0 otherwise.
// ============================================================================
int check_save_files_exist(void)
{
    char path[260];
    for (int i = 1; i <= 8; i++) {
        sprintf(path, "%ssavedat%d.dat", GetSaveRoot(), i);
        FILE* f = fopen(path, "rb");
        if (f != NULL) {
            fclose(f);
            return 1;
        }
    }
    return 0;
}

// ============================================================================
// title_setup_texture_pages (0x00470970)
// Creates texture pages for button prompt images.
// ============================================================================
void title_setup_texture_pages(int slot, int mode)
{
    // Same legacy-descriptor caveat as TextureLoader: these tables are indexed
    // by slot * 0x37C and the port's stand-ins are a few KB, so the room-load
    // calls (load_room_bg passes the camera index, 0..7) run off the end. The
    // DX11 path takes page state from the g_TexturePage* arrays, so reads that
    // fall out of range can yield 0 - but the destroy loop's writes must not
    // happen at all.
    int slotBase = slot * 0x37C;
    const bool cntOk   = (size_t)slotBase / sizeof(DWORD)
                         < sizeof(g_VideoDriverArray_814) / sizeof(DWORD);
    const bool tableOk = (size_t)slotBase + 8 * sizeof(DWORD) <= sizeof(g_TexturePageTable_DAT);
    const bool dataOk  = (size_t)slotBase + 2 * 0x68 <= sizeof(g_VideoDriverArray_4d0);

    int pageCount = cntOk ? g_VideoDriverArray_814[slotBase / 4] : 0;
    if (pageCount != 0 && tableOk) {
        DWORD* pageTable = (DWORD*)((BYTE*)&g_TexturePageTable_DAT + slotBase);
        for (int i = 0; i < pageCount; i++) {
            if ((size_t)slotBase + (size_t)(i + 1) * sizeof(DWORD)
                > sizeof(g_TexturePageTable_DAT)) {
                break;
            }
            if (pageTable[i] != 0) {
                destroy_texture_page(pageTable[i]);
                pageTable[i] = 0;
            }
        }
    }

    if (dataOk) {
        BYTE* pageData = (BYTE*)&g_VideoDriverArray_4d0 + slotBase;
        for (int i = 0; i < 2; i++) {
            int handle = create_texture_page(pageData, (mode != 0) ? 26 : 10);
            if (handle == 0) break;
            pageData += 0x68;
        }
    }

    g_titleTextureSlotId = slot;
}

// ============================================================================
// init_title_screen (0x004306e0)
// ============================================================================
void init_title_screen(void)
{
    g_bGameActive = 0;
    set_display_resolution(320, 240, 0);

    g_roomCameraId = 0;

    LoadFile(GAME_DATA_ROOT "data\\title.pix", g_TimImageBuffer__bitmap, 0x20);
    display_image(0, g_TimImageBuffer__bitmap, 320, 240);

    title_setup_texture_pages(0, 1);

    //empty_00470960(0);

    const char* buttonTexPath;
    if (!g_bPadConnected) {
        buttonTexPath = GAME_DATA_ROOT "data\\t_press.tim";
    } else {
        buttonTexPath = GAME_DATA_ROOT "data\\t_start.tim";
    }
    LoadFile(buttonTexPath, g_TimImageBuffer__bitmap, 0x20);

    g_titleTexturePageData[4] = 26;
    g_titleTexturePageData[0] = 8;
    g_TextureCurrentPage = 26;
    g_TextureBankID = 8;
    LoadTexturePage(g_TimImageBuffer__bitmap, 8, 0, 12, 4, 0, 0, 0);

    g_titleTexturePageData[1] = g_TextureBankID;
    g_titleLoopFlag = 1;
    g_titleTexturePageData[5] = g_TextureCurrentPage;
    g_titleTexturePageData[2] = g_titleTexturePageData[1];
    g_titleTexturePageData[6] = g_titleTexturePageData[5];

    {
        char dbg[256];
        sprintf(dbg, "[INIT] LoadTexturePage done, SRV[27]=%p\n", g_TexturePageSRV[27]);
        OutputDebugStringA(dbg);
    }


    if (check_save_files_exist()) {
        g_titleSelectionId = 2;
        g_main_state_flags &= ~MSF_SCREEN_MODE_MASK;
        return;
    }
    g_titleSelectionId = 1;
    g_main_state_flags &= ~MSF_SCREEN_MODE_MASK;
}

// ============================================================================
// set_scene_render_param (0x0040a8e0)
// ============================================================================
void set_scene_render_param(int value)
{
    g_sceneRenderParam = value;
}

// ============================================================================
// title_exit_loop (0x00430e10)
// ============================================================================
void title_exit_loop(void)
{
    g_titleLoopFlag = 0;
    g_main_state_flags = (g_main_state_flags & ~MSF_SCREEN_MODE_MASK) | MSF_SCREEN_STANDALONE;
}

// ============================================================================
// fade_update (0x0047b950)
// ============================================================================
void fade_update(void)
{
    if (g_fading_state <= 0 && g_fading_counter != 0) {
        if (g_fading_counter <= 0) {
            g_fading_state = 0x7FFF;
        } else {
            g_fading_state = 0;
        }
    }
}

// ============================================================================
// read_sidewinder_pad (0x00497e30)
// Original: return g_pMasterInputState.field466_0x200 (joystick[0].currPress)
// ============================================================================
int read_sidewinder_pad(void)
{
    return g_pMasterInputState.joysticks[0].currPress;
}

// ============================================================================
// TitleTextPosData and UpdateTitleTextSprite
// ============================================================================
struct TitleTextPosData {
    int vramY;
    int sprHeight;
    int screenY;
};

static const TitleTextPosData g_titleTextPosTable[3] = {
    { 0,  54, 24 },  // index 0: "PRESS ANY BUTTON"
    { 83, 70,  8 },  // index 1: "NEW GAME"
    { 175,70,  8 },  // index 2: "LOAD GAME"
};

// ============================================================================
// UpdateTitleTextSprite (0x00430d40)
// ============================================================================
void UpdateTitleTextSprite(unsigned char brightness, unsigned char selectionId)
{
    TextureDesc* td = &g_TextureDesc;

    td->flags = 0x10000000;
    if (brightness != 0x80) {
        td->flags = 0x40000000;
    }

    const TitleTextPosData* entry = &g_titleTextPosTable[selectionId];

    td->texU = 0;
    td->screenX = -130;
    td->texturePage = g_titleTexturePageData[selectionId];
    td->width = 256;
    td->texV = (unsigned char)entry->vramY;
    td->height = entry->sprHeight;
    td->screenY = entry->screenY + 38;
    // g_titleCurrentSprH = (float)entry->sprHeight;

    td->colorMulR = brightness;
    td->clutX = 0;
    td->colorMulG = brightness;
    td->pivotX = 0;
    td->pivotY = 0;
    td->colorMulB = brightness;

    td->clutY = 0x1E0;

    display_texture(td, 2, 12, 1);
}

// ============================================================================
// CUSTOM (port-only): the title menu.
//
// The original screen is two pre-composed bitmaps - one per highlight state -
// each holding NEW GAME, LOAD GAME and the copyright lines together, centred.
// There is no way to move one item, and no art at all for anything else. So the
// three pieces were lifted out of t_start.tim pixel for pixel at bake time and
// live in the shared atlas (AchievementAtlasData.h), where each can be placed
// on its own; EXTRA and QUIT are lettered to match, same cap height, same
// tracking. All four go through one draw path, which is what keeps them
// looking like a single menu rather than two fonts sharing a screen.
// ============================================================================

#define TITLE_ITEM_NEW   0
#define TITLE_ITEM_LOAD  1
#define TITLE_ITEM_EXTRA 2
#define TITLE_ITEM_QUIT  3
#define TITLE_ITEM_COUNT 4

// Left-aligned column. x is the shared left edge of every word - the point of
// the layout - and the rows are evenly spaced above the copyright line.
//
// These are in the ORIGINAL's coordinate space, which is centre-relative: its
// own 256-wide block sits at screenX -130 and lands centred, so the screen
// offset this space is measured from is the middle of a 320-wide screen. Using
// the same space (and the same "+ g_ScreenOffsetX" the engine's own 2D applies)
// means the menu cannot drift away from the art behind it, whatever that offset
// turns out to be - the numbers are anchored to the original block's own -130,
// not to an assumption about where the origin sits.
// The column sits in the lower half but well clear of the copyright, which
// keeps the place it had inside the original block. It no longer has to dodge
// the logo: the menu gets a background of its own (see title_menu_backdrop).
#define TITLE_MENU_X     (-132)   // 28 px in from the left edge
#define TITLE_MENU_Y     (-48)    // first row at y 72
#define TITLE_MENU_STEP  (24)
#define TITLE_COPY_X     (-128)   // the 256-wide copyright block, centred
#define TITLE_COPY_Y     (96)

static int s_titleMenuIndex = 0;

// CUSTOM: the menu gets its own backdrop. The title art is a full-bleed logo
// over an eye - fine for PRESS ANY BUTTON, but it leaves nowhere for a list to
// live. sel_back is the game's own file-select plate: the same world, dark,
// no lettering of its own, and empty on the left where the column goes.
//
// display_image replaces the framebuffer the renderer composites the screen
// from, so this is a single call at the moment of the transition, not
// something to repeat per frame.
static void title_menu_backdrop(void)
{
    LoadFile(GAME_DATA_ROOT "data\\SEL_BACK.PIX", g_TimImageBuffer__bitmap, 0x20);
    display_image(0, g_TimImageBuffer__bitmap, 320, 240);
}

// The voice gate:
//   0 nothing started
//   1 asked for it, waiting for the mixer to actually begin
//   2 heard it start, waiting for it to end
//   3 it never started - hold for the clip's own length instead
//   5 the phrase is over: fading the title art down to black
//   4 done - the menu has it from here
static int s_titleVoiceState = 0;
static int s_titleVoiceFrames = 0;

// The first version asked getSndStat six frames after play_sfx and treated
// "not playing" as "finished". Six frames is a few milliseconds - XAudio2 had
// not queued the buffer yet - so the menu opened almost instantly while the
// line played on underneath it, and because the pad was still down the menu
// took that as a confirm and flashed into the character select. Hence: never
// conclude anything from the status until the sound has been SEEN playing.
#define TITLE_VOICE_START_FRAMES 90    // patience for the mixer to begin
#define TITLE_VOICE_NOMINAL      110   // evil01 is 3.44 s at a 30 Hz tick
#define TITLE_VOICE_MAX_FRAMES   400   // hard backstop, never reached normally

// Frames to ignore the button for after the menu opens, so the press that
// started the phrase cannot also pick the first item.
static int s_titleMenuGuard = 0;

static const AchvRect* title_item_art(int item)
{
    switch (item) {
    case TITLE_ITEM_NEW:   return &g_achvMarkWordnew;
    case TITLE_ITEM_LOAD:  return &g_achvMarkWordload;
    case TITLE_ITEM_EXTRA: return &g_achvMarkWordextra;
    default:               return &g_achvMarkWordquit;
    }
}

// Game space -> backbuffer, the same transform AddTintSprite applies to
// everything else on this screen (screen offset first, then the render scale).
static void title_blit(const AchvRect* r, float x, float y, unsigned int color)
{
    float sx, sy;
    MarniGetRenderScale(&sx, &sy);
    const float gx = (x + (float)g_ScreenOffsetX) * sx;
    const float gy = (y + (float)g_ScreenOffsetY) * sy;
    UiAtlas_PushPixel(r, gx, gy, (float)r->w * sx, (float)r->h * sy, color, 520u);
}

// `dim` fades the whole menu with the screen, so it follows the original's
// fades instead of sitting at full brightness through them.
static void title_draw_menu(unsigned char dim)
{
    if (!UiAtlas_Ready()) return;

    const unsigned int lit  = 0xFF000000u | (0xFFFFFFu);
    const unsigned int idle = 0xFF000000u | (0x6E6E6Eu);

    for (int i = 0; i < TITLE_ITEM_COUNT; i++) {
        const AchvRect* art = title_item_art(i);
        unsigned int c = (i == s_titleMenuIndex) ? lit : idle;
        // scale the colour by the fade the caller asked for
        const unsigned int k = dim;
        unsigned int r = (((c >> 16) & 0xFF) * k) >> 7;
        unsigned int g = (((c >> 8) & 0xFF) * k) >> 7;
        unsigned int b = ((c & 0xFF) * k) >> 7;
        if (r > 255) r = 255;
        if (g > 255) g = 255;
        if (b > 255) b = 255;
        title_blit(art, (float)TITLE_MENU_X,
                   (float)(TITLE_MENU_Y + i * TITLE_MENU_STEP),
                   0xFF000000u | (r << 16) | (g << 8) | b);
    }

    // the copyright, centred as it was inside the original block
    {
        unsigned int v = (0x6Eu * (unsigned int)dim) >> 7;
        if (v > 255) v = 255;
        title_blit(&g_achvMarkWordcopy, (float)TITLE_COPY_X, (float)TITLE_COPY_Y,
                   0xFF000000u | (v << 16) | (v << 8) | v);
    }
}

// Move the cursor. Returns 1 when it moved, so the caller can restart the
// attract-demo countdown exactly as the original did.
static int title_menu_move(unsigned short padPressed)
{
    if (padPressed & 0x1100) {          // up
        s_titleMenuIndex = (s_titleMenuIndex + TITLE_ITEM_COUNT - 1) % TITLE_ITEM_COUNT;
        return 1;
    }
    if (padPressed & 0x4000) {          // down
        s_titleMenuIndex = (s_titleMenuIndex + 1) % TITLE_ITEM_COUNT;
        return 1;
    }
    return 0;
}

// ============================================================================
// update_title_options (0x00430810)
// ============================================================================
void update_title_options(void)
{
	DWORD sidewinderPress = 0;
	DWORD sidewinderState = 0;
	if (g_bPadConnected) {
		sidewinderState = read_sidewinder_pad();
		sidewinderPress = sidewinderState & 0x10000 & ~g_PlayerPadHeldPrev;
	}
	g_PlayerPadHeldPrev = sidewinderState;

    if (g_titleMode != 0) {
        if (g_titleMode != 1) return;

        switch (g_titleOptionsFading) {
        case 0:
            g_titleOptionsFading = 1;
            g_fade_type_id = 2;
            g_fading_counter = 0xFC00;
            g_main_state_flags = (g_main_state_flags & ~MSF_SCREEN_MODE_MASK) | MSF_SCREEN_REBUILD;
            fade_update();
            return;

        case 1:
            if (g_fading_state < 0) {
                g_titleOptionsFading = 2;
                g_titleDemoTime = 0x708;
            }
            title_draw_menu(128);
            return;

	case 2:
		// CUSTOM: the skinned menu replaces the original's two-item sprite.
		title_draw_menu(128);

		// The button that started the title phrase is often still down when
		// the menu appears; without this it would be read as picking the first
		// item the moment the list showed up.
		if (s_titleMenuGuard > 0) {
			s_titleMenuGuard--;
			if ((g_PlayerPadPressed & 0xeff) || sidewinderPress) {
				s_titleMenuGuard = 2;    // still held: keep waiting
			}
			g_titleDemoTime--;
			break;
		}

		if ((g_PlayerPadPressed & 0xeff) || sidewinderPress) {
			// EXTRA has nowhere to go yet - it is a placeholder for a screen
			// that does not exist, so it simply refuses rather than starting
			// a fade into nothing.
			if (s_titleMenuIndex == TITLE_ITEM_EXTRA) {
				g_titleDemoTime = 0x708;
				break;
			}
			if (s_titleMenuIndex == TITLE_ITEM_QUIT) {
				// The same close the window's own X button takes: WM_DESTROY
				// runs the settings save and the sound-bank teardown before
				// the message loop returns.
				plat_window_destroy(g_hWnd);
				return;
			}
			// NEW GAME and LOAD GAME keep the original's exit codes -
			// title_state switches on g_titleSelectionId, 1 = character
			// select, 2 = load.
			g_titleSelectionId = (s_titleMenuIndex == TITLE_ITEM_NEW) ? 1 : 2;
			play_sfx(SFX_BANKS, 1); // null sfx
			g_titleOptionsFading = 6;
			g_fade_type_id = 1;
			g_fading_counter = 0x7F00;
			fade_update();
			g_bGameActive = 2;
			return;
		}

		if (title_menu_move((unsigned short)g_PlayerPadPressed)) {
			g_titleDemoTime = 0x708;
		}

            // (the original's demo_reset label is gone with the goto that used
            // it: the two-item wrap-around it existed for is now handled by
            // title_menu_move.)
            g_titleDemoTime--;
            if (g_titleDemoTime != 0) break;

            g_titleOptionsFading = 3;
            g_fade_type_id = 2;
            g_fading_counter = 0x400;
            fade_update();
            return;

        case 3:
            if (g_fading_state < 0) {
                g_titleSelectionId = 0;
                title_exit_loop();
                return;
            }
            title_draw_menu(128);
            if ((g_PlayerPadPressed & 0xeff) == 0) {
                return;
            }
            g_titleOptionsFading = 0;
            g_fading_counter = 0xF000;
            return;

        case 4:
            if (g_fading_state < 0) {
                title_exit_loop();
                g_main_state_flags &= MSF_SCREEN_MODE_MASK;
                return;
            }
            title_draw_menu(128);

        case 6:
            if (g_fading_state < 0) {
                g_titleOptionsFading = 7;
                g_fade_type_id = 1;
                g_fading_counter = 0xC000;
                fade_update();
                title_draw_menu(128);
                return;
            }

        case 8:
            if (g_fading_state < 0) {
                g_titleOptionsFading = 9;
                g_fade_type_id = 1;
                g_fading_counter = 0xF800;
                fade_update();
                title_draw_menu(128);
                return;
            }
            break;

        case 7:
            if (g_fading_state < 0) {
                g_titleOptionsFading = 8;
                g_fade_type_id = 1;
                g_fading_counter = 0x8000;
                fade_update();
                title_draw_menu(0x80);
                return;
            }

        case 9:
            if (g_fading_state < 0) {
                g_titleOptionsFading = 4;
                g_fade_type_id = 2;
                g_fading_counter = 0x270;
                fade_update();
                title_draw_menu(0x80);
                return;
            }

        default:
            break;
        }

        title_draw_menu(0x80);
        return;
    }

    switch (g_titleOptionsFading) {
        case 0:
            g_titleOptionsFading = 1;
            g_titleDemoTime = 0x80;
            goto option_selected;
        case 1:
    option_selected:
            g_titleDemoTime -= 4;
            UpdateTitleTextSprite(-0x80 - (char)g_titleDemoTime, 0);
            if (g_titleDemoTime == 0) {
                g_titleOptionsFading = 2;
                g_titleDemoTime = 0x708;
            }
            if ((g_PlayerPadPressed & 0xeff) || sidewinderPress) {
                g_titleOptionsFading = 2;
                g_titleDemoTime = 0x708;
            }
            break;

        case 2:
            // The prompt is only up while the screen is still waiting for a
            // button. Once one is pressed the phrase takes over, so PRESS ANY
            // BUTTON goes out with the flash below rather than sitting on
            // screen through a line that is already answering it.
            if (s_titleVoiceState == 0 || s_titleVoiceState == 4) {
                UpdateTitleTextSprite(0x80, 0);
            }

            // CUSTOM: the game says its own name here. The press starts the
            // line, the screen holds on PRESS ANY BUTTON while it plays, and
            // only when it has finished does the menu open - so the two never
            // talk over each other. Further presses are ignored on purpose:
            // the phrase is the opening beat, not a thing to click past.
            // The handover. The menu is a different picture on a different
            // backdrop, so cutting to it the instant the phrase stops is a
            // jump however the menu then fades up. Instead the title art fades
            // out first, the swap happens under full black, and the fade back
            // in is armed in the SAME frame - so the black never lifts between
            // the two and the whole thing reads as one move.
            if (s_titleVoiceState == 5) {
                if (g_fading_state > 0x7B80) {
                    s_titleMenuIndex = TITLE_ITEM_NEW;
                    s_titleMenuGuard = 8;
                    title_menu_backdrop();
                    s_titleVoiceState = 4;
                    g_titleMode = 1;
                    // Step 1, not 0: step 0 would arm the fade a frame later,
                    // and that frame would show the new backdrop at full
                    // brightness before the black arrived - a flash of the
                    // menu before its own entrance. Arm it here instead and
                    // let step 1 do what it exists for: hold until it clears.
                    g_titleOptionsFading = 1;
                    g_fading_state = 0;
                    g_fade_type_id = 2;
                    g_fading_counter = 0xFC00;
                    fade_update();
                    g_main_state_flags = (g_main_state_flags & ~MSF_SCREEN_MODE_MASK) | MSF_SCREEN_REBUILD;
                }
                break;
            }

            if (s_titleVoiceState != 0 && s_titleVoiceState != 4) {
                const int handle = g_SfxBanks[SFX_TITLE_EVIL01 * 2];
                int done = 0;
                s_titleVoiceFrames++;

                if (s_titleVoiceState == 1) {
                    if (handle != 0 && getSndStat(handle) == 1) {
                        s_titleVoiceState = 2;          // it is really playing
                        s_titleVoiceFrames = 0;
                    } else if (s_titleVoiceFrames >= TITLE_VOICE_START_FRAMES) {
                        // No device, or the bank never loaded. Hold anyway: the
                        // beat is the same length whether it is heard or not.
                        s_titleVoiceState = 3;
                        s_titleVoiceFrames = 0;
                    }
                } else if (s_titleVoiceState == 2) {
                    done = (getSndStat(handle) != 1)
                           || (s_titleVoiceFrames > TITLE_VOICE_MAX_FRAMES);
                } else {
                    done = (s_titleVoiceFrames >= TITLE_VOICE_NOMINAL);
                }

                if (done) {
                    // Start the fade out. 0x400 a frame is the same rate the
                    // title itself uses to leave when it times out, and it is
                    // matched by the fade in on the other side, so the two
                    // halves of the handover are the same length.
                    s_titleVoiceState = 5;
                    g_fading_state = 0;
                    g_fade_type_id = 2;
                    g_fading_counter = 0x400;
                    fade_update();
                }
                break;
            }

            g_titleDemoTime--;
            if (g_titleDemoTime == 0) {
                g_titleOptionsFading = 3;
                g_fade_type_id = 2;
                g_fading_counter = 0x400;
                fade_update();
            }
            if ((g_PlayerPadPressed & 0xeff) || sidewinderPress) {
                play_sfx(SFX_BANKS, SFX_TITLE_EVIL01);
                s_titleVoiceState = 1;
                s_titleVoiceFrames = 0;

                // The press gets the same white flash a menu pick gets, then
                // the picture comes back out of it.
                //
                // g_fade_type_id picks the COLOUR, and it is not a brightness:
                // draw_rect reads it as a variant, 1 = the white overlay and
                // 2 = the black one. This was set to 2, which is why the press
                // produced no white - it was drawing the black veil at an
                // alpha that started at full and fell away, i.e. a fade-in
                // from black over a screen that was already there.
                //
                // Armed the way the game arms a fade-IN: full level on the
                // press frame, minus 0x400 every frame after, so white covers
                // the screen at once and then thins out over about a second
                // while the phrase plays. fade_update only arms from a level
                // of 0 or less, and what the level is here is left over from
                // the title's own fade-in, so it is put there rather than
                // assumed.
                g_fading_state = 0;
                g_fade_type_id = 1;
                g_fading_counter = 0xFC00;
                fade_update();
            }
            break;

        case 3:
            if (g_fading_state > 0x7B80) {
                g_fading_state = 0x7FFF;
                g_fading_counter = 0;
                g_titleSelectionId = 0;
                g_main_state_flags = (g_main_state_flags & ~MSF_SCREEN_MODE_MASK) | MSF_SCREEN_STANDALONE;
                title_exit_loop();
            }
            if ((g_PlayerPadPressed & 0xeff) || sidewinderPress) {
                g_titleOptionsFading = 2;
                g_fading_state = -1;
                g_titleDemoTime = 0x708;
            }
            UpdateTitleTextSprite(0x80, 0);
            break;

        case 4:
            UpdateTitleTextSprite(0x80, 0);
            if (g_fading_state > 0x7B80) {
                g_titleMode = 1;
                g_titleOptionsFading = 0;
                g_fading_state = 0x7FFF;
                g_fading_counter = 0;
                g_main_state_flags = (g_main_state_flags & ~MSF_SCREEN_MODE_MASK) | MSF_SCREEN_STANDALONE;
            }
            break;
    }
}

// ============================================================================
// title_state (0x00430470)
// Title screen state: displays title, waits for player selection,
// then chains to game_start, logos_state, or characterSelectionScreen.
// ============================================================================
void title_state(void)
{
    int i;

	g_main_state_flags &= ~MSF_INTENSITY_RAMP;
	g_PlayerPadHeldPrev = 0;
	g_PlayerPadHeld = 0;
	g_RawPadHeld = 0;
	g_PlayerPadPressed = 0;
	g_playingGameFlag = 0;
	g_menu_choice_id = 0;

    setMenuScreenOffset(320, 240, 0, 0, 0);
    CenterScreenOrigin();
    clear_textures();
    set_scene_render_param(0xc0);
    sounds_reset();

    g_loadDataDestPointer = g_DataBuffer;
    LoadSoundBank(BANK_TITLE, g_DataBuffer);

    g_fading_state = -1;
    g_titleLoopFlag = 0;
    g_SpecialRoomLightState = 0xFFFF;
    g_titleMode = 0;
    g_titleOptionsFading = 0;

    init_title_screen();

    // empty_00497c10(0);
    // empty_0040abb0((void*)0, 0, 0, 0);

    g_main_state_flags = (g_main_state_flags & ~MSF_SCREEN_MODE_MASK) | MSF_SCREEN_STANDALONE;
    // 0x0047b950 call site: the original calls 0x00483510 here, a stub that
    // just returns 0 - call dropped

    setMenuScreenOffset(320, 240, 0, 0, 1);
    Task_sleep(1);

    if (g_fmvPlayCount < 1) {
        g_selectedFmvId = 0;
        g_fmvDataPointer = g_loadDataDestPointer;
        g_fmvPlayCount = 0x10;
        g_main_state_flags = g_main_state_flags | MSF_FMV_REQUEST;
        Task_sleep(1);
    }

    g_main_state_flags = (g_main_state_flags & ~MSF_SCREEN_MODE_MASK) | MSF_SCREEN_REBUILD;
    Task_sleep(1);

    do {
        update_title_options();
        Task_sleep(1);
    } while (g_titleLoopFlag != 0);

    // legacy gpu wait
    if (g_GPU_VENDOR_ID == 1) {
        for (i = 180; i != 0; i--) {
            Task_sleep(1);
        }
    }

    cleanup_texture_slot(12);

    switch (g_titleSelectionId) {
    case 0:
        nullsub_0047eb80();
        g_main_state_flags2 |= MSF2_ATTRACT_DEMO;
        Task_chain((void*)game_start);
        Task_chain((void*)logos_state);
        return;

    case 1:
        nullsub_0047eb80();
        Task_chain((void*)characterSelectionScreen);

    case 2:
    case 3:
        g_main_state_flags = (g_main_state_flags & ~MSF_SCREEN_MODE_MASK) | MSF_SCREEN_STANDALONE;
        LoadSaveGameState(1, 0x80180000, 0, 1, 0);
        g_loadSaveStateFlag = 0;
        Game_timer = g_gameTimerSnapshot;
        g_main_state_flags = (g_main_state_flags & ~MSF_SCREEN_MODE_MASK) | MSF_SCREEN_STANDALONE;
        nullsub_0047eb80();
        Task_chain((void*)game_start);

    default:
        return;
    }
}

// nullsub_0047eb80 - empty no-op in the original PC build.
// likely a PS1 version function stripped during the PC port.
void nullsub_0047eb80(void) { }
