@echo off
REM Build LilithWidget SKSE DLL
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" amd64
set PATH=C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%PATH%
cd /d %~dp0

if not exist build\release (
    echo === CONFIGURING ===
    cmake --preset release
    if errorlevel 1 (
        echo === CONFIGURE FAILED ===
        exit /b 1
    )
)

echo === BUILDING ===
cmake --build build/release
if errorlevel 1 (
    echo === BUILD FAILED ===
    exit /b 1
)

echo === BUILD SUCCEEDED ===
if exist build\release\LilithWidget.dll (
    if defined SKYRIM_MODS_FOLDER if not exist "%SKYRIM_MODS_FOLDER%\Lilith Widget--Claude\SKSE\Plugins" mkdir "%SKYRIM_MODS_FOLDER%\Lilith Widget--Claude\SKSE\Plugins"
    if defined SKYRIM_MODS_FOLDER copy /y build\release\LilithWidget.dll "%SKYRIM_MODS_FOLDER%\Lilith Widget--Claude\SKSE\Plugins\LilithWidget.dll"
    echo === DEPLOYED TO MO2 ===
) else (
    echo === DLL NOT FOUND ===
    exit /b 1
)
