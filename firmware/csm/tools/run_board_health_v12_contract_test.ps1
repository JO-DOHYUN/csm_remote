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

$outputDir = Join-Path $project ".pio\board-health-v12-test"
New-Item -ItemType Directory -Force -Path $outputDir | Out-Null
$output = Join-Path $outputDir "board_health_v12_contract_test.exe"
$include = Join-Path $project "include"
$sources = @(
  (Join-Path $project "test\board_health_v12_contract_test.cpp"),
  (Join-Path $project "src\protocol\TypedFrame.cpp")
)
$quotedSources = ($sources | ForEach-Object { '"' + $_ + '"' }) -join ' '
$compile = "call `"$vsDevCmd`" -no_logo -arch=x64 && cl /nologo /std:c++17 /W4 /EHsc /DCSM_TYPED_FRAME_NATIVE=1 /I`"$include`" $quotedSources /Fe:`"$output`""

cmd.exe /d /s /c $compile
if ($LASTEXITCODE -ne 0) {
  throw "BOARD_HEALTH v13 contract test compilation failed"
}

& $output
if ($LASTEXITCODE -ne 0) {
  throw "BOARD_HEALTH v13 contract test failed"
}
