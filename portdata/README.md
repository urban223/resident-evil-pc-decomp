# portdata — the runtime assets this port added

Everything under here was produced by this project and is tracked in git.
`tools/deploy_portdata.py` copies it into the three data trees the game
actually reads from. See `docs/ASSETS.md` for the whole picture.

`assets/` is a different thing and is **not** tracked: it is the player's own
copy of the game. Nothing of Capcom's belongs here.

| file | made by | what it is |
|---|---|---|
| `USA/Data/edui.bin` | `tools/build_editor_ui.py` | the editor's fonts, plates and icons |
| `USA/Data/achvui.bin` | `tools/build_achievement_ui.py` | the Space GUI atlas: toast, status skin, title word art |
| `USA/Data/raid1.lvl` | the in-game editor | the RAID arena level |
| `USA/Item_m2/IFLR.ivm` | authored from scratch | the flare pistol's examine model |
| `USA/Item_m2/IACD.ivm` | authored from scratch | the acid pistol's examine model |
| `USA/Item_m2/IFRZ.ivm` | authored from scratch | the freeze pistol's examine model |
| `USA/Sound/achv.wav` | `tools/build_achievement_sfx.py` | the achievement unlock cue |
| `USA/Sound/raid.wav` | `tools/build_raid_sfx.py` | the RAID screen's sting |
| `USA/Sound/raidbgm.wav` | `tools/build_raid_bgm.py` | the RAID screen's loop |

The three `.ivm` models are here because they share nothing with the game's
files: own geometry, own 256x256 texture, own palette — `docs/IVM_MODEL_FORMAT.md`
covers how they were built and `docs/GRENADE_PISTOL.md` why. Their in-hand
counterparts `players/{w1f,w2f,w3f}.emw` are **not** here, and cannot be: each
carries Jill's animation half byte for byte.

The two `.bin` atlases are baked from the Space GUI pack, which is licensed for
use in a product but not for redistribution as a pack — so the baked sheets are
here and `assets/SpaceGUI/` is not. That also means
`tools/build_editor_ui.py` and `tools/build_achievement_ui.py` cannot be re-run
from a bare clone; they do not need to be, because their output is tracked.

**Not here, and deliberately:** anything carrying Capcom's own art or audio —
`titlebg.pix`, `titlelogo.bin` (cut out of `title.pix`), `raideye.bin` (a frame
of `ou.avi`), the barrelled `W12.EMW`, the custom pistols' in-hand `.emw`
models, and the RAID room's RDT. Their generators are tracked — except the
in-hand `.emw`, which has none; `docs/ASSETS.md` says what that costs a clone.
