param(
    [string]$BuildDir = "build-ci",
    [string]$Configuration = "Release",
    [int]$Iterations = 3
)

$ErrorActionPreference = "Stop"

function Write-Step([string]$Name) {
    Write-Host ""
    Write-Host "=== $Name ==="
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

$nativeQtRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$reportDir = Join-Path $PSScriptRoot "reports"
New-Item -ItemType Directory -Force -Path $reportDir | Out-Null
$timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
$reportPath = Join-Path $reportDir "stability-gate-$timestamp.md"
Set-Location $nativeQtRoot

$exePath = Resolve-ExecutablePath -RepoRoot $nativeQtRoot -BuildDir $BuildDir -Configuration $Configuration
if (-not $exePath) {
    throw "Could not locate vdocable.exe after build."
}

$results = @()
$originalPath = $env:PATH
$env:PATH = ((Resolve-QtBinDirs) + @((Split-Path -Parent $exePath), $originalPath)) -join ";"

try {
    Push-Location (Split-Path -Parent $exePath)
    for ($i = 1; $i -le [Math]::Max(1, $Iterations); $i++) {
        Write-Step "Smoke Cycle $i"
        & $exePath --smoke-test --auto-exit-ms 1800
        $exitCode = if ($null -ne $LASTEXITCODE) { [int]$LASTEXITCODE } else { 0 }
        if ($exitCode -ne 0) {
            throw "Smoke cycle $i failed with exit code $exitCode."
        }
        Start-Sleep -Milliseconds 300
        $results += "- Smoke cycle ${i}: PASS"
    }
}
finally {
    if (Get-Location) {
        Pop-Location
    }
    $env:PATH = $originalPath
}

$lines = @(
    "# Stability Gate Report",
    "",
    "- Date: $(Get-Date -Format "yyyy-MM-dd HH:mm:ss")",
    "- BuildDir: $BuildDir",
    "- Configuration: $Configuration",
    "- Executable: $exePath",
    "- Iterations: $Iterations",
    ""
) + $results
Set-Content -Path $reportPath -Value $lines -Encoding UTF8

Write-Host ""
Write-Host "Report written to: $reportPath"
