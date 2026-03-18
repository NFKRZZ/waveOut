param(
    [ValidateSet("Debug", "Release")]
    [string]$Config = "Release",
    [string]$AudioPath = ""
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$xplatRoot = Join-Path $repoRoot "xplat"
$buildDir = Join-Path $repoRoot "build/xplat"
$toolchain = "C:/vcpkg/vcpkg/scripts/buildsystems/vcpkg.cmake"

if (!(Test-Path $toolchain))
{
    throw "vcpkg toolchain not found at $toolchain"
}

Write-Host "Configuring xplat..."
cmake -S $xplatRoot -B $buildDir -DCMAKE_TOOLCHAIN_FILE="$toolchain"
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed." }

Write-Host "Building xplat ($Config)..."
cmake --build $buildDir --config $Config
if ($LASTEXITCODE -ne 0) { throw "Build failed." }

$exe = Join-Path $buildDir "$Config/waveout_xplat.exe"
if (!(Test-Path $exe))
{
    throw "Executable not found: $exe"
}

Write-Host "Done."
if ($AudioPath -ne "")
{
    Write-Host "Run:"
    Write-Host "`"$exe`" `"$AudioPath`""
}
else
{
    Write-Host "Run:"
    Write-Host "`"$exe`""
}
