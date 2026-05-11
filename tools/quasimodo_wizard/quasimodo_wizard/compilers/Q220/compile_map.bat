@echo off
setlocal EnableDelayedExpansion
:: Quasimodo Q220 — bundled q2tool (BSP / VIS / RAD). No committed drive letters.
:: Argument: one .map file path. Wizard sets QUASIMODO_WIZARD_COMPILE=1 (skip pause).
:: Optional: QUASIMODO_GAMEDIR, QUASIMODO_BASEDIR (otherwise derive repo root from this folder).

set "TOOLDIR=%~dp0"
set "TOOLDIR=%TOOLDIR:~0,-1%"

if "%~1"=="" (
  echo.
  echo  Drag and drop a .map file onto this script to compile it.
  echo.
  if not defined QUASIMODO_WIZARD_COMPILE pause
  exit /b 1
)

if defined QUASIMODO_GAMEDIR (
  set "GAMEDIR=!QUASIMODO_GAMEDIR!"
) else (
  pushd "!TOOLDIR!\..\..\..\..\.." 2>nul
  if errorlevel 1 (
    echo ERROR: Could not resolve repository root from Q220 folder.
    echo Set QUASIMODO_GAMEDIR and QUASIMODO_BASEDIR, or run from the source tree.
    if not defined QUASIMODO_WIZARD_COMPILE pause
    exit /b 1
  )
  set "GAMEDIR=!CD!"
  popd
)

if defined QUASIMODO_BASEDIR (
  set "BASEDIR=!QUASIMODO_BASEDIR!"
) else (
  set "BASEDIR=!GAMEDIR!\baseq2"
)

set "MAPFILE=%~1"
set "MAPNAME=%~n1"
set "MAPDIR=%~dp1"

echo.
echo  ============================================================
echo   Compiling: !MAPNAME!.map
echo  ============================================================
echo.

echo  [1/3] Running BSP...
"%TOOLDIR%\q2tool" -bsp -qbsp "!MAPFILE!"
if errorlevel 1 goto error

echo.
echo  [2/3] Running VIS...
"%TOOLDIR%\q2tool" -vis "!MAPDIR!!MAPNAME!.bsp"
if errorlevel 1 goto error

echo.
echo  [3/3] Running RAD...
"%TOOLDIR%\q2tool" -rad -extra -bounce 4 -gamedir "!GAMEDIR!" -basedir "!BASEDIR!" "!MAPDIR!!MAPNAME!.bsp"
if errorlevel 1 goto error

echo.
echo  ============================================================
echo   Done! BSP is at:
echo   !MAPDIR!!MAPNAME!.bsp
echo  ============================================================
echo.
if not defined QUASIMODO_WIZARD_COMPILE pause
exit /b 0

:error
echo.
echo  ============================================================
echo   ERROR: Compilation failed. Check output above.
echo  ============================================================
echo.
if not defined QUASIMODO_WIZARD_COMPILE pause
exit /b 1
