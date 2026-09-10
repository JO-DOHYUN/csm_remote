param([switch]$Off)
$ErrorActionPreference = 'Stop'
$project = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$installation = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $installation) { throw 'MSVC not found' }
$dev = Join-Path $installation 'Common7/Tools/VsDevCmd.bat'
$mode = if ($Off) { 'off' } else { 'on' }
$dir = Join-Path $project ".pio/observation-owner-$mode"
New-Item -ItemType Directory -Force -Path $dir | Out-Null
python (Join-Path $PSScriptRoot 'extract_observation_owner_fixture.py') (Join-Path $dir 'observation_main_body.inc')
if ($LASTEXITCODE -ne 0) { throw 'Owner fixture extraction failed' }
python (Join-Path $PSScriptRoot 'extract_observation_owner_fixture.py') (Join-Path $dir 'observation_tcp_body.inc') tcp
if ($LASTEXITCODE -ne 0) { throw 'TCP owner fixture extraction failed' }
$flag = if ($Off) { 0 } else { 1 }
$sources = @('test/observation_owner_test.cpp','src/board/uplink/WifiRealtimeMailbox.cpp',
 'src/board/uplink/WifiRealtimeWorker.cpp','src/board/uplink/WifiControlPlaneMailbox.cpp',
 'src/board/uplink/UsbCdcSink.cpp','src/board/uplink/WifiWorkerMailbox.cpp',
 'src/board/uplink/ProductDownlinkRouter.cpp',
 'src/board/uplink/CanonicalPublisher.cpp','src/board/uplink/RecordAdmission.cpp',
 'src/board/uplink/UplinkPriorityPolicy.cpp',
 'src/board/uplink/WifiTcpSink.cpp','src/board/uplink/WifiTransportDiagnostic.cpp',
 'src/protocol/TypedFrame.cpp','src/protocol/RealtimeControl.cpp',
 'src/board/control/HostRealtimeAuthority.cpp','src/board/control_island/ControlSourceManager.cpp')
$argsText = ($sources | ForEach-Object { '"'+(Join-Path $project $_)+'"' }) -join ' '
# Existing worker alignas(8) intentionally adds host ABI padding (MSVC C4324).
$command = 'call "{0}" -no_logo -arch=x64 && cl /nologo /std:c++17 /EHsc /W4 /WX /wd4324 /D_CRT_SECURE_NO_WARNINGS /DSERIAL_CDC=1 /DBOARD_ENABLE_WIFI_UPLINK=1 /DCSM_TYPED_FRAME_NATIVE=1 /DBOARD_ENABLE_SERVICE_HIL_OBSERVABILITY={1} /I"{2}" /I"{3}" {4} /Fe:owner.exe' -f $dev,$flag,(Join-Path $project 'test/observation_stubs'),(Join-Path $project 'include'),$argsText
$command += ' /I"'+$dir+'"'
$command += ' /DBOARD_UPLINK_POOL_MEDIUM_PAYLOAD_BYTES=192'
Push-Location $dir
try {
  cmd.exe /d /s /c $command
  if ($LASTEXITCODE -ne 0) { throw 'Owner compile failed' }
  & ./owner.exe
  if ($LASTEXITCODE -ne 0) { throw 'Owner assertion failed' }
} finally { Pop-Location }
