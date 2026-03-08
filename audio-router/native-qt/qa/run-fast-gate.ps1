param(
    [string]$BuildDir = "build-ci",
    [string]$Configuration = "Release",
    [switch]$SkipConfigure = $false,
    [switch]$SkipPackage = $false
)

$ErrorActionPreference = "Stop"

function Write-Step([string]$Name) {
    Write-Host ""
    Write-Host "=== $Name ==="
}

function Resolve-QtPrefix {
    foreach ($candidate in @(
        $env:CMAKE_PREFIX_PATH,
        $env:QT_ROOT_DIR,
        "C:\vcpkg\installed\x64-windows"
    )) {
        if (-not [string]::IsNullOrWhiteSpace($candidate) -and (Test-Path $candidate)) {
            return $candidate
        }
    }
    return ""
}

function Resolve-QtBinDirs {
    $dirs = @()
    $candidates = @("C:\vcpkg\installed\x64-windows\bin")
    if (-not [string]::IsNullOrWhiteSpace($env:QT_ROOT_DIR)) {
        $candidates = @((Join-Path $env:QT_ROOT_DIR "bin")) + $candidates
    }
    foreach ($candidate in $candidates) {
        if (-not [string]::IsNullOrWhiteSpace($candidate) -and (Test-Path $candidate)) {
            $dirs += $candidate
        }
    }
    return $dirs | Select-Object -Unique
}

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

function Invoke-SmokeLaunch([string]$ExecutablePath, [string]$PathValue, [int]$TimeoutMs) {
    $workingDir = Split-Path -Parent $ExecutablePath
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $ExecutablePath
    $psi.WorkingDirectory = $workingDir
    $psi.Arguments = "--smoke-test --auto-exit-ms 2500"
    $psi.UseShellExecute = $false
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $psi.EnvironmentVariables["PATH"] = $PathValue

    $process = [System.Diagnostics.Process]::Start($psi)
    if (-not $process.WaitForExit($TimeoutMs)) {
        try {
            $process.Kill()
        } catch {
        }
        throw "Smoke launch timed out for $ExecutablePath"
    }

    $stdout = $process.StandardOutput.ReadToEnd()
    $stderr = $process.StandardError.ReadToEnd()
    if ($process.ExitCode -ne 0) {
        throw "Smoke launch exited with code $($process.ExitCode) for $ExecutablePath`nSTDOUT: $stdout`nSTDERR: $stderr"
    }
}

function Resolve-MakensisPath {
    $command = Get-Command makensis.exe -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }
    foreach ($candidate in @(
        "C:\Program Files (x86)\NSIS\makensis.exe",
        "C:\Program Files\NSIS\makensis.exe",
        (Join-Path (Resolve-Path (Join-Path $nativeQtRoot "..\..")) "game-capture\.tools\nsis-3.11\makensis.exe"),
        (Join-Path (Resolve-Path (Join-Path $nativeQtRoot "..\..")) "game-capture\.tools\nsis-3.11\Bin\makensis.exe")
    )) {
        if (Test-Path $candidate) {
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

$nativeQtRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$reportDir = Join-Path $PSScriptRoot "reports"
New-Item -ItemType Directory -Force -Path $reportDir | Out-Null
$timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
$reportPath = Join-Path $reportDir "fast-gate-$timestamp.md"
Set-Location $nativeQtRoot

$results = @()

if (-not $SkipConfigure) {
    Write-Step "Configure"
    $cmakeArgs = @(
        "-S", ".",
        "-B", $BuildDir,
        "-G", "Visual Studio 17 2022",
        "-A", "x64",
        "-DVDO_CABLE_BUILD_TESTS=ON"
    )
    $qtPrefix = Resolve-QtPrefix
    if ($qtPrefix) {
        $cmakeArgs += @("-DCMAKE_PREFIX_PATH=$qtPrefix")
    }
    cmake @cmakeArgs
    if ($LASTEXITCODE -ne 0) {
        throw "CMake configure failed."
    }
    $results += "- Configure: PASS"
}

Write-Step "Build"
cmake --build $BuildDir --config $Configuration --target vdocable vdocable_logic_test
if ($LASTEXITCODE -ne 0) {
    throw "Build failed."
}
$results += "- Build: PASS"

Write-Step "CTest"
ctest --test-dir $BuildDir -C $Configuration --output-on-failure
if ($LASTEXITCODE -ne 0) {
    throw "CTest failed."
}
$results += "- CTest: PASS"

Write-Step "Smoke Launch"
$exePath = Resolve-ExecutablePath -RepoRoot $nativeQtRoot -BuildDir $BuildDir -Configuration $Configuration
if (-not $exePath) {
    throw "Could not locate vdocable.exe after build."
}
$originalPath = $env:PATH
$env:PATH = ((Resolve-QtBinDirs) + @((Split-Path -Parent $exePath), $originalPath)) -join ";"
Push-Location (Split-Path -Parent $exePath)
& $exePath --smoke-test --auto-exit-ms 2500
$exitCode = $LASTEXITCODE
Pop-Location
$env:PATH = $originalPath
if ($exitCode -ne 0) {
    throw "Smoke launch exited with code $exitCode."
}
$results += "- Smoke launch: PASS"

if (-not $SkipPackage) {
    Write-Step "Packaging"
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot "build-release.ps1") -BuildDir $BuildDir -Configuration $Configuration -Version "0.0.0-ci" -SkipCodeSigning -SkipVirusTotal
    if ($LASTEXITCODE -ne 0) {
        throw "Release packaging failed."
    }

    $distRoot = Join-Path $nativeQtRoot "dist"
    $zipPath = Join-Path $distRoot "vdocable-win64.zip"
    if (-not (Test-Path $zipPath)) {
        throw "Missing packaged ZIP artifact: $zipPath"
    }
    if ((Resolve-MakensisPath) -and -not (Test-Path (Join-Path $distRoot "vdocable-setup.exe"))) {
        throw "NSIS is present but setup artifact is missing."
    }
    if ((Resolve-SevenZipExe) -and -not (Test-Path (Join-Path $distRoot "vdocable-portable.exe"))) {
        throw "7-Zip is present but portable artifact is missing."
    }
    $stageExe = Join-Path $distRoot "vdocable-0.0.0-ci-win64\vdocable.exe"
    if (-not (Test-Path $stageExe)) {
        throw "Missing packaged stage executable: $stageExe"
    }
    foreach ($crtName in @("MSVCP140.dll", "VCRUNTIME140.dll", "VCRUNTIME140_1.dll")) {
        if (-not (Test-Path (Join-Path (Split-Path -Parent $stageExe) $crtName))) {
            throw "Missing required VC runtime DLL in package staging: $crtName"
        }
    }
    Invoke-SmokeLaunch -ExecutablePath $stageExe -PathValue "C:\Windows\System32;C:\Windows;C:\Windows\System32\Wbem" -TimeoutMs 10000
    $results += "- Packaged smoke launch: PASS"
    $results += "- Packaging: PASS"
}

$lines = @(
    "# Fast Gate Report",
    "",
    "- Date: $(Get-Date -Format "yyyy-MM-dd HH:mm:ss")",
    "- BuildDir: $BuildDir",
    "- Configuration: $Configuration",
    "- Executable: $exePath",
    ""
) + $results
Set-Content -Path $reportPath -Value $lines -Encoding UTF8

Write-Host ""
Write-Host "Report written to: $reportPath"
