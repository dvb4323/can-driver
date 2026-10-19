param([string]$Compiler='gcc', [string]$OutputDirectory="$PSScriptRoot/build")
# User-operated host runner; not part of the S32DS embedded source paths.
$ErrorActionPreference='Stop'
$phase4aRoot=(Resolve-Path "$PSScriptRoot/../..").Path
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$phase4aExe=Join-Path $OutputDirectory 'phase4a_tests.exe'
& $Compiler -std=c99 -Wall -Wextra -Werror -pedantic "-I$phase4aRoot/protocol" "-I$phase4aRoot/application" "-I$phase4aRoot/interfaces" "-I$phase4aRoot/backends" "-I$phase4aRoot/platform" "-I$phase4aRoot/drivers/can/inc" "$phase4aRoot/protocol/can_protocol.c" "$phase4aRoot/application/motor_app.c" "$phase4aRoot/backends/motor_mock.c" "$phase4aRoot/platform/app_time.c" "$PSScriptRoot/test_phase4a.c" -o $phase4aExe
if ($LASTEXITCODE -ne 0) { throw 'Phase 4A host test compile failed' }
& $phase4aExe
if ($LASTEXITCODE -ne 0) { throw 'Phase 4A host tests failed' }
