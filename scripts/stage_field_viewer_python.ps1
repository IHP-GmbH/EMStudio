# Stage an embeddable CPython + Field viewer deps into build/dist/field_viewer_python
# for the Windows installer. Used by CI (.github/workflows/build.yml).
#
# Usage (from repo root, after dist/ exists):
#   powershell -File scripts/stage_field_viewer_python.ps1 [-DestDir build\dist\field_viewer_python]

param(
    [string]$DestDir = "",
    [string]$PythonVersion = "3.11.9"
)

$ErrorActionPreference = "Stop"

$RepoRoot = Split-Path -Parent $PSScriptRoot
if (-not $DestDir) {
    $DestDir = Join-Path $RepoRoot "build\dist\field_viewer_python"
}

$ReqFile = Join-Path $RepoRoot "requirements-field-viewer.txt"
if (-not (Test-Path $ReqFile)) {
    throw "Missing $ReqFile"
}

Write-Host "Staging Field viewer Python → $DestDir"

if (Test-Path $DestDir) {
    Remove-Item -Recurse -Force $DestDir
}
New-Item -ItemType Directory -Path $DestDir | Out-Null

$ZipName = "python-$PythonVersion-embed-amd64.zip"
$ZipUrl = "https://www.python.org/ftp/python/$PythonVersion/$ZipName"
$ZipPath = Join-Path $env:TEMP $ZipName

Write-Host "Downloading $ZipUrl"
Invoke-WebRequest -Uri $ZipUrl -OutFile $ZipPath -UseBasicParsing

Write-Host "Extracting embeddable Python"
Expand-Archive -Path $ZipPath -DestinationPath $DestDir -Force

$PythonExe = Join-Path $DestDir "python.exe"
if (-not (Test-Path $PythonExe)) {
    throw "python.exe missing after extract"
}

# Enable site-packages / pip. Must be ASCII — UTF-16 from Set-Content breaks ._pth.
$Pth = Get-ChildItem -Path $DestDir -Filter "python*._pth" | Select-Object -First 1
if (-not $Pth) {
    throw "python*._pth not found in $DestDir"
}
$ZipStem = [System.IO.Path]::GetFileNameWithoutExtension(
    (Get-ChildItem -Path $DestDir -Filter "python*.zip" | Select-Object -First 1).Name
)
$PthBody = @"
$ZipStem.zip
.
Lib\site-packages
import site
"@
[System.IO.File]::WriteAllText($Pth.FullName, ($PthBody -replace "`n", "`r`n"))

$SitePackages = Join-Path $DestDir "Lib\site-packages"
New-Item -ItemType Directory -Path $SitePackages -Force | Out-Null

$GetPip = Join-Path $env:TEMP "get-pip.py"
Invoke-WebRequest -Uri "https://bootstrap.pypa.io/get-pip.py" -OutFile $GetPip -UseBasicParsing

Write-Host "Installing pip into embeddable Python"
& $PythonExe $GetPip --no-warn-script-location
if ($LASTEXITCODE -ne 0) { throw "get-pip failed ($LASTEXITCODE)" }

Write-Host "Installing Field viewer packages from $ReqFile"
& $PythonExe -m pip install --no-warn-script-location -U pip
if ($LASTEXITCODE -ne 0) { throw "pip upgrade failed ($LASTEXITCODE)" }
& $PythonExe -m pip install --no-warn-script-location -r $ReqFile
if ($LASTEXITCODE -ne 0) { throw "pip install field viewer deps failed ($LASTEXITCODE)" }

# Refuse accidental full PySide6 (Addons) — Essentials must be enough (#30).
$addonsMarker = Join-Path $SitePackages "PySide6\QtWebEngineCore.pyd"
if (Test-Path $addonsMarker) {
    throw "PySide6 Addons present ($addonsMarker); use PySide6-Essentials only"
}

Write-Host "Smoke import (pyvista / Pillow / pyvistaqt / PySide6 Essentials)"
$prevQtPlatform = $env:QT_QPA_PLATFORM
$env:QT_QPA_PLATFORM = "offscreen"
$smoke = @'
import os
os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
import pyvista as pv
import PIL  # noqa: F401
from PySide6.QtWidgets import QApplication
from PySide6.QtOpenGLWidgets import QOpenGLWidget  # noqa: F401
from pyvistaqt import QtInteractor
app = QApplication.instance() or QApplication([])
interactor = QtInteractor(off_screen=True)
print("field_viewer_python ok", pv.__version__, type(interactor).__name__)
'@
& $PythonExe -c $smoke
$smokeCode = $LASTEXITCODE
if ($null -ne $prevQtPlatform) { $env:QT_QPA_PLATFORM = $prevQtPlatform }
else { Remove-Item Env:QT_QPA_PLATFORM -ErrorAction SilentlyContinue }
if ($smokeCode -ne 0) { throw "smoke import failed ($smokeCode)" }

Write-Host "Removing build-only pip / setuptools / caches"
$buildOnly = @(
    "pip", "pip-*",
    "setuptools", "setuptools-*",
    "wheel", "wheel-*",
    "pkg_resources",
    "_distutils_hack", "distutils-precedence.pth"
)
foreach ($pat in $buildOnly) {
    Get-ChildItem -Path $SitePackages -Force -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -like $pat } |
        ForEach-Object { Remove-Item -LiteralPath $_.FullName -Recurse -Force -ErrorAction SilentlyContinue }
}
$ScriptsDir = Join-Path $DestDir "Scripts"
if (Test-Path $ScriptsDir) {
    Get-ChildItem -Path $ScriptsDir -Force -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -match '^(pip|easy_install)' } |
        ForEach-Object { Remove-Item -LiteralPath $_.FullName -Force -ErrorAction SilentlyContinue }
}
Get-ChildItem -Path $DestDir -Recurse -Directory -Filter "__pycache__" -ErrorAction SilentlyContinue |
    ForEach-Object { Remove-Item -LiteralPath $_.FullName -Recurse -Force -ErrorAction SilentlyContinue }

$Readme = @"
EMStudio bundled Python for Layout Field (2D) and Field 3D.
Do not replace this folder; Preferences FIELD_VIEWER_PYTHON may point here.
Packages: see requirements-field-viewer.txt in the EMStudio source tree
(PySide6-Essentials, not the full PySide6 Addons wheel).
"@
[System.IO.File]::WriteAllText((Join-Path $DestDir "README.txt"), $Readme)

Write-Host "Done: $DestDir"
Get-ChildItem $DestDir | Select-Object Name, Length | Format-Table -AutoSize
