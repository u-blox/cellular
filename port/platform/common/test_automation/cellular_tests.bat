@echo off
echo This batch file sets up the environment variables for building/testing the cellular driver https://github.com/u-blox/cellular.
echo.

setlocal EnableDelayedExpansion

rem Local variables
set fetch=
set create_dir=
set clean_code_dir=
set clean_build_dir=
set platform_number=
set platform_string=
set code_directory=
set config_directory=%~dp0config
set com_port=
set espidf_repo_root=
set CELLULAR_FLAGS=
set return_value=1

rem Need to set this to avoid odd problems with code page cp65001
rem and Python
set PYTHONIOENCODING=utf-8

rem Platform descriptions.  If you add a new one, don't forget to increment num_platforms.
rem Note: special characters need to be escaped (i.e. ^) in these platform descriptions,
rem see https://www.robvanderwoude.com/escapechars.php for a list of characters that need escaping.
set platform_1=unit tests under v4 Espressif SDK from u-blox clone of repo on NINA-W1 with a SARA-R4 module on a WHRE board
set platform_2=unit tests under latest v4 Espressif SDK on ESP32 W-ROVER board with a SARA-R4 module
set platform_3=unit tests under latest v4 Espressif SDK on ESP32 W-ROVER board with a SARA-R5 module
set platform_4=Amazon-FreeRTOS SDK^, latest version^, on the ESP32 chipset ^(e.g. NINA-W1^)^, with a SARA-R4 module on a WHRE board
set platform_5=v4 Espressif SDK on NINA-W1^, with a SARA-R4 module on a WHRE board talking to AWS
set num_platforms=5

rem Process command line parameters
set pos=0
rem Retrieve arg removing quotes from paths (so that we can concatenate paths here)
set arg=%~1
rem pos represents the number of a positional argument
if not "%arg%"=="" (
    if "%arg%"=="/f" (
        set fetch=YES
    ) else if "%arg%"=="/d" (
        set create_dir=YES
    ) else if "%arg%"=="/c" (
        set clean_code_dir=YES
    ) else if "%arg%"=="/b" (
        set clean_build_dir=YES
    ) else if "%pos%"=="0" (
        set platform_number=%arg%
        set /A pos=pos+1
    ) else if "%pos%"=="1" (
        set code_directory=%arg%
        set /A pos=pos+1
    ) else if "%pos%"=="2" (
        set build_directory=%arg%
        set /A pos=pos+1
    ) else if "%pos%"=="3" (
        set com_port=%arg%
        set /A pos=pos+1
    ) else (
        echo %~n0: ERROR can't understand parameter "%arg%".
        goto usage
    )
    shift /1
    goto parameters
)

rem Check platform number and set platform string
if "%platform_number%"=="" (
    echo %~n0: ERROR must specify a platform.
    goto usage
)
echo %~n0: platform is %platform_number%.
if %platform_number% LEQ %num_platforms% (
    set platform_string=!platform_%platform_number%!
) else (
    echo %~n0: ERROR don't understand platform %platform_number%.
    goto usage
)

rem Check code directory, create/clean it if requested
if "%code_directory%"=="" (
    echo %~n0: ERROR must specify a code directory.
    goto usage
)
echo %~n0: code directory is "%code_directory%".
if exist "%code_directory%" (
    if not "%clean_code_dir%"=="" (
        echo %~n0: cleaning code directory...
        del /s /q /f "%code_directory%\*" 1>nul
        for /f "delims=" %%f in ('dir /ad /b "%code_directory%"') do rd /s /q "%code_directory%\%%f" 1>nul
    )
)
if not exist "%code_directory%" (
    if not "%create_dir%"=="" (
        echo %~n0: code_directory "%code_directory%" does not exist, creating it...
        md "%code_directory%"
        if not exist "%code_directory%" (
            echo %~n0: ERROR unable to create code directory "%code_directory%" ^(you must use backslash \, not /^).
            goto usage
        )
    ) else (
        echo %~n0: ERROR code directory "%code_directory%" does not exist and command-line switch /c was not specified.
        goto usage
    )
)

rem Check build directory, create/clean it if requested
if "%build_directory%"=="" (
    echo %~n0: ERROR must specify a build directory.
    goto usage
)
echo %~n0: build_directory is "%build_directory%".
if exist "%build_directory%" (
    if not "%clean_build_dir%"=="" (
        echo %~n0: cleaning build directory...
        del /s /q /f "%build_directory%\*" 1>nul
        for /f "delims=" %%f in ('dir /ad /b "%build_directory%"') do rd /s /q "%build_directory%\%%f" 1>nul
    )
)
if not exist "%build_directory%" (
    if not "%create_dir%"=="" (
        echo %~n0: build_directory "%build_directory%" does not exist, creating it...
        md "%build_directory%"
        if not exist "%build_directory%" (
            echo %~n0: ERROR unable to create build directory "%build_directory%" ^(you must use backslash \, not /^).
            goto usage
        )
    ) else (
        echo %~n0: ERROR build directory "%build_directory%" does not exist and command-line switch /c was not specified.
        goto usage
    )
)

rem Check COM port
if "%com_port%"=="" (
    echo %~n0: ERROR must specify a COM port, e.g. COM1.
    goto usage
)
echo %~n0: COM port is %com_port%.

rem Build start
echo.
echo ######## start of build/test for platform %platform_number%: %platform_string% ########
echo.
echo %~n0: changing code_directory to "%code_directory%"...
pushd "%code_directory%"
if not "%fetch%"=="" (
    if exist cellular (
        pushd cellular
        echo %~n0: updating cellular driver code...
        call git pull
        popd
    ) else (
        echo %~n0: cloning cellular driver from https://github.com/u-blox/cellular...
        call git clone https://github.com/u-blox/cellular.git
    )
) else (
    echo %~n0: not fetching any code, just building.
)   
goto build_platform_%platform_number%

rem Build end
:build_end
    echo.
    echo ######## end of build/test for platform %platform_number%: %platform_string% ########
    echo.
    goto end

rem Build unit tests under v4 Espressif SDK from u-blox clone of repo on NINA-W1 with a SARA-R4 module on a WHRE board
:build_platform_1
    set espidf_repo_root=u-blox
    set CELLULAR_FLAGS=-DCELLULAR_CFG_MODULE_SARA_R4
    echo %~n0: flags set for WHRE board to indicate SARA-R4.
    echo %~n0: will pull v4 ESP-IDF from https://github.com/%espidf_repo_root%/esp-idf ^(using u-blox branch for fix for flash chip version used in NINA-W1^)^, specify /b to avoid build collisions...
    goto build_platform_1_2_3

rem Build unit tests under latest v4 Espressif SDK on ESP32 W-ROVER board with a SARA-R4 module
:build_platform_2
    set espidf_repo_root=espressif
    echo %~n0: will pull latest v4 ESP-IDF from https://github.com/%espidf_repo_root%/esp-idf^, specify /b to avoid build collisions...
    set CELLULAR_FLAGS=-DCELLULAR_CFG_MODULE_SARA_R4 -DCELLULAR_CFG_PIN_RXD=19 -DCELLULAR_CFG_PIN_TXD=21 -DCELLULAR_CFG_PIN_VINT=-1 -DCELLULAR_CFG_PIN_ENABLE_POWER=-1
    echo %~n0: flags set for W-ROVER board to indicate SARA-R4^, RXD on D19^, TXD on D21^, no VINT or Enable Power pins connected.
    goto build_platform_1_2_3

rem Build unit tests under latest v4 Espressif SDK on ESP32 W-ROVER board with a SARA-R5 module
:build_platform_3
    set espidf_repo_root=espressif
    echo %~n0: will pull latest v4 ESP-IDF from https://github.com/%espidf_repo_root%/esp-idf^, specify /b to avoid build collisions...
    set CELLULAR_FLAGS=-DCELLULAR_CFG_MODULE_SARA_R5 -DCELLULAR_CFG_PIN_RXD=19 -DCELLULAR_CFG_PIN_TXD=21 -DCELLULAR_CFG_PIN_VINT=-1 -DCELLULAR_CFG_PIN_ENABLE_POWER=-1
    echo %~n0: flags set for W-ROVER board to indicate SARA-R5^, RXD on D19^, TXD on D21^, all pins connected up.
    goto build_platform_1_2_3

rem Build platforms 1, 2 or 3: unit tests under v4 Espressif SDK on ESP32 chipset with SARA-R4 or SARA-R5
:build_platform_1_2_3
    if not "%fetch%"=="" (
        if exist esp-idf (
            pushd esp-idf
            echo %~n0: updating ESP-IDF code...
            call git pull
            popd
        ) else (
            echo %~n0: cloning ESP-IDF from https://github.com/%espidf_repo_root%/esp-idf into esp-idf-%espidf_repo_root%...
            call git clone https://github.com/%espidf_repo_root%/esp-idf esp-idf-%espidf_repo_root%
        )
    )
    echo %~n0: setting up paths assuming Python 2.7 is in "C:\Python27"...
    set path=C:\Python27;C:\Python27\Scripts;%path%
    echo %~n0: print version information start
    python --version
    echo %~n0: print version information end
    echo %~n0: setting environment variables assuming the latest ESP-IDF tools will be installed in C:\Program Files\Espressif\ESP-IDF Tools latest...
    @echo on
    set IDF_TOOLS_PATH=C:\Program Files\Espressif\ESP-IDF Tools latest
    @echo off
    rem To run the installation process we need to fiddle with files in ESP-IDF Tools, which is in Program Files and requires
    rem administrator privilages, so check for that here, see:
    rem https://stackoverflow.com/questions/1894967/how-to-request-administrator-access-inside-a-batch-file/10052222#10052222
    if "%PROCESSOR_ARCHITECTURE%"=="amd64" (
        >nul 2>&1 "%SYSTEMROOT%\SysWOW64\cacls.exe" "%SYSTEMROOT%\SysWOW64\config\system"
    ) else (
        >nul 2>&1 "%SYSTEMROOT%\system32\cacls.exe" "%SYSTEMROOT%\system32\config\system"
    )
    rem If error flag is set, we do not have admin.
    if not "%errorlevel%"=="0" (
        echo %~n0: ERROR administrator privileges are required to run the ESP-IDF installation batch file, please run as administrator.
        goto build_end
    )
    echo %~n0: calling ESP-IDF install.bat from ESP-IDF code_directory esp-idf-%espidf_repo_root%...
    pushd esp-idf-%espidf_repo_root%
    echo %~n0: %CD%
    call install.bat
    echo %~n0: calling ESP-IDF export.bat from ESP-IDF code_directory...
    call export.bat
    popd
    echo %~n0: building tests and then downloading them over %com_port%...
    echo %~n0: building in %build_directory%\esp-idf-%espidf_repo_root% to keep paths short
    @echo on
    idf.py -p %com_port% -C cellular\port\platform\espressif\sdk\esp-idf\unit_test -B %build_directory%\esp-idf-%espidf_repo_root% -D TEST_COMPONENTS="cellular_tests" size flash
    @echo off
    rem Back to where this batch file was called from to run the tests with the Python script there
    popd
    if "%errorlevel%"=="0" (
        python %~dp0esp-idf\run_unit_tests_and_detect_outcome.py %com_port% %build_directory%\test_results.log %build_directory%\test_results.xml
        set return_value=0
    ) else (
        echo %~n0: ERROR build or download failed.
    )
    goto build_end

rem Build Amazon-FreeRTOS SDK on ESP32 with a SARA-R4 module on a WHRE board
:build_platform_4
    if not "%fetch%"=="" (
        if exist amazon-freertos (
            pushd amazon-freertos
            echo %~n0: updating Amazon FreeRTOS SDK code...
            call git pull
            popd
        ) else (
            echo %~n0: cloning Amazon FreeRTOS SDK from https://github.com/aws/amazon-freertos...
            call git clone https://github.com/aws/amazon-freertos.git
        )
    )
    echo %~n0: setting up paths assuming the V3.3 Espressif tools are in code_directory "C:\Program Files\Espressif\ESP-IDF Tools for v3.3",
    echo       CMake version 3.13 or later is in "C:\Program Files\CMake" and Python 2.7 is in "C:\Python27"...
    echo %~n0: note: you must have already installed all of these tools (WITHOUT adding them to the path, to do this you may need to
    echo       temporarily add them to the path with the line below) and run both "easy_install awscli" and "easy_install boto3".
    set path=C:\Program Files\Espressif\ESP-IDF Tools for v3.3\mconf-idf;C:\Program Files\Espressif\ESP-IDF Tools for v3.3\tools\bin;C:\Program Files\CMake\bin;C:\Python27;C:\Python27\Scripts;%path%
    echo %~n0: print version information start
    xtensa-esp32-elf-gcc --version
    cmake --version
    python --version
    echo %~n0: print version information end
    echo %~n0: setting environment variables...
    @echo on
    set THING_NAME=rob_test
    set IDF_PATH=
    @echo off
    echo %~n0: copying configuration files...
    @echo on
    copy "%config_directory%\amazon-freertos\configure.json" amazon-freertos\tools\aws_config_quick_start
    @echo off
    popd
    goto build_end

rem Build Espressif SDK v4 on ESP32 with a SARA-R4 module on a WHRE board talking to AWS
:build_platform_5
    echo %~n0: ERROR: not yet implemented.
    popd
    goto build_end

rem Usage string
:usage
    echo.
    echo Usage: %~n0 [/f /d /c /b] platform code_directory build_directory comport
    echo.
    echo where:
    echo.
    echo - /f indicates that a code fetch should be performed^; if this is NOT specified then all the necessary code is
    echo   assumed to have already been fetched to code_directory,
    echo - /d indicates that code and build directories should be created if they do not exist,
    echo - /c indicates that the code directory should be cleaned first,
    echo - /b indicates that the build directory should be cleaned first,
    echo - platform is a number representing the platform you'd like to build/test for selected from the following:
    for /L %%a in (1,1,!num_platforms!) do echo   %%a: !platform_%%a!
    echo - code_directory is the directory into which to fetch code.
    echo - build_directory is the directory in which to do building/testing.
    echo - comport is the port where the device under test is connected (e.g. COM1).
    echo.
    echo Note that the installation of various tools may require administrator privileges and so it is usually
    echo advisable to run this batch file as administrator.
    goto end

rem Done
:end
    echo.
    echo %~n0: end.
    exit /b %return_value%
