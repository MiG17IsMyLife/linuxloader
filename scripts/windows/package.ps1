param(
    [ValidateSet('wmmt3', 'csneo')]
    [string]$Profile = 'wmmt3',
    [string]$BuildDirectory = 'build-pacloader',
    [string]$OutputDirectory = 'dist'
)

$ErrorActionPreference = 'Stop'
$repository = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$manifest = Join-Path $repository "packaging\dependencies\$Profile.txt"
$buildExe = Join-Path $repository "$BuildDirectory\linuxloader.exe"
$output = Join-Path $repository "$OutputDirectory\$Profile"

if (-not (Test-Path -LiteralPath $buildExe)) {
    throw "Build output not found: $buildExe"
}

New-Item -ItemType Directory -Path $output -Force | Out-Null
Copy-Item -LiteralPath $buildExe -Destination (Join-Path $output 'linuxloader.exe') -Force

foreach ($entry in Get-Content -LiteralPath $manifest) {
    $entry = $entry.Trim()
    if (-not $entry -or $entry.StartsWith('#')) { continue }
    $source = Join-Path $repository "libs\win32\$entry"
    if (-not (Test-Path -LiteralPath $source)) { throw "Missing dependency: $source" }
    $destination = Join-Path $output $entry
    New-Item -ItemType Directory -Path (Split-Path $destination) -Force | Out-Null
    Copy-Item -LiteralPath $source -Destination $destination -Force
}

# Native pacloader does not import this DLL, but old Linux guest C++ shared
# objects request libgcc_s.so.1 and the ELF resolver maps that request here.
$gcc = (Get-Command gcc.exe -ErrorAction Stop).Source
$guestGcc = Join-Path (Split-Path $gcc) 'libgcc_s_dw2-1.dll'
$guestGccDestination = Join-Path $output 'll-deps\libgcc_s_dw2-1.dll'
New-Item -ItemType Directory -Path (Split-Path $guestGccDestination) -Force | Out-Null
Copy-Item -LiteralPath $guestGcc -Destination $guestGccDestination -Force

Write-Host "Created $Profile package at $output"
