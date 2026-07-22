$ErrorActionPreference = "Stop"

$project = Resolve-Path (Join-Path $PSScriptRoot "..")
$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
$installation = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if ([string]::IsNullOrWhiteSpace($installation)) {
  throw "Visual Studio C++ x64 tools are not installed"
}
$vsDevCmd = Join-Path $installation "Common7\Tools\VsDevCmd.bat"
$outputDir = Join-Path $project ".pio\runtime-supervisor-test"
New-Item -ItemType Directory -Force -Path $outputDir | Out-Null
$include = Join-Path $project "include"
$sources = @(
  (Join-Path $project "test\runtime_supervisor_contract_test.cpp"),
  (Join-Path $project "src\board\diagnostics\BootRecovery.cpp"),
  (Join-Path $project "src\board\diagnostics\RuntimeSupervisor.cpp")
)
$quotedSources = ($sources | ForEach-Object { '"' + $_ + '"' }) -join ' '

foreach ($profile in 0..3) {
  $output = Join-Path $outputDir "runtime_supervisor_profile_$profile.exe"
  $compile = "call `"$vsDevCmd`" -no_logo -arch=x64 && cl /nologo /std:c++17 /W4 /EHsc /D BOARD_RESET_EXPERIMENT_PROFILE=$profile /I`"$include`" $quotedSources /Fe:`"$output`""
  cmd.exe /d /s /c $compile
  if ($LASTEXITCODE -ne 0) { throw "Runtime supervisor profile $profile compilation failed" }
  & $output
  if ($LASTEXITCODE -ne 0) { throw "Runtime supervisor profile $profile failed" }
}
