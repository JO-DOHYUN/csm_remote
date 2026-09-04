$ErrorActionPreference = "Stop"

$project = Resolve-Path (Join-Path $PSScriptRoot "..")
$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
$installation = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if ([string]::IsNullOrWhiteSpace($installation)) { throw "Visual Studio C++ x64 tools are not installed" }
$vsDevCmd = Join-Path $installation "Common7\Tools\VsDevCmd.bat"
$outputDir = Join-Path $project ".pio\wifi-control-plane-test"
New-Item -ItemType Directory -Force -Path $outputDir | Out-Null
$output = Join-Path $outputDir "wifi_control_plane_contract_test.exe"
$include = Join-Path $project "include"
$stubs = Join-Path $project "test\native_stubs"
$sources = @(
  (Join-Path $project "test\wifi_control_plane_contract_test.cpp"),
  (Join-Path $project "src\protocol\TypedFrame.cpp"),
  (Join-Path $project "src\board\uplink\WifiControlPlaneMailbox.cpp"),
  (Join-Path $project "src\board\uplink\WifiRealtimeMailbox.cpp")
)
$quotedSources = ($sources | ForEach-Object { '"' + $_ + '"' }) -join ' '
$compile = "call `"$vsDevCmd`" -no_logo -arch=x64 && cl /nologo /std:c++17 /EHsc /DCSM_TYPED_FRAME_NATIVE=1 /I`"$stubs`" /I`"$include`" $quotedSources /Fe:`"$output`""
cmd.exe /d /s /c $compile
if ($LASTEXITCODE -ne 0) { throw "Wi-Fi control-plane contract test compilation failed" }
& $output
if ($LASTEXITCODE -ne 0) { throw "Wi-Fi control-plane contract test failed" }
