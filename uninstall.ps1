# ============================================================================
#  uninstall.ps1 — Uninstaller for Pomodoro on Windows
#  Usage: powershell -c "irm https://raw.githubusercontent.com/kaustubh-26/pomodoro/main/uninstall.ps1 | iex"
# ============================================================================

$ErrorActionPreference = "Stop"

Write-Host "`n  Pomodoro Uninstaller (Windows)`n" -ForegroundColor Cyan

$InstallDir = "$env:LOCALAPPDATA\Programs\Pomodoro"
$ConfigDir = "$env:APPDATA\pomodoro"

# 1. Stop running instances
Write-Host "==> Stopping running instances..." -ForegroundColor Yellow
$processes = @("pomodoro", "pomo", "pomodoro_tray_indicator")
foreach ($proc in $processes) {
    Get-Process -Name $proc -ErrorAction SilentlyContinue | Stop-Process -Force
}

# 2. Remove installed files
Write-Host "==> Removing installed files..." -ForegroundColor Yellow
if (Test-Path $InstallDir) {
    Remove-Item -Path $InstallDir -Recurse -Force
}

# 3. Remove from PATH
Write-Host "==> Updating PATH..." -ForegroundColor Yellow
$UserPath = [Environment]::GetEnvironmentVariable("Path", "User")
if ($UserPath -like "*$InstallDir*") {
    # Split the path by semicolon, remove our directory, and rejoin
    $PathArray = $UserPath -split ';' | Where-Object { $_ -ne $InstallDir -and $_ -ne '' }
    $NewPath = $PathArray -join ';'
    [Environment]::SetEnvironmentVariable("Path", $NewPath, "User")
    Write-Host "   Removed from PATH." -ForegroundColor Green
}

# 4. Remove config and logs (with prompt)
if (Test-Path $ConfigDir) {
    Write-Host ""
    $ans = Read-Host "Also remove config and session logs in $ConfigDir? [y/N]"
    if ($ans -match "^[yY]") {
        Remove-Item -Path $ConfigDir -Recurse -Force
        Write-Host "   Config and logs removed." -ForegroundColor Green
    } else {
        Write-Host "   Kept $ConfigDir"
    }
}

Write-Host "`n  Uninstalled successfully.`n" -ForegroundColor Green