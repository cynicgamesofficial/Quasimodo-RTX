@echo off
:: ============================================================
:: Quake 2 Map Compiler
:: Place this .bat in the same folder as q2tool.exe
:: Drag and drop your .map file onto this .bat to compile
:: ============================================================

:: Tool lives next to this bat
set TOOLDIR=%~dp0
set TOOLDIR=%TOOLDIR:~0,-1%

set MAPFILE=%~1
set MAPNAME=%~n1
set MAPDIR=%~dp1

if "%MAPFILE%"=="" (
    echo.
    echo  Drag and drop a .map file onto this script to compile it.
    echo.
    pause
    exit /b
)

echo.
echo  ============================================================
echo   Compiling: %MAPNAME%.map
echo  ============================================================
echo.

:: --- BSP ---
echo  [1/3] Running BSP...
"%TOOLDIR%\q2tool" -bsp -qbsp "%MAPFILE%"
if errorlevel 1 goto error

:: --- VIS ---
echo.
echo  [2/3] Running VIS...
"%TOOLDIR%\q2tool" -vis "%MAPDIR%%MAPNAME%.bsp"
if errorlevel 1 goto error

:: --- RAD ---
echo.
echo  [3/3] Running RAD...
"%TOOLDIR%\q2tool" -rad -extra -bounce 4 "%MAPDIR%%MAPNAME%.bsp"
if errorlevel 1 goto error

echo.
echo  ============================================================
echo   Done! BSP is at:
echo   %MAPDIR%%MAPNAME%.bsp
echo  ============================================================
echo.
pause
exit /b

:error
echo.
echo  ============================================================
echo   ERROR: Compilation failed. Check output above.
echo  ============================================================
echo.
pause
exit /b
