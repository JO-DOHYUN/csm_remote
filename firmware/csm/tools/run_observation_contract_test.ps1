$ErrorActionPreference = 'Stop'
$project = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$installation = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if ([string]::IsNullOrWhiteSpace($installation)) { throw 'Visual Studio C++ tools unavailable' }
$vsDevCmd = Join-Path $installation 'Common7/Tools/VsDevCmd.bat'
$outputDir = Join-Path $project '.pio/observation-contract'
New-Item -ItemType Directory -Force -Path $outputDir | Out-Null
$exe = Join-Path $outputDir 'observation_contract_test.exe'
$source = Join-Path $project 'test/observation_contract_test.cpp'
$typed = Join-Path $project 'src/protocol/TypedFrame.cpp'
$include = Join-Path $project 'include'
$compile = 'call "{0}" -no_logo -arch=x64 && cl /nologo /W4 /WX /std:c++17 /EHsc /DCSM_TYPED_FRAME_NATIVE=1 /I"{1}" "{2}" "{3}" /Fe:"{4}"' -f $vsDevCmd,$include,$source,$typed,$exe
Push-Location $outputDir
try {
  cmd.exe /d /s /c $compile
  if ($LASTEXITCODE -ne 0) { throw 'Observation C++ compile failed' }
  & $exe (Join-Path $project 'shared/observability/golden.tsv')
  if ($LASTEXITCODE -ne 0) { throw 'Observation C++ contract failed' }
} finally { Pop-Location }
