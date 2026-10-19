<#
.SYNOPSIS
    Builds the Windows MSI of the client or of the router.

.DESCRIPTION
    Compiles installer/windows/<product>.wxs into an MSI holding the program that was built by
    build-win.ps1. Only en-us: the other languages of the original need wisubstg.vbs, which is
    not in this Windows Kits.

    The router's installer takes the service along with the binary - it runs --install as a
    deferred action, so the service is created by the package rather than by the person running
    it. That is the reason it needs the rights the deferred action is given, and why the binary
    has to be in the package for the action to find.

.EXAMPLE
    .\build-installer.ps1
    .\build-installer.ps1 -Product router
    .\build-installer.ps1 -Product router -Version 3.1.1.1
#>
param(
    [ValidateSet('client', 'router')]
    [string]$Product = 'client',
    [string]$Version = '3.1.1.1',
    [string]$Architecture = 'x86_64'
)

$ErrorActionPreference = 'Stop'

$repo   = Split-Path (Split-Path (Split-Path $PSScriptRoot -Parent) -Parent) -Parent
$root   = Split-Path $repo -Parent
$wix    = Join-Path $root 'tools\wix3'
$srcDir = Join-Path $root "aspia\installer\windows"
$build  = Join-Path $root 'aspia\builds\local-win-x64\Release'
$stage  = Join-Path $root "aspia\builds\msi-$Product-$Architecture"

$executable = "aspia_$Product.exe"
$builtBinary = Join-Path $build $executable

if (-not (Test-Path $builtBinary))
{
    throw "The $Product is not built: $builtBinary is missing. Run build-win.ps1 -Target build first."
}

foreach ($tool in 'candle.exe', 'light.exe')
{
    if (-not (Test-Path (Join-Path $wix $tool)))
    {
        throw "WiX is not unpacked: $tool is missing under $wix"
    }
}

# The staging folder holds the one file the installer takes. A folder of its own keeps the
# .wixobj and the .cab out of the build tree of the program itself, and keeps the two products
# from overwriting each other's leftovers.
New-Item -ItemType Directory -Force -Path $stage | Out-Null
Copy-Item $builtBinary $stage -Force

$env:ASPIA_VERSION  = $Version
$env:ASPIA_BIN_DIR  = $stage

$candleArch = switch ($Architecture) {
    'x64'     { 'x64' }
    'x86_64'  { 'x64' }
    'x86'     { 'x86' }
    default   { throw "Architecture not known to candle: $Architecture" }
}

$extensions = @(
    '-ext', 'WixUtilExtension',
    '-ext', 'WixUIExtension',
    '-ext', 'WixFirewallExtension'
)

$msi = Join-Path $stage "aspia-$Product-$Version-$Architecture.msi"
$wxs = "$Product.wxs"
$wxl = "translations\$Product.en-us.wxl"

Push-Location $srcDir
try
{
    Write-Host '##################################################'
    Write-Host "Aspia $Product MSI"
    Write-Host "  Version    : $Version"
    Write-Host "  Arch       : $Architecture (candle: $candleArch)"
    Write-Host "  Exe        : $builtBinary"
    Write-Host "  Output     : $msi"

    & (Join-Path $wix 'candle.exe') -out "$stage\" -arch $candleArch @extensions $wxs
    if ($LASTEXITCODE -ne 0) { throw "candle failed with $LASTEXITCODE" }

    & (Join-Path $wix 'light.exe') -sval -out $msi -cultures:en-us @extensions -loc $wxl `
        "$stage\$Product.wixobj"
    if ($LASTEXITCODE -ne 0) { throw "light failed with $LASTEXITCODE" }
}
finally
{
    Pop-Location
}

Write-Host '##################################################'
Get-Item $msi | Select-Object Name, @{n='MB'; e={ [math]::Round($_.Length / 1MB, 2) } }, LastWriteTime