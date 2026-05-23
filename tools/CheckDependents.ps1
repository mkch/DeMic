# CheckDependents.ps1
# Checks *.exe in the current directory and *.plugin in the plugin subdirectory
# for dependencies on non-system DLLs.
#
# Usage: Run from the directory containing the exe files, e.g.:
#   CheckDependents.ps1
#   CheckDependents.ps1 -TargetDir "x64\Release"

param(
    [string]$TargetDir = "."
)

# Locate dumpbin.exe via PATH first, then via vswhere.
function Find-Dumpbin {
    $found = Get-Command dumpbin.exe -ErrorAction SilentlyContinue
    if ($found) { return $found.Source }

    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) {
        $vswhere = "${env:ProgramFiles}\Microsoft Visual Studio\Installer\vswhere.exe"
    }
    if (Test-Path $vswhere) {
        $vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2>$null
        if ($vsPath) {
            $candidates = Get-ChildItem -Path $vsPath -Filter dumpbin.exe -Recurse -ErrorAction SilentlyContinue
            if ($candidates) { return $candidates[0].FullName }
        }
    }
    return $null
}

# Returns $true if the DLL name is a known Windows system DLL
# (i.e., it lives in System32 / SysWOW64 / the Windows dir itself).
function Is-SystemDll([string]$dllName) {
    $winDir   = [System.Environment]::GetFolderPath("Windows")
    $sys32    = Join-Path $winDir "System32"
    $syswow64 = Join-Path $winDir "SysWOW64"
    $sysnative = Join-Path $winDir "Sysnative"   # 32-bit process on 64-bit OS

    foreach ($dir in @($sys32, $syswow64, $sysnative, $winDir)) {
        if (Test-Path (Join-Path $dir $dllName)) { return $true }
    }
    return $false
}

# Runs dumpbin /DEPENDENTS on a file and returns the list of imported DLL names.
function Get-Dependents([string]$dumpbin, [string]$filePath) {
    $output = & $dumpbin /DEPENDENTS $filePath 2>&1
    $dlls = @()
    $inSection = $false
    foreach ($line in $output) {
        if ($line -match 'Image has the following dependencies') {
            $inSection = $true
            continue
        }
        if ($inSection) {
            $trimmed = $line.Trim()
            if ($trimmed -eq '') { continue }
            # Dependency lines contain only a DLL name (no spaces, ends with .dll/.plugin etc.)
            if ($trimmed -match '^[\w\-\.\+]+\.(?:dll|plugin|exe)$') {
                $dlls += $trimmed
            } elseif ($trimmed -match '^Summary') {
                break
            }
        }
    }
    return $dlls
}

# ---- Main ----

$dumpbin = Find-Dumpbin
if (-not $dumpbin) {
    Write-Error "dumpbin.exe not found. Please run this script from a Visual Studio Developer PowerShell, or install Visual Studio with C++ tools."
    exit 1
}
Write-Host "Using dumpbin: $dumpbin" -ForegroundColor DarkGray

$targetDir = Resolve-Path $TargetDir -ErrorAction Stop
$pluginDir  = Join-Path $targetDir "plugin"

# Collect files to check.
$files  = @(Get-ChildItem -Path $targetDir -Filter "*.exe" -File -ErrorAction SilentlyContinue)
$files += @(Get-ChildItem -Path $pluginDir -Filter "*.plugin" -File -ErrorAction SilentlyContinue)

if ($files.Count -eq 0) {
    Write-Warning "No *.exe or plugin\*.plugin files found under: $targetDir"
    exit 0
}

$anyNonSystem = $false

foreach ($file in $files) {
    $relPath = $file.FullName.Substring($targetDir.Path.Length).TrimStart('\')
    Write-Host ""
    Write-Host "Checking: $relPath" -ForegroundColor Cyan

    $deps = Get-Dependents $dumpbin $file.FullName
    if ($deps.Count -eq 0) {
        Write-Host "  (no import dependencies found)" -ForegroundColor DarkGray
        continue
    }

    $nonSystem = @()
    foreach ($dll in $deps) {
        if (-not (Is-SystemDll $dll)) {
            $nonSystem += $dll
        }
    }

    if ($nonSystem.Count -eq 0) {
        Write-Host "  OK - all dependencies are system DLLs." -ForegroundColor Green
    } else {
        $anyNonSystem = $true
        foreach ($dll in $nonSystem) {
            Write-Host "  [NON-SYSTEM] $dll" -ForegroundColor Yellow
        }
    }
}

Write-Host ""
if ($anyNonSystem) {
    Write-Host "Result: Non-system DLL dependencies found (see above)." -ForegroundColor Yellow
    exit 1
} else {
    Write-Host "Result: All dependencies are system DLLs." -ForegroundColor Green
    exit 0
}
