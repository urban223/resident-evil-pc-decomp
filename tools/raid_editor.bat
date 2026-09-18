@echo off
rem raid_editor.bat - the browser editor's launcher. Kept only to say where the
rem editor went.
rem
rem There is one editor now, and it is in the game: run the game, start RAID
rem mode, and press F2. The viewport you fly around is the room the renderer
rem draws, lit by the engine's own lighting, holding the models the game
rem loaded - and the Play button runs the level in that same viewport, in the
rem same process. Nothing has to agree with anything, because there is only
rem one of everything.
rem
rem tools\raid_editor.html and tools\raid_editor_server.py are what this used
rem to start. The .html has since been deleted from the tree, so the server has
rem nothing left to serve and answers 404 for it; both it and this file can go.
echo.
echo   The RAID level editor is now part of the game.
echo.
echo   Run the game, start RAID mode, and press F2.
echo   F5 plays the level in the editor's viewport, Esc stops.
echo   Ctrl+S writes Data\raid1.lvl.
echo.
pause
