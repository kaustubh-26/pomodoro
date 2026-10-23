# ============================================================================
#  install.ps1 — One-line installer for Pomodoro on Windows
#  Usage: powershell -ExecutionPolicy Bypass -c "irm https://raw.githubusercontent.com/kaustubh-26/pomodoro/main/install.ps1 | iex"
# ============================================================================

$ErrorActionPreference = "Stop"

Write-Host "`n  Pomodoro Installer (Windows)`n" -ForegroundColor Cyan
Write-Host "  Note: Windows Defender may flag this script or the .exe" -ForegroundColor Yellow
Write-Host "  due to the 'download and execute' pattern. If prompted," -ForegroundColor Yellow
Write-Host "  please click 'More info' -> 'Run anyway'.`n" -ForegroundColor Yellow

# Define paths
$RepoUrl = "https://github.com/kaustubh-26/pomodoro"
$ZipUrl = "$RepoUrl/releases/latest/download/pomodoro-windows-x86_64.zip"
$InstallDir = "$env:LOCALAPPDATA\Programs\Pomodoro"
$TempZip = "$env:TEMP\pomodoro.zip"

# 1. Download the latest release
Write-Host "==> Downloading latest release..." -ForegroundColor Cyan
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
Invoke-WebRequest -Uri $ZipUrl -OutFile $TempZip

# Remove the "Mark of the Web" that triggers Defender
Unblock-File -Path $TempZip

# 2. Extract to install directory
Write-Host "==> Extracting to $InstallDir..." -ForegroundColor Cyan
if (Test-Path $InstallDir) {
    Remove-Item -Path $InstallDir -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $InstallDir | Out-Null
Expand-Archive -Path $TempZip -DestinationPath $InstallDir -Force
Remove-Item -Path $TempZip

# Unblock all extracted .exe files so they don't get quarantined
Get-ChildItem -Path $InstallDir -Filter *.exe | Unblock-File

# 3. Add to User PATH
Write-Host "==> Updating PATH..." -ForegroundColor Cyan
$UserPath = [Environment]::GetEnvironmentVariable("Path", "User")
if ($UserPath -notlike "*$InstallDir*") {
    $NewPath = $UserPath + ";" + $InstallDir
    [Environment]::SetEnvironmentVariable("Path", $NewPath, "User")
    Write-Host "   Added to PATH. (You may need to restart your terminal)." -ForegroundColor Green
} else {
    Write-Host "   Already in PATH." -ForegroundColor Green
}

# 4. Done!
Write-Host "`n  Done.`n" -ForegroundColor Green
Write-Host "Quick start:"
Write-Host "  pomodoro.exe --daemon   # background + tray"
Write-Host "  pomodoro.exe            # foreground UI"
Write-Host "  pomo.exe                # attach to a running instance`n"