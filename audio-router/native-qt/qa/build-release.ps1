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

function Resolve-QtBinDir {
    $candidates = @()
    if (-not [string]::IsNullOrWhiteSpace($env:QT_ROOT_DIR)) {
        $candidates += (Join-Path $env:QT_ROOT_DIR "bin")
    }

    $windeployqt = Resolve-WindeployQtPath
    if ($windeployqt) {
        $deployDir = Split-Path -Parent $windeployqt

        $vcpkgStyleRoot = Resolve-Path (Join-Path $deployDir "..\..\..") -ErrorAction SilentlyContinue
        if ($vcpkgStyleRoot) {
            $candidates += (Join-Path $vcpkgStyleRoot.Path "bin")
        }
        $candidates += $deployDir
    }

    $candidates += @(
        "C:\vcpkg\installed\x64-windows\bin",
        "C:\Qt\6.8.3\msvc2022_64\bin"
    )

    foreach ($candidate in ($candidates | Select-Object -Unique)) {
        if (-not [string]::IsNullOrWhiteSpace($candidate) -and
            (Test-Path $candidate) -and
            (Test-Path (Join-Path $candidate "Qt6Core.dll"))) {
            return $candidate
        }
    }

    foreach ($candidate in ($candidates | Select-Object -Unique)) {
        if (-not [string]::IsNullOrWhiteSpace($candidate) -and (Test-Path $candidate)) {
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

function Resolve-VcRuntimeDir {
    $roots = @()
    if (-not [string]::IsNullOrWhiteSpace($env:VCINSTALLDIR) -and (Test-Path $env:VCINSTALLDIR)) {
        $roots += (Resolve-Path (Join-Path $env:VCINSTALLDIR "..\Redist\MSVC")).Path
    }

    $roots += @(
        "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Redist\MSVC",
        "C:\Program Files (x86)\Microsoft Visual Studio\2022\Community\VC\Redist\MSVC",
        "C:\Program Files (x86)\Microsoft Visual Studio\2022\Professional\VC\Redist\MSVC",
        "C:\Program Files (x86)\Microsoft Visual Studio\2022\Enterprise\VC\Redist\MSVC"
    )

    $candidates = @()
    foreach ($root in ($roots | Select-Object -Unique)) {
        if (-not [string]::IsNullOrWhiteSpace($root) -and (Test-Path $root)) {
            $candidates += Get-ChildItem -Path $root -Directory -ErrorAction SilentlyContinue |
                ForEach-Object { Join-Path $_.FullName "x64\Microsoft.VC143.CRT" } |
                Where-Object { Test-Path $_ }
        }
    }

    if (-not $candidates) {
        return ""
    }

    return ($candidates |
        Sort-Object {
            try {
                [version](Split-Path (Split-Path $_ -Parent) -Leaf)
            } catch {
                [version]"0.0.0.0"
            }
        } -Descending |
        Select-Object -First 1)
}

function Copy-VcRuntimeLibraries([string]$StageDir) {
    $runtimeDir = Resolve-VcRuntimeDir
    if (-not $runtimeDir) {
        Write-Warning "VC runtime redist directory not found; packaged app may require the VC++ runtime to already be installed."
        return
    }

    $runtimeDlls = Get-ChildItem -Path $runtimeDir -Filter "*.dll" -File -ErrorAction SilentlyContinue
    if (-not $runtimeDlls) {
        Write-Warning "No VC runtime DLLs found in $runtimeDir"
        return
    }

    foreach ($dll in $runtimeDlls) {
        Copy-Item -Path $dll.FullName -Destination (Join-Path $StageDir $dll.Name) -Force
    }
}

function Copy-QtSupportLibraries([string]$StageDir, [string]$QtBinDir) {
    if (-not $QtBinDir -or -not (Test-Path $QtBinDir)) {
        Write-Warning "Qt bin directory not found; skipping support library copy."
        return
    }

    $supportDlls = @(
        "brotlicommon.dll",
        "brotlidec.dll",
        "bz2.dll",
        "double-conversion.dll",
        "freetype.dll",
        "harfbuzz.dll",
        "jpeg62.dll",
        "libcrypto-3-x64.dll",
        "libpng16.dll",
        "md4c.dll",
        "pcre2-16.dll",
        "zlib1.dll",
        "zstd.dll"
    )

    foreach ($dllName in $supportDlls) {
        $sourcePath = Join-Path $QtBinDir $dllName
        if (Test-Path $sourcePath) {
            Copy-Item -Path $sourcePath -Destination (Join-Path $StageDir $dllName) -Force
        }
    }
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
$iconPath = Join-Path $repoRoot "resources\vdocable.ico"
$qtBinDir = Resolve-QtBinDir

Write-Step "Stage Artifacts"
if (Test-Path $stageDir) {
    Remove-Item -Recurse -Force $stageDir
}
New-Item -ItemType Directory -Path $stageDir -Force | Out-Null
Copy-Item -Path $exePath -Destination (Join-Path $stageDir "vdocable.exe") -Force
if (Test-Path $iconPath) {
    Copy-Item -Path $iconPath -Destination (Join-Path $stageDir "vdocable.ico") -Force
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
Copy-VcRuntimeLibraries -StageDir $stageDir
Copy-QtSupportLibraries -StageDir $stageDir -QtBinDir $qtBinDir

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
    "- VC++ runtime files",
    "- vdocable.ico"
)
Set-Content -Path (Join-Path $stageDir "RELEASE-NOTES.txt") -Value $notes -Encoding UTF8

if (-not $SkipCodeSigning) {
    Write-Step "Code Signing (Staged Binary)"
    $signStageArgs = @{
        FilePaths = @(Join-Path $stageDir "vdocable.exe")
    }
    if ($FailOnSigningError) {
        $signStageArgs.FailOnError = $true
    }
    & (Join-Path $PSScriptRoot "sign-artifacts.ps1") @signStageArgs
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
    $signDistArgs = @{
        DistDir = $distRoot
        Version = $Version
    }
    if ($FailOnSigningError) {
        $signDistArgs.FailOnError = $true
    }
    & (Join-Path $PSScriptRoot "sign-artifacts.ps1") @signDistArgs
    if ($LASTEXITCODE -ne 0 -and $FailOnSigningError) {
        throw "Code signing failed for release EXEs."
    }
}

if (-not $SkipVirusTotal) {
    Write-Step "VirusTotal Submission"
    $virusTotalArgs = @{
        DistDir = $distRoot
        Version = $Version
    }
    if ($FailOnVirusTotalError) {
        $virusTotalArgs.FailOnError = $true
    }
    & (Join-Path $PSScriptRoot "submit-virustotal.ps1") @virusTotalArgs
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
