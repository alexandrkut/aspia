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
    [ValidateSet('client', 'host', 'router', 'relay')]
    [string]$Product = 'client',
    [string]$Version = '',
    [string]$Architecture = 'x86_64'
)

$ErrorActionPreference = 'Stop'

$root   = 'C:\Projects\aspia-mod'
$wix    = Join-Path $root 'tools\wix3'
$srcDir = Join-Path $root "aspia\installer\windows"
$build  = Join-Path $root 'aspia\builds\local-win-x64\Release'
$stage  = Join-Path $root "aspia\builds\msi-$Product-$Architecture"

# The version of the package is the version of the program: the three numbers of the sources
# and the number of the build of this line of the versions, which is the fourth number of the
# version the program shows in the window "About". The installer replaces a package of the
# same version, so the number is raised by hand with every release.
if (-not $Version)
{
    $cmake = Get-Content (Join-Path $root 'aspia\CMakeLists.txt') -Raw

    $parts = @('MAJOR', 'MINOR', 'PATCH', 'BUILD') | ForEach-Object {
        if ($cmake -match "set\(ASPIA_VERSION_$_ (\d+)\)") { $Matches[1] } else { throw "the version is not in the sources" }
    }

    $Version = $parts -join '.'

    Write-Host "the version of the package is $Version"
}

$executable = "aspia_$Product.exe"
$builtBinary = Join-Path $build $executable

if (-not (Test-Path $builtBinary))
{
    throw "The $Product is not built: $builtBinary is missing. Run build-win.ps1 -Target build first."
}

# The host takes the credentials of the user with it in a library of its own, and the
# installer puts it next to the program.
$extraFiles = @()

if ($Product -eq 'host')
{
    $credentials = Join-Path $build 'aspia_credentials.dll'

    if (-not (Test-Path $credentials))
    {
        throw "The credentials library is not built: $credentials is missing."
    }

    $extraFiles += $credentials
}

foreach ($tool in 'candle.exe', 'light.exe')
{
    if (-not (Test-Path (Join-Path $wix $tool)))
    {
        throw "WiX is not unpacked: $tool is missing under $wix"
    }
}

# The staging folder holds the files the installer takes. A folder of its own keeps the
# .wixobj and the .cab out of the build tree of the program itself, and keeps the products
# from overwriting each other's leftovers.
New-Item -ItemType Directory -Force -Path $stage | Out-Null
Copy-Item $builtBinary $stage -Force

foreach ($file in $extraFiles)
{
    Copy-Item $file $stage -Force
}

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