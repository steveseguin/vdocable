param(
    [Parameter(Mandatory = $true)]
    [string]$Version,
    [string]$BuildDir = "build-release",
    [string]$Configuration = "Release",
    [string]$Repo = "steveseguin/vdocable",
    [switch]$SkipFastGate = $false
)

$ErrorActionPreference = "Stop"

function Write-Step([string]$Name) {
    Write-Host ""
    Write-Host "=== $Name ==="
}

$nativeQtRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$repoRoot = Resolve-Path (Join-Path $nativeQtRoot "..\..")
Set-Location $repoRoot

if (-not $SkipFastGate) {
    Write-Step "Fast Gate"
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot "run-fast-gate.ps1") -BuildDir $BuildDir -Configuration $Configuration
    if ($LASTEXITCODE -ne 0) {
        throw "Fast gate failed."
    }

    Write-Step "Stability Gate"
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot "run-stability-gate.ps1") -BuildDir $BuildDir -Configuration $Configuration -Iterations 3
    if ($LASTEXITCODE -ne 0) {
        throw "Stability gate failed."
    }
}

Write-Step "Build Release Artifacts"
& powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot "build-release.ps1") -BuildDir $BuildDir -Configuration $Configuration -Version $Version
if ($LASTEXITCODE -ne 0) {
    throw "build-release.ps1 failed."
}

$distRoot = Join-Path $nativeQtRoot "dist"
$versionedSetup = Join-Path $distRoot "vdocable-$Version-setup.exe"
$versionedPortable = Join-Path $distRoot "vdocable-$Version-portable.exe"
$versionedZip = Join-Path $distRoot "vdocable-$Version-win64.zip"
$stableSetup = Join-Path $distRoot "vdocable-setup.exe"
$stablePortable = Join-Path $distRoot "vdocable-portable.exe"
$stableZip = Join-Path $distRoot "vdocable-win64.zip"

foreach ($path in @($versionedSetup, $versionedPortable, $versionedZip, $stableSetup, $stablePortable, $stableZip)) {
    if (-not (Test-Path $path)) {
        throw "Missing release artifact: $path"
    }
}

$tag = "v$Version"
$title = "VDO Cable v$Version"
$notesPath = Join-Path $repoRoot ("release-notes-{0}.md" -f $tag)
$notes = @"
## VDO Cable $Version

Automated release from native QA flow:
- Fast gate (unless skipped)
- Build and package
- Code signing
- VirusTotal submission
"@
Set-Content -Path $notesPath -Value $notes -Encoding UTF8

Write-Step "Create or Update GitHub Release"
$releaseExists = $false
try {
    gh release view $tag --repo $Repo --json tagName | Out-Null
    if ($LASTEXITCODE -eq 0) {
        $releaseExists = $true
    }
} catch {
    $releaseExists = $false
}

if ($releaseExists) {
    gh release upload $tag $versionedSetup $versionedPortable $versionedZip $stableSetup $stablePortable $stableZip --clobber --repo $Repo
    gh release edit $tag --repo $Repo --title $title --notes-file $notesPath --latest
} else {
    gh release create $tag $versionedSetup $versionedPortable $versionedZip $stableSetup $stablePortable $stableZip --repo $Repo --target main --title $title --notes-file $notesPath --latest
}

Remove-Item -Path $notesPath -Force -ErrorAction SilentlyContinue

Write-Host ""
Write-Host "Release completed: https://github.com/$Repo/releases/tag/$tag"
