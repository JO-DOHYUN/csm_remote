$ErrorActionPreference = "Stop"

$project = Resolve-Path (Join-Path $PSScriptRoot "..")
$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
if (!(Test-Path -LiteralPath $vswhere)) {
  throw "Visual Studio locator was not found: $vswhere"
}
$installation = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if ([string]::IsNullOrWhiteSpace($installation)) {
  throw "Visual Studio C++ x64 tools are not installed"
}
$vsDevCmd = Join-Path $installation "Common7\Tools\VsDevCmd.bat"

$outputDir = Join-Path $project ".pio\wifi-isolation-test"
New-Item -ItemType Directory -Force -Path $outputDir | Out-Null
$output = Join-Path $outputDir "wifi_isolation_contract_test.exe"
$include = Join-Path $project "include"
$sources = @(
  (Join-Path $project "test\wifi_isolation_contract_test.cpp"),
  (Join-Path $project "src\protocol\TypedFrame.cpp"),
  (Join-Path $project "src\board\uplink\ProductDownlinkRouter.cpp"),
  (Join-Path $project "src\board\uplink\WifiWorkerMailbox.cpp")
)
$quotedSources = ($sources | ForEach-Object { '"' + $_ + '"' }) -join ' '
$compile = "call `"$vsDevCmd`" -no_logo -arch=x64 && cl /nologo /std:c++17 /EHsc /DCSM_TYPED_FRAME_NATIVE=1 /DBOARD_WIFI_SINK_QUEUE_RECORDS=128 /DBOARD_WIFI_SINK_QUEUE_BYTES=8192 /DBOARD_WIFI_SINK_CRITICAL_RESERVE_RECORDS=4 /DBOARD_WIFI_SINK_CRITICAL_RESERVE_BYTES=2112 /DBOARD_WIFI_ISOLATE_HIGH_WATER_PERCENT=59 /I`"$include`" $quotedSources /Fe:`"$output`""

cmd.exe /d /s /c $compile
if ($LASTEXITCODE -ne 0) {
  throw "Wi-Fi isolation contract test compilation failed"
}

& $output
if ($LASTEXITCODE -ne 0) {
  throw "Wi-Fi isolation contract test failed"
}
