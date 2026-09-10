param(
  [string]$FrameworkRoot = "$env:USERPROFILE\.platformio\packages\framework-arduino-mbed",
  [string]$ToolchainRoot = "$env:USERPROFILE\.platformio\packages\toolchain-gccarmnoneeabi",
  [ValidatePattern('^[D-Z]$')]
  [string]$ShortDrive = 'X',
  [switch]$PackageOnly
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$profileRoot = Join-Path $projectRoot 'third_party\mbed_portenta_product'
$buildRoot = Join-Path $projectRoot 'build'
$vendorRoot = Join-Path $buildRoot 'vendor-src'
$arduinoRoot = Join-Path $vendorRoot 'ArduinoCore-mbed-4.3.1'
$mbedRoot = Join-Path $vendorRoot 'mbed-os-17dc3dc2'
$builderRoot = Join-Path $profileRoot 'builder'
$cmakeBuild = Join-Path $buildRoot 'mbed-product-build'
$venvRoot = Join-Path $buildRoot 'mbed-venv'
$artifactRoot = Join-Path $profileRoot 'artifact\PORTENTA_H7_M7'
$stockLibrary = Join-Path $FrameworkRoot 'variants\PORTENTA_H7_M7\libs\libmbed.a'
$stockConfig = Join-Path $FrameworkRoot 'variants\PORTENTA_H7_M7\mbed_config.h'
$outputLibrary = Join-Path $artifactRoot 'libmbed.a'
$outputConfig = Join-Path $artifactRoot 'mbed_config.h'
$overrideTemplate = Join-Path $builderRoot 'csm_product_mbed_overrides.h'
$outputOverrides = Join-Path $artifactRoot 'csm_product_mbed_overrides.h'
$artifactManifest = Join-Path $artifactRoot 'artifact-manifest.json'
$arduinoCommit = '6816d442fd00bc17f83c73396d3d8d90285a6a8a'
$mbedCommit = '17dc3dc2e6e2817a8bd3df62f38583319f0e4fed'
$stockLibrarySha = '411F46E0A6498B647BA9DC7CCEBCBBD0D45305D9C96FAD5A2DF64F9C995885CF'
$stockConfigSha = '1F0EF173CACB2F278A1550E75665E50BDCCF2F9FCD57F7B7021289653C6F083B'
$strip = Join-Path $ToolchainRoot 'bin\arm-none-eabi-strip.exe'

function Invoke-Checked {
  param([string]$Executable, [string[]]$Arguments)
  & $Executable @Arguments
  if ($LASTEXITCODE -ne 0) {
    throw "$Executable failed with exit code $LASTEXITCODE"
  }
}

function Get-Sha256([string]$Path) {
  if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
    throw "Missing file: $Path"
  }
  return (Get-FileHash -Algorithm SHA256 -LiteralPath $Path).Hash.ToUpperInvariant()
}

function Test-BaseProductSourcePatches {
  $heap = Join-Path $mbedRoot 'connectivity\lwipstack\lwip-sys\arch\lwip_sys_arch.c'
  $buffer = Join-Path $mbedRoot 'connectivity\drivers\wifi\COMPONENT_WHD\whd-bsp-integration\cy_network_buffer.c'
  $sdio = Join-Path $mbedRoot 'targets\TARGET_STM\TARGET_STM32H7\TARGET_STM32H747xI\TARGET_PORTENTA_H7\COMPONENT_WHD\port\cyhal_sdio.c'
  $portenta = Join-Path $mbedRoot 'targets\TARGET_STM\TARGET_STM32H7\TARGET_STM32H747xI\TARGET_PORTENTA_H7\COMPONENT_WHD\CMakeLists.txt'
  return (Select-String -Quiet -LiteralPath $heap -Pattern '.csm_lwip_heap_d3') -and
    (Select-String -Quiet -LiteralPath $buffer -Pattern 'pbuf_alloc\(PBUF_RAW, size, PBUF_RAM\)') -and
    (Select-String -Quiet -LiteralPath $sdio -Pattern 'Product contract: propagate SDIO failure') -and
    (Select-String -Quiet -LiteralPath $portenta -Pattern 'port/cy_hal.c')
}

function Test-ServiceHilArenaPatch {
  $arena = Join-Path $mbedRoot 'connectivity\lwipstack\source\lwip_tools.cpp'
  $accept = Join-Path $mbedRoot 'connectivity\lwipstack\source\LWIPStack.cpp'
  return (Select-String -Quiet -LiteralPath $arena -Pattern 'csm_lwip_socket_arena_snapshot') -and
    (Select-String -Quiet -LiteralPath $accept -Pattern 'netconn_accept\(s->conn, &accepted\)') -and
    (Select-String -Quiet -LiteralPath $accept -Pattern 'netconn_delete\(accepted\)')
}

New-Item -ItemType Directory -Force -Path $buildRoot,$vendorRoot,$artifactRoot | Out-Null

if (-not $PackageOnly) {
  if (-not (Test-Path -LiteralPath (Join-Path $arduinoRoot '.git'))) {
    Invoke-Checked git @('clone', '--no-checkout', 'https://github.com/arduino/ArduinoCore-mbed.git', $arduinoRoot)
    Invoke-Checked git @('-C', $arduinoRoot, 'checkout', '--detach', $arduinoCommit)
  }
  if ((git -C $arduinoRoot rev-parse HEAD).Trim() -ne $arduinoCommit) {
    throw 'ArduinoCore-mbed source is not at the pinned commit'
  }

  if (-not (Test-Path -LiteralPath (Join-Path $mbedRoot '.git'))) {
    Invoke-Checked git @('clone', '--no-checkout', 'https://github.com/ARMmbed/mbed-os.git', $mbedRoot)
    Invoke-Checked git @('-C', $mbedRoot, 'checkout', '--detach', $mbedCommit)
  }
  if ((git -C $mbedRoot rev-parse HEAD).Trim() -ne $mbedCommit) {
    throw 'Mbed OS source is not at the pinned commit'
  }

  if (-not (Test-BaseProductSourcePatches)) {
    if ((git -C $mbedRoot status --porcelain).Count -ne 0) {
      throw 'Generated Mbed source is modified but does not match the product patch markers'
    }
    $excluded = @(
      '0017-', '0026-', '0050-',
      '0160-', '0161-', '0162-', '0163-', '0164-',
      '0165-', '0166-', '0167-', '0168-', '0169-',
      '0197-', '0231-'
    )
    Get-ChildItem (Join-Path $arduinoRoot 'patches') -Filter '*.patch' |
      Sort-Object Name |
      Where-Object {
        $name = $_.Name
        -not ($excluded | Where-Object { $name.StartsWith($_) })
      } |
      ForEach-Object {
        Invoke-Checked git @('-C', $mbedRoot, 'apply', '--whitespace=nowarn', $_.FullName)
      }
    Get-ChildItem (Join-Path $profileRoot 'patches') -Filter '*.patch' |
      Sort-Object Name |
      ForEach-Object {
        Invoke-Checked git @('-C', $mbedRoot, 'apply', '--whitespace=nowarn', $_.FullName)
      }
  }
  if (-not (Test-BaseProductSourcePatches)) {
    throw 'Pinned product source patches did not materialize'
  }
  if (-not (Test-ServiceHilArenaPatch)) {
    $arenaPatch = Join-Path $profileRoot 'patches\0005-service-hil-lwip-arena-observability.patch'
    Invoke-Checked git @('-C', $mbedRoot, 'apply', '--check', $arenaPatch)
    Invoke-Checked git @('-C', $mbedRoot, 'apply', '--whitespace=nowarn', $arenaPatch)
  }
  if (-not (Test-ServiceHilArenaPatch)) {
    throw 'Service/HIL lwIP arena observability patch did not materialize'
  }

  if (-not (Test-Path -LiteralPath (Join-Path $venvRoot 'Scripts\python.exe'))) {
    Invoke-Checked py @('-3.11', '-m', 'venv', $venvRoot)
  }
  $python = Join-Path $venvRoot 'Scripts\python.exe'
  Invoke-Checked $python @(
    '-m', 'pip', 'install', '--disable-pip-version-check',
    'mbed-tools==7.58.0', 'cmake==3.27.9', 'ninja==1.11.1.1',
    'prettytable', 'future', 'intelhex', 'jinja2'
  )
  $scripts = Join-Path $venvRoot 'Scripts'
  $mbedTools = Join-Path $scripts 'mbed-tools.exe'
  $cmake = Join-Path $scripts 'cmake.exe'
  $env:Path = "$(Join-Path $ToolchainRoot 'bin');$scripts;$env:Path"
  $shortDriveRoot = "${ShortDrive}:"
  $shortProjectHost = Join-Path $buildRoot 'mbed-product-app'
  $shortProject = "$shortDriveRoot\mbed-product-app"
  $shortBuild = "$shortProject\short-build"
  if (Test-Path -LiteralPath "$shortDriveRoot\") {
    throw "Short build drive is already in use: $shortDriveRoot"
  }
  New-Item -ItemType Directory -Force -Path $shortProjectHost | Out-Null
  Copy-Item -Force -Path (Join-Path $builderRoot '*') -Destination $shortProjectHost
  Invoke-Checked subst @($shortDriveRoot, $buildRoot)
  try {
    Invoke-Checked $mbedTools @(
      'configure', '-m', 'PORTENTA_H7_M7', '-t', 'GCC_ARM', '-b', 'release',
      '-p', $shortProject,
      '--mbed-os-path', "$shortDriveRoot\vendor-src\mbed-os-17dc3dc2",
      '--app-config', (Join-Path $profileRoot 'mbed_app.product.json'),
      '-o', $shortBuild
    )
    Invoke-Checked $cmake @(
      '-S', $shortProject, '-B', $shortBuild, '-G', 'Ninja',
      '-DCMAKE_BUILD_TYPE=Release',
      "-DMBED_PATH=$shortDriveRoot/vendor-src/mbed-os-17dc3dc2",
      "-DPython3_EXECUTABLE=$shortDriveRoot/mbed-venv/Scripts/python.exe"
    )
    Invoke-Checked $cmake @('--build', $shortBuild, '--parallel', '8')
  } finally {
    & subst $shortDriveRoot /D
  }
}

if ((Get-Sha256 $stockLibrary) -ne $stockLibrarySha -or
    (Get-Sha256 $stockConfig) -ne $stockConfigSha) {
  throw 'Installed Arduino Mbed 4.3.1 framework does not match the pinned base artifact'
}

$compiledRoot = Join-Path $cmakeBuild 'CMakeFiles\csm_mbed_product.dir'
$shortCompiledRoot = Join-Path $buildRoot 'mbed-product-app\short-build\CMakeFiles\csm_mbed_product.dir'
if (Test-Path -LiteralPath $shortCompiledRoot) {
  # The Mbed compiler expands response files before spawning its child process.
  # Keep the reproducible short X: build as the packaging source so Windows'
  # command-line limit cannot turn a valid product build into CreateProcess.
  $compiledRoot = $shortCompiledRoot
}
$generatedConfig = Join-Path $cmakeBuild 'mbed_config.h'
$shortGeneratedConfig = Join-Path $buildRoot 'mbed-product-app\short-build\mbed_config.h'
if (Test-Path -LiteralPath $shortGeneratedConfig) {
  $generatedConfig = $shortGeneratedConfig
}
if (-not (Test-Path -LiteralPath $compiledRoot) -or
    -not (Test-Path -LiteralPath $generatedConfig)) {
  throw 'Pinned Mbed compile output is unavailable'
}

$objects = Get-ChildItem -Recurse -File $compiledRoot | Where-Object {
  $_.Name -like '*.obj' -and (
    $_.FullName -match '[\\/]connectivity[\\/]lwipstack[\\/]' -or
    $_.Name -eq 'cy_network_buffer.c.obj' -or
    $_.Name -eq 'cyhal_sdio.c.obj'
  )
}
if ($objects.Count -ne 67) {
  throw "Expected 67 network replacement objects, found $($objects.Count)"
}

$stage = Join-Path $buildRoot ("mbed-overlay-stage-" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $stage | Out-Null
try {
  Copy-Item -LiteralPath $stockLibrary -Destination $outputLibrary
  $staged = @()
  foreach ($object in $objects) {
    $member = $object.Name -replace '\.(c|cpp|cc|cxx|S|s)\.obj$','.o'
    $target = Join-Path $stage $member
    Copy-Item -LiteralPath $object.FullName -Destination $target
    Invoke-Checked $strip @('--strip-debug', $target)
    $staged += $target
  }
  $ar = Join-Path $ToolchainRoot 'bin\arm-none-eabi-ar.exe'
  Invoke-Checked $ar (@('rcsD', $outputLibrary) + $staged)
  # PlatformIO whole-archives both FrameworkArduino and libmbed. Arduino
  # compiles this same source into its core archive, so the repacked product
  # archive must leave the application-owned copy as the sole definition.
  Invoke-Checked $ar @('dD', $outputLibrary, 'mstd_mutex.o')
  Copy-Item -LiteralPath $generatedConfig -Destination $outputConfig
  Copy-Item -LiteralPath $overrideTemplate -Destination $outputOverrides

  $memberNames = & $ar 't' $outputLibrary
  if ($LASTEXITCODE -ne 0) {
    throw 'Unable to enumerate the product archive'
  }
  $memberListText = (($memberNames | ForEach-Object { "$_`n" }) -join '')
  $memberListBytes = [Text.Encoding]::UTF8.GetBytes($memberListText)
  $memberListStream = New-Object IO.MemoryStream(,$memberListBytes)
  try {
    $memberListSha = (Get-FileHash -Algorithm SHA256 -InputStream $memberListStream).Hash.ToUpperInvariant()
  } finally {
    $memberListStream.Dispose()
  }

  $output = [ordered]@{
    schema = 1
    generated_utc = [DateTime]::UtcNow.ToString('o')
    base = [ordered]@{
      libmbed_sha256 = $stockLibrarySha
      mbed_config_sha256 = $stockConfigSha
    }
    source = [ordered]@{
      arduino_core_commit = $arduinoCommit
      mbed_os_commit = $mbedCommit
    }
    replacement = [ordered]@{
      object_count = $objects.Count
      debug_sections_stripped = $true
      removed_application_owned_members = @('mstd_mutex.o')
      member_list_sha256 = $memberListSha
      lwip_heap_object_bytes = 40979
    }
    output = [ordered]@{
      libmbed_sha256 = Get-Sha256 $outputLibrary
      mbed_config_sha256 = Get-Sha256 $outputConfig
      app_overrides_sha256 = Get-Sha256 $outputOverrides
    }
  }
  $manifestJson = $output | ConvertTo-Json -Depth 5
  [IO.File]::WriteAllText(
    $artifactManifest,
    $manifestJson,
    (New-Object Text.UTF8Encoding($false))
  )
} finally {
  $resolvedStage = (Resolve-Path -LiteralPath $stage).Path
  if (-not $resolvedStage.StartsWith($buildRoot, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing to remove unexpected staging path: $resolvedStage"
  }
  Remove-Item -Recurse -Force -LiteralPath $resolvedStage
}

Write-Output "Pinned product Mbed ready: $(Get-Sha256 $outputLibrary)"
