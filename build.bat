@echo off
setlocal EnableExtensions EnableDelayedExpansion

set "ROOT_DIR=%~dp0"
if "%ROOT_DIR:~-1%"=="\" set "ROOT_DIR=%ROOT_DIR:~0,-1%"
set "BUILD_GRAPHICAL=1"
set "BUILD_HEADLESS=0"
set "HEADLESS_PROFILE=headless"
set "BUILD_EXAMPLES=ON"
set "BUILD_TESTS=ON"
set "BUILD_RMLUI=OFF"
set "CLEAN=0"
set "CONFIG=Release"
set "JOBS=%NUMBER_OF_PROCESSORS%"
if "%JOBS%"=="" set "JOBS=2"

:parse_args
if "%~1"=="" goto after_parse
if /I "%~1"=="--headless" (
  set "BUILD_HEADLESS=1"
) else if /I "%~1"=="--headless-only" (
  set "BUILD_GRAPHICAL=0"
  set "BUILD_HEADLESS=1"
) else if /I "%~1"=="--minimal-headless" (
  set "BUILD_HEADLESS=1"
  set "HEADLESS_PROFILE=minimal-headless"
) else if /I "%~1"=="--no-examples" (
  set "BUILD_EXAMPLES=OFF"
) else if /I "%~1"=="--no-tests" (
  set "BUILD_TESTS=OFF"
) else if /I "%~1"=="--rmlui" (
  set "BUILD_RMLUI=ON"
) else if /I "%~1"=="--clean" (
  set "CLEAN=1"
) else if /I "%~1"=="--config" (
  shift
  if "%~1"=="" goto missing_config
  set "CONFIG=%~1"
) else if /I "%~1"=="--jobs" (
  shift
  if "%~1"=="" goto missing_jobs
  set "JOBS=%~1"
) else if /I "%~1"=="-j" (
  shift
  if "%~1"=="" goto missing_jobs
  set "JOBS=%~1"
) else if /I "%~1"=="--help" (
  goto usage
) else if /I "%~1"=="-h" (
  goto usage
) else (
  echo error: unknown option "%~1" 1>&2
  goto usage_error
)
shift
goto parse_args

:after_parse
if "%BUILD_GRAPHICAL%"=="1" call :run_profile portable || exit /b 1
if "%BUILD_HEADLESS%"=="1" call :run_profile %HEADLESS_PROFILE% || exit /b 1
exit /b 0

:run_profile
set "PROFILE=%~1"
set "BUILD_DIR=%ROOT_DIR%\build\%PROFILE%"

if /I "%PROFILE%"=="portable" (
  set "PROFILE_ARGS=-DKARMA_HEADLESS=OFF -DKARMA_BUILD_RMLUI_DEMO=%BUILD_RMLUI%"
) else if /I "%PROFILE%"=="headless" (
  set "PROFILE_ARGS=-DKARMA_HEADLESS=ON"
) else if /I "%PROFILE%"=="minimal-headless" (
  set "PROFILE_ARGS=-DKARMA_HEADLESS=ON -DKARMA_ENABLE_AUDIO=OFF -DKARMA_ENABLE_NAVIGATION=OFF -DKARMA_NETWORK_BACKEND_ENET=OFF -DKARMA_PHYSICS_BACKEND_JOLT=OFF -DKARMA_PHYSICS_BACKEND_BULLET=OFF"
) else (
  echo error: unknown build profile "%PROFILE%" 1>&2
  exit /b 2
)

if "%CLEAN%"=="1" (
  cmake -E rm -rf "%BUILD_DIR%" || exit /b 1
)

echo ==^> Configuring %PROFILE%
cmake -S "%ROOT_DIR%" -B "%BUILD_DIR%" -DKARMA_FETCH_DEPS=ON -DKARMA_BUILD_EXAMPLES=%BUILD_EXAMPLES% -DKARMA_BUILD_TESTS=%BUILD_TESTS% -DBUILD_TESTING=%BUILD_TESTS% -DCMAKE_BUILD_TYPE=%CONFIG% %PROFILE_ARGS% || exit /b 1

echo ==^> Building %PROFILE%
cmake --build "%BUILD_DIR%" --config "%CONFIG%" --parallel %JOBS% || exit /b 1
exit /b 0

:missing_config
echo error: --config requires a value 1>&2
exit /b 2

:missing_jobs
echo error: --jobs requires a value 1>&2
exit /b 2

:usage_error
call :print_usage
exit /b 2

:usage
call :print_usage
exit /b 0

:print_usage
echo Usage: build.bat [options]
echo.
echo Default: configure and build the normal graphical profile, including examples
echo and tests. Headless is not built unless requested.
echo.
echo Options:
echo   --headless              Also build the regular headless profile.
echo   --headless-only         Build only the regular headless profile.
echo   --minimal-headless      Build the smaller headless profile.
echo   --no-examples           Do not generate or build example executables.
echo   --no-tests              Do not generate or build test executables.
echo   --rmlui                 Build the RmlUi adapter/demo in graphical builds.
echo   --clean                 Remove selected build directories before configure.
echo   --config ^<name^>         Build configuration. Default: Release.
echo   --jobs ^<count^>          Parallel build job count.
echo   -h, --help              Show this help.
echo.
echo Examples:
echo   build.bat
echo   build.bat --headless
echo   build.bat --headless-only --minimal-headless --no-examples --no-tests
echo   build.bat --no-examples --config Debug --jobs 4
exit /b 0
