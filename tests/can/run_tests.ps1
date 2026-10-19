param([string]$Compiler = 'gcc', [string]$OutputDirectory = "$PSScriptRoot/build")
$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path "$PSScriptRoot/../..").Path
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$testExe = Join-Path $OutputDirectory 'can_driver_tests.exe'
& $Compiler -std=c99 -Wall -Wextra -Werror -pedantic "-I$PSScriptRoot/fakes" "-I$projectRoot/drivers/can/inc" "$projectRoot/drivers/can/src/can_driver.c" "$PSScriptRoot/test_can_driver.c" -o $testExe
if ($LASTEXITCODE -ne 0) { throw 'Host test compile failed' }
& $testExe
if ($LASTEXITCODE -ne 0) { throw 'Host adapter tests failed' }
$boardExe = Join-Path $OutputDirectory 'board_can_tests.exe'
& $Compiler -std=c99 -Wall -Wextra -Werror -pedantic "-I$PSScriptRoot/fakes" "-I$projectRoot/board" "$projectRoot/board/board_can.c" "$PSScriptRoot/test_board_can.c" -o $boardExe
if ($LASTEXITCODE -ne 0) { throw 'Host board test compile failed' }
& $boardExe
if ($LASTEXITCODE -ne 0) { throw 'Host board tests failed' }
# These are host simulations. The verified=1 case touches no real hardware.
$appCases = @(
    @{ Name='loopback'; Profile=0; Verified=0 },
    @{ Name='normal_blocked'; Profile=1; Verified=0 },
    @{ Name='normal_simulated'; Profile=1; Verified=1 }
)
foreach ($case in $appCases) {
    $appExe = Join-Path $OutputDirectory ("can_app_" + $case.Name + '.exe')
    & $Compiler -std=c99 -Wall -Wextra -Werror -pedantic "-I$PSScriptRoot/fakes" "-I$projectRoot/drivers/can/inc" "-I$projectRoot/board" "-DCAN_TEST_PROFILE=$($case.Profile)" "-DCAN_TEST_HARDWARE_VERIFIED=$($case.Verified)" "$PSScriptRoot/test_can_application.c" -o $appExe
    if ($LASTEXITCODE -ne 0) { throw "Host application compile failed: $($case.Name)" }
    & $appExe
    if ($LASTEXITCODE -ne 0) { throw "Host application tests failed: $($case.Name)" }
}
$espExe = Join-Path $OutputDirectory 'can_esp32_tests.exe'
& $Compiler -std=c99 -Wall -Wextra -Werror -pedantic "-I$PSScriptRoot/fakes" "-I$projectRoot/drivers/can/inc" "-I$projectRoot/board" "$PSScriptRoot/test_can_esp32.c" -o $espExe
if ($LASTEXITCODE -ne 0) { throw 'Host ESP32 application compile failed' }
& $espExe
if ($LASTEXITCODE -ne 0) { throw 'Host ESP32 application tests failed' }
