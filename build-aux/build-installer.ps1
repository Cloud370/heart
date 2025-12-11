<#
.SYNOPSIS
    Build InnoSetup installer for OBS plugin (Windows x64)
.DESCRIPTION
    This script builds the project and creates an InnoSetup installer.
.PARAMETER Config
    Build configuration (Release, RelWithDebInfo, Debug). Default: Release
.PARAMETER SkipBuild
    Skip the CMake build step
.PARAMETER Clean
    Clean build artifacts before building
#>

param(
    [ValidateSet('Release', 'RelWithDebInfo', 'Debug')]
    [string]$Config = 'Release',
    [switch]$SkipBuild,
    [switch]$Clean
)

$ErrorActionPreference = 'Stop'

$ProjectRoot = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$BuildDir = Join-Path $ProjectRoot 'build_x64'
$ReleaseDir = Join-Path $ProjectRoot 'release'

# Find InnoSetup compiler
$IsccPath = if (Get-Command iscc -ErrorAction SilentlyContinue) {
    (Get-Command iscc).Source
} elseif (Test-Path 'C:\Program Files (x86)\Inno Setup 6\ISCC.exe') {
    'C:\Program Files (x86)\Inno Setup 6\ISCC.exe'
} else {
    $null
}

if (-not $IsccPath) {
    Write-Error "InnoSetup compiler (iscc) not found. Please install InnoSetup 6."
    exit 1
}

Write-Host "=== OBS Plugin Installer Build Script ===" -ForegroundColor Cyan
Write-Host "Project Root: $ProjectRoot"
Write-Host "Build Config: $Config"
Write-Host "InnoSetup: $IsccPath"
Write-Host ""

# Clean if requested
if ($Clean) {
    Write-Host "[1/4] Cleaning build artifacts..." -ForegroundColor Yellow
    if (Test-Path $BuildDir) {
        Remove-Item -Recurse -Force $BuildDir
    }
    if (Test-Path (Join-Path $ReleaseDir 'plugintemplate-for-obs')) {
        Remove-Item -Recurse -Force (Join-Path $ReleaseDir 'plugintemplate-for-obs')
    }
    if (Test-Path (Join-Path $ReleaseDir 'Output')) {
        Remove-Item -Recurse -Force (Join-Path $ReleaseDir 'Output')
    }
} else {
    Write-Host "[1/4] Skipping clean..." -ForegroundColor DarkGray
}

# Configure CMake
if (-not (Test-Path $BuildDir) -or $Clean) {
    Write-Host "[2/4] Configuring CMake..." -ForegroundColor Yellow
    Push-Location $ProjectRoot
    try {
        cmake --preset windows-x64
        if ($LASTEXITCODE -ne 0) { throw "CMake configure failed" }
    } finally {
        Pop-Location
    }
} else {
    Write-Host "[2/4] Skipping CMake configure (build directory exists)..." -ForegroundColor DarkGray
}

# Build
if (-not $SkipBuild) {
    Write-Host "[3/4] Building project ($Config)..." -ForegroundColor Yellow
    cmake --build $BuildDir --config $Config
    if ($LASTEXITCODE -ne 0) { throw "CMake build failed" }

    # Install to release directory
    Write-Host "       Installing to release directory..." -ForegroundColor Yellow
    cmake --install $BuildDir --config $Config --prefix $ReleaseDir
    if ($LASTEXITCODE -ne 0) { throw "CMake install failed" }
} else {
    Write-Host "[3/4] Skipping build..." -ForegroundColor DarkGray
}

# Build installer
Write-Host "[4/4] Building InnoSetup installer..." -ForegroundColor Yellow
$IssFile = Get-ChildItem -Path $ReleaseDir -Filter '*.iss' | Select-Object -First 1
if (-not $IssFile) {
    throw "No .iss file found in $ReleaseDir"
}

Push-Location $ReleaseDir
try {
    & $IsccPath $IssFile.Name
    if ($LASTEXITCODE -ne 0) { throw "InnoSetup compilation failed" }
} finally {
    Pop-Location
}

# Done
$InstallerPath = Get-ChildItem -Path (Join-Path $ReleaseDir 'Output') -Filter '*.exe' | Select-Object -First 1
Write-Host ""
Write-Host "=== Build Complete ===" -ForegroundColor Green
Write-Host "Installer: $($InstallerPath.FullName)" -ForegroundColor Green
