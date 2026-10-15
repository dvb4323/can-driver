param([string]$Compiler = 'gcc', [string]$OutputDirectory = "$PSScriptRoot/build")
$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path "$PSScriptRoot/../..").Path
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$testExe = Join-Path $OutputDirectory 'can_driver_tests.exe'
& $Compiler -std=c99 -Wall -Wextra -Werror -pedantic "-I$PSScriptRoot/fakes" "-I$projectRoot/drivers/can/inc" "$projectRoot/drivers/can/src/can_driver.c" "$PSScriptRoot/test_can_driver.c" -o $testExe
if ($LASTEXITCODE -ne 0) { throw 'Host test compile failed' }
& $testExe
if ($LASTEXITCODE -ne 0) { throw 'Host adapter tests failed' }
