param(
  [string]$ExpectedEnv = "portenta_h7_m7_mid_mcp2515_j4_dual_csm"
)

$ErrorActionPreference = "Stop"
$root = Resolve-Path (Join-Path $PSScriptRoot "..")
Set-Location $root

$required = @("platformio.ini", "src\main.cpp", "board\AGENTS.md", "shared\docs\TRANSPORT_AND_RECORDS_KO.md")
foreach ($path in $required) {
  if (!(Test-Path -LiteralPath $path)) {
    throw "Missing required CSM file: $path"
  }
}

$nestedProjects = @("qt", "vsm", "VSM", "android", "Android", "app", "csm")
foreach ($path in $nestedProjects) {
  if (Test-Path -LiteralPath $path) {
    throw "Nested non-CSM workspace exists under CSM root: $path"
  }
}

$platformio = Get-Content -LiteralPath "platformio.ini" -Raw
if ($platformio -notmatch "default_envs\s*=\s*$([regex]::Escape($ExpectedEnv))") {
  throw "platformio.ini default_envs is not $ExpectedEnv"
}

$branch = git branch --show-current
$status = git status --short

Write-Host "CSM workspace OK"
Write-Host "root=$root"
Write-Host "branch=$branch"
if ($status) {
  Write-Host "git_dirty=1"
} else {
  Write-Host "git_dirty=0"
}
