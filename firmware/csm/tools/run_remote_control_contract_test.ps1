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

$outputDir = Join-Path $project ".pio\contract-test"
New-Item -ItemType Directory -Force -Path $outputDir | Out-Null
$output = Join-Path $outputDir "remote_control_contract_test.exe"
$include = Join-Path $project "include"
$sources = @(
  (Join-Path $project "test\remote_control_contract_test.cpp"),
  (Join-Path $project "src\board\authority\AuthorityManager.cpp"),
  (Join-Path $project "src\board\SafetySupervisor.cpp"),
  (Join-Path $project "src\board\can\BuiltinCanTxOwner.cpp"),
  (Join-Path $project "src\board\control\CanTxGateway.cpp"),
  (Join-Path $project "src\board\control\CommandLimiter.cpp"),
  (Join-Path $project "src\board\control\ControlReleaseSchedule.cpp"),
  (Join-Path $project "src\board\control\RemoteControlOrchestrator.cpp"),
  (Join-Path $project "src\board\control\RemoteControlRuntime.cpp"),
  (Join-Path $project "src\board\control\ServiceHilIntentRuntime.cpp"),
  (Join-Path $project "src\board\control\VehicleCommandMapper.cpp"),
  (Join-Path $project "src\board\remote\CrsfParser.cpp"),
  (Join-Path $project "src\board\remote\M4RemoteMailboxContract.cpp"),
  (Join-Path $project "src\board\remote\M4RemoteMailboxReader.cpp"),
  (Join-Path $project "src\board\remote\M4RemoteMailboxWriter.cpp"),
  (Join-Path $project "src\board\remote\RcNormalizer.cpp"),
  (Join-Path $project "src\board\remote\RemoteControlSource.cpp"),
  (Join-Path $project "src\board\remote\RemoteSharedMemory.cpp")
)
$quotedSources = ($sources | ForEach-Object { '"' + $_ + '"' }) -join ' '
$compile = "call `"$vsDevCmd`" -no_logo -arch=x64 && cl /nologo /std:c++17 /EHsc /D CSM_REMOTE_SHARED_MEMORY_TEST=1 /I`"$include`" $quotedSources /Fe:`"$output`""

cmd.exe /d /s /c $compile
if ($LASTEXITCODE -ne 0) {
  throw "remote control contract test compilation failed"
}

& $output
if ($LASTEXITCODE -ne 0) {
  throw "remote control contract test failed"
}
