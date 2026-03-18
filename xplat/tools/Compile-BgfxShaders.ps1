param(
    [Parameter(Mandatory = $true)]
    [string]$ShadercPath,

    [Parameter(Mandatory = $true)]
    [string]$BgfxShaderIncludeDir
)

$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$root = Split-Path -Parent $scriptDir
$src = Join-Path $root "assets\shaders\src"
$outRoot = Join-Path $root "assets\shaders"

$targets = @(
    @{ Folder = "dx11"; Profile = "s_5_0"; Platform = "windows" },
    @{ Folder = "dx12"; Profile = "s_5_0"; Platform = "windows" },
    @{ Folder = "metal"; Profile = "metal"; Platform = "osx" },
    @{ Folder = "glsl"; Profile = "120"; Platform = "linux" },
    @{ Folder = "essl"; Profile = "300_es"; Platform = "android" },
    @{ Folder = "spirv"; Profile = "spirv"; Platform = "linux" }
)

$shaderPairs = @(
    @{ In = "vs_color.sc"; Out = "vs_color.bin"; Type = "vertex" },
    @{ In = "fs_color.sc"; Out = "fs_color.bin"; Type = "fragment" },
    @{ In = "vs_texture.sc"; Out = "vs_texture.bin"; Type = "vertex" },
    @{ In = "fs_texture.sc"; Out = "fs_texture.bin"; Type = "fragment" }
)

foreach ($t in $targets) {
    $targetOut = Join-Path $outRoot $t.Folder
    New-Item -ItemType Directory -Force -Path $targetOut | Out-Null

    foreach ($s in $shaderPairs) {
        $inPath = Join-Path $src $s.In
        $outPath = Join-Path $targetOut $s.Out

        & $ShadercPath `
            -f $inPath `
            -o $outPath `
            --type $s.Type `
            --platform $t.Platform `
            --profile $t.Profile `
            -i $BgfxShaderIncludeDir
    }
}

Write-Host "Shader compilation complete."

