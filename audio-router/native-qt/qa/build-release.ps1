param(
    [string]$BuildDir = "build-release",
    [string]$Configuration = "Release",
    [string]$Version = "0.1.0",
    [switch]$SkipCodeSigning = $false,
    [switch]$SkipVirusTotal = $false,
    [switch]$FailOnSigningError = $false,
    [switch]$FailOnVirusTotalError = $false
)

$ErrorActionPreference = "Stop"

function Resolve-ExecutablePath([string]$RepoRoot, [string]$BuildDir, [string]$Configuration) {
    $candidates = @(
        (Join-Path $RepoRoot "$BuildDir/bin/$Configuration/vdocable.exe"),
        (Join-Path $RepoRoot "$BuildDir/bin/vdocable.exe")
    )
    foreach ($candidate in $candidates) {
        if (Test-Path $candidate) {
            return (Resolve-Path $candidate).Path
        }
    }
    return ""
}

function Resolve-WindeployQtPath {
    $command = Get-Command windeployqt.exe -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }

    $candidates = @(
        "C:\Qt\6.8.3\msvc2022_64\bin\windeployqt.exe",
        "C:\vcpkg\installed\x64-windows\tools\Qt6\bin\windeployqt.exe"
    )
    if (-not [string]::IsNullOrWhiteSpace($env:QT_ROOT_DIR)) {
        $candidates = @((Join-Path $env:QT_ROOT_DIR "bin\windeployqt.exe")) + $candidates
    }

    foreach ($candidate in $candidates) {
        if (-not [string]::IsNullOrWhiteSpace($candidate) -and (Test-Path $candidate)) {
            return $candidate
        }
    }

    return ""
}

function Resolve-SevenZipExe {
    $command = Get-Command 7z.exe -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }
    foreach ($candidate in @(
        "C:\Program Files\7-Zip\7z.exe",
        "C:\Program Files (x86)\7-Zip\7z.exe"
    )) {
        if (Test-Path $candidate) {
            return $candidate
        }
    }
    return ""
}

function Resolve-SevenZipSfx {
    foreach ($candidate in @(
        "C:\Program Files\7-Zip\7z.sfx",
        "C:\Program Files (x86)\7-Zip\7z.sfx"
    )) {
        if (Test-Path $candidate) {
            return $candidate
        }
    }
    return ""
}

function Resolve-MakensisPath {
    $command = Get-Command makensis.exe -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }
    $nativeQtRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
    $workspaceRoot = Resolve-Path (Join-Path $nativeQtRoot "..\..")
    foreach ($candidate in @(
        "C:\Program Files (x86)\NSIS\makensis.exe",
        "C:\Program Files\NSIS\makensis.exe",
        (Join-Path $workspaceRoot "game-capture\.tools\nsis-3.11\makensis.exe"),
        (Join-Path $workspaceRoot "game-capture\.tools\nsis-3.11\Bin\makensis.exe")
    )) {
        if (Test-Path $candidate) {
            return $candidate
        }
    }
    return ""
}

function Convert-ToNsisVersion([string]$Value) {
    $parts = @($Value -split '[^0-9]+' | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
    while ($parts.Count -lt 4) {
        $parts += "0"
    }
    if ($parts.Count -gt 4) {
        $parts = $parts[0..3]
    }
    return ($parts -join ".")
}

function Write-Step([string]$Name) {
    Write-Host ""
    Write-Host "=== $Name ==="
}

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$workspaceRoot = Resolve-Path (Join-Path $repoRoot "..\..")
Set-Location $repoRoot

$exePath = Resolve-ExecutablePath -RepoRoot $repoRoot -BuildDir $BuildDir -Configuration $Configuration
if (-not $exePath) {
    throw "Could not locate vdocable.exe in build output. Build first: $BuildDir"
}

$artifactPrefix = "vdocable"
$distRoot = Join-Path $repoRoot "dist"
$stageDir = Join-Path $distRoot "$artifactPrefix-$Version-win64"
$zipPath = Join-Path $distRoot "$artifactPrefix-$Version-win64.zip"
$zipStablePath = Join-Path $distRoot "$artifactPrefix-win64.zip"
$installerVersionedPath = Join-Path $distRoot "$artifactPrefix-$Version-setup.exe"
$installerStablePath = Join-Path $distRoot "$artifactPrefix-setup.exe"
$portableVersionedPath = Join-Path $distRoot "$artifactPrefix-$Version-portable.exe"
$portableStablePath = Join-Path $distRoot "$artifactPrefix-portable.exe"
$iconPath = Join-Path $workspaceRoot "game-capture\native-qt\resources\vdoninja.ico"

Write-Step "Stage Artifacts"
if (Test-Path $stageDir) {
    Remove-Item -Recurse -Force $stageDir
}
New-Item -ItemType Directory -Path $stageDir -Force | Out-Null
Copy-Item -Path $exePath -Destination (Join-Path $stageDir "vdocable.exe") -Force
if (Test-Path $iconPath) {
    Copy-Item -Path $iconPath -Destination (Join-Path $stageDir "vdoninja.ico") -Force
}

$windeployqt = Resolve-WindeployQtPath
if ($windeployqt) {
    Write-Step "Run windeployqt"
    & $windeployqt --release --no-translations --compiler-runtime --dir $stageDir $exePath
    if ($LASTEXITCODE -ne 0) {
        throw "windeployqt failed with exit code $LASTEXITCODE"
    }
} else {
    Write-Host "windeployqt not found; copying local runtime files from build output."
    $exeDir = Split-Path -Parent $exePath
    Get-ChildItem -Path $exeDir -Filter "*.dll" -File -ErrorAction SilentlyContinue |
        ForEach-Object { Copy-Item -Path $_.FullName -Destination $stageDir -Force }
    foreach ($subDir in @("platforms", "styles", "imageformats")) {
        $src = Join-Path $exeDir $subDir
        if (Test-Path $src) {
            Copy-Item -Path $src -Destination (Join-Path $stageDir $subDir) -Recurse -Force
        }
    }
}

$platformsDir = Join-Path $stageDir "platforms"
$stylesDir = Join-Path $stageDir "styles"
New-Item -ItemType Directory -Path $platformsDir -Force | Out-Null
New-Item -ItemType Directory -Path $stylesDir -Force | Out-Null

$qtPluginRoots = @()
if ($env:QT_PLUGIN_PATH) {
    $qtPluginRoots += $env:QT_PLUGIN_PATH
}
if (-not [string]::IsNullOrWhiteSpace($env:QT_ROOT_DIR)) {
    $qtPluginRoots += (Join-Path $env:QT_ROOT_DIR "plugins")
}
$qtPluginRoots += @(
    "C:\vcpkg\installed\x64-windows\Qt6\plugins",
    "C:\vcpkg\installed\x64-windows\plugins"
)

$qwindowsTarget = Join-Path $platformsDir "qwindows.dll"
if (-not (Test-Path $qwindowsTarget)) {
    foreach ($root in $qtPluginRoots) {
        $candidate = Join-Path $root "platforms\qwindows.dll"
        if (Test-Path $candidate) {
            Copy-Item -Path $candidate -Destination $qwindowsTarget -Force
            break
        }
    }
}
if (-not (Test-Path $qwindowsTarget)) {
    throw "Missing required Qt platform plugin qwindows.dll in release staging."
}

$styleTarget = Join-Path $stylesDir "qmodernwindowsstyle.dll"
if (-not (Test-Path $styleTarget)) {
    foreach ($root in $qtPluginRoots) {
        $candidate = Join-Path $root "styles\qmodernwindowsstyle.dll"
        if (Test-Path $candidate) {
            Copy-Item -Path $candidate -Destination $styleTarget -Force
            break
        }
    }
}

$notes = @(
    "VDO Cable Release",
    "Version: $Version",
    "BuildDir: $BuildDir",
    "Configuration: $Configuration",
    "Built: $(Get-Date -Format "yyyy-MM-dd HH:mm:ss")",
    "",
    "Contents:",
    "- vdocable.exe",
    "- Qt runtime files",
    "- vdoninja.ico"
)
Set-Content -Path (Join-Path $stageDir "RELEASE-NOTES.txt") -Value $notes -Encoding UTF8

if (-not $SkipCodeSigning) {
    Write-Step "Code Signing (Staged Binary)"
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot "sign-artifacts.ps1") -FilePaths @(Join-Path $stageDir "vdocable.exe") -FailOnError:$FailOnSigningError
    if ($LASTEXITCODE -ne 0 -and $FailOnSigningError) {
        throw "Code signing failed for staged binary."
    }
}

Write-Step "Zip Package"
if (Test-Path $zipPath) {
    Remove-Item -Force $zipPath
}
Compress-Archive -Path (Join-Path $stageDir "*") -DestinationPath $zipPath -Force
Copy-Item -Path $zipPath -Destination $zipStablePath -Force

Write-Step "Portable EXE"
$sevenZipExe = Resolve-SevenZipExe
$sevenZipSfx = Resolve-SevenZipSfx
$portableConfig = Join-Path $repoRoot "portable-sfx-config.txt"
$portableArchive = Join-Path $distRoot "$artifactPrefix-$Version-portable.7z"
if ($sevenZipExe -and $sevenZipSfx -and (Test-Path $portableConfig)) {
    if (Test-Path $portableArchive) {
        Remove-Item -Force $portableArchive
    }
    if (Test-Path $portableVersionedPath) {
        Remove-Item -Force $portableVersionedPath
    }
    & $sevenZipExe a -t7z -mx=9 $portableArchive (Join-Path $stageDir "*")
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to create portable archive via 7-Zip."
    }
    cmd /c "copy /b `"$sevenZipSfx`" + `"$portableConfig`" + `"$portableArchive`" `"$portableVersionedPath`" >nul"
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to create portable executable SFX."
    }
    Copy-Item -Path $portableVersionedPath -Destination $portableStablePath -Force
    Remove-Item -Force $portableArchive
} else {
    Write-Host "7-Zip or portable config missing; skipped portable SFX creation."
}

Write-Step "NSIS Installer"
$makensis = Resolve-MakensisPath
if ($makensis) {
    $numericVersion = Convert-ToNsisVersion -Value $Version
    if (Test-Path $installerVersionedPath) {
        Remove-Item -Force $installerVersionedPath
    }
    & $makensis /V2 "/DVERSION=$Version" "/DPRODUCT_VERSION_NUMERIC=$numericVersion" "/DBUILD_BIN_DIR=$stageDir" "/DOUTFILE=$installerVersionedPath" installer.nsi
    if ($LASTEXITCODE -ne 0) {
        throw "NSIS installer build failed."
    }
    Copy-Item -Path $installerVersionedPath -Destination $installerStablePath -Force
} else {
    Write-Host "makensis not found; skipped installer build."
}

if (-not $SkipCodeSigning) {
    Write-Step "Code Signing (Release EXEs)"
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot "sign-artifacts.ps1") -DistDir $distRoot -Version $Version -FailOnError:$FailOnSigningError
    if ($LASTEXITCODE -ne 0 -and $FailOnSigningError) {
        throw "Code signing failed for release EXEs."
    }
}

if (-not $SkipVirusTotal) {
    Write-Step "VirusTotal Submission"
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot "submit-virustotal.ps1") -DistDir $distRoot -Version $Version -FailOnError:$FailOnVirusTotalError
    if ($LASTEXITCODE -ne 0 -and $FailOnVirusTotalError) {
        throw "VirusTotal submission failed."
    }
}

Write-Host ""
Write-Host "Release staging dir: $stageDir"
Write-Host "Release zip: $zipPath"
if (Test-Path $portableVersionedPath) {
    Write-Host "Release portable: $portableVersionedPath"
}
if (Test-Path $installerVersionedPath) {
    Write-Host "Release installer: $installerVersionedPath"
}
