# Installs/uninstalls a per-user startup shortcut that opens a VISIBLE
# console window running the GPU hold. Closing the window releases the
# dGPU back to normal power saving. Nothing is hidden; no admin needed.
#
# Usage (normal PowerShell, no elevation required):
#   .\setup-hold-startup.ps1            # install (creates Startup shortcut)
#   .\setup-hold-startup.ps1 -Remove    # remove the shortcut
#
# Paths are auto-detected: python from PATH (or -PythonExe override), the
# hold script from this directory. Override with -PythonExe / -HoldScript.
param(
  [switch]$Remove,
  [string]$PythonExe = '',
  [string]$HoldScript = ''
)
$ErrorActionPreference = 'Stop'
$shortcutName = 'GA107M GPU Hold.lnk'
$startup = [Environment]::GetFolderPath('Startup')
$lnk = Join-Path $startup $shortcutName

if ($Remove) {
  if (Test-Path $lnk) { Remove-Item $lnk -Force; "Removed: $lnk" }
  else { "No startup shortcut found" }
  exit 0
}

if (!$PythonExe) {
  $cmd = Get-Command python -ErrorAction SilentlyContinue
  if ($cmd) { $PythonExe = $cmd.Source }
  else { throw "python not found on PATH; pass -PythonExe explicitly" }
}
if (!$HoldScript) { $HoldScript = Join-Path $PSScriptRoot 'hold_gpu_windowed.py' }
if (!(Test-Path $PythonExe)) { throw "Python not found: $PythonExe" }
if (!(Test-Path $HoldScript)) { throw "hold script not found: $HoldScript" }

$shell = New-Object -ComObject WScript.Shell
$sc = $shell.CreateShortcut($lnk)
$sc.TargetPath = $PythonExe
$sc.Arguments = "`"$HoldScript`""
$sc.WorkingDirectory = Split-Path $HoldScript
$sc.Description = 'Keeps the dGPU awake to preserve the GA107M unlock (close window to release)'
$sc.Save()

if (Test-Path $lnk) {
  "Installed: $lnk"
  "  Runs: $PythonExe `"$HoldScript`""
  "At each logon a console window opens running the hold."
  "Close the window (or Ctrl+C) any time to release the GPU."
  "Remove later with:  .\setup-hold-startup.ps1 -Remove"
} else { throw "Shortcut creation failed" }
