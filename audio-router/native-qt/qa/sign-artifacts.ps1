param(
    [string]$DistDir = "",
    [string]$Version = "",
    [string]$CodeSigningRepo = "C:\Users\Steve\code\code-signing",
    [string[]]$FilePaths = @(),
    [string]$PfxPath = "",
    [string]$Password = "",
    [switch]$FailOnError = $false
)

$ErrorActionPreference = "Stop"

function Resolve-SigntoolPath {
    $command = Get-Command signtool.exe -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }

    $candidate = Get-ChildItem "C:\Program Files (x86)\Windows Kits\10\bin" -Recurse -Filter "signtool.exe" -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -like "*\x64\signtool.exe" } |
        Sort-Object FullName -Descending |
        Select-Object -First 1
    if ($candidate) {
        return $candidate.FullName
    }

    return ""
}

function Resolve-CodeSigningPassword([string]$CodeSigningRepoPath, [string]$ExplicitPassword) {
    if (-not [string]::IsNullOrWhiteSpace($ExplicitPassword)) {
        return $ExplicitPassword.Trim()
    }
    foreach ($envName in @("WINDOWS_SIGN_PFX_PASSWORD", "SIGNING_CERT_PASSWORD", "WIN_CSC_KEY_PASSWORD")) {
        $envValue = (Get-Item "Env:$envName" -ErrorAction SilentlyContinue).Value
        if (-not [string]::IsNullOrWhiteSpace($envValue)) {
            return $envValue.Trim()
        }
    }

    $configPath = Join-Path $CodeSigningRepoPath "secrets\decrypted\build-config.env"
    if (-not (Test-Path $configPath)) {
        return ""
    }

    $line = Get-Content $configPath -ErrorAction Stop | Where-Object { $_ -match '^WIN_CSC_KEY_PASSWORD=' } | Select-Object -First 1
    if (-not $line) {
        return ""
    }

    return ($line -replace '^WIN_CSC_KEY_PASSWORD=', '').Trim()
}

function Resolve-PfxPath([string]$ExplicitPfxPath, [string]$CodeSigningRepoPath) {
    if (-not [string]::IsNullOrWhiteSpace($ExplicitPfxPath) -and (Test-Path $ExplicitPfxPath)) {
        return @{
            Path = (Resolve-Path $ExplicitPfxPath).Path
            Temp = $false
        }
    }

    $base64Value = $env:WINDOWS_SIGN_PFX_BASE64
    if ([string]::IsNullOrWhiteSpace($base64Value)) {
        $base64Value = $env:SIGNING_CERT_PFX_BASE64
    }
    if (-not [string]::IsNullOrWhiteSpace($base64Value)) {
        $tempPath = Join-Path ([System.IO.Path]::GetTempPath()) "vdocable-codesign.pfx"
        [System.IO.File]::WriteAllBytes($tempPath, [Convert]::FromBase64String($base64Value))
        return @{
            Path = $tempPath
            Temp = $true
        }
    }

    $localPfx = Join-Path $CodeSigningRepoPath "secrets\decrypted\certs\socialstream.pfx"
    if (Test-Path $localPfx) {
        return @{
            Path = $localPfx
            Temp = $false
        }
    }

    return @{
        Path = ""
        Temp = $false
    }
}

function Sign-File([string]$signtoolPath, [string]$pfxPath, [string]$password, [string]$filePath) {
    & $signtoolPath sign /fd SHA256 /td SHA256 /tr "http://timestamp.digicert.com" /f $pfxPath /p $password $filePath | Out-Host
    if ($LASTEXITCODE -ne 0) {
        throw "signtool failed for '$filePath' with exit code $LASTEXITCODE"
    }
}

function Test-SignatureAcceptable($signature) {
    if (-not $signature -or -not $signature.SignerCertificate) {
        return $false
    }
    $hardFailures = @("NotSigned", "HashMismatch", "NotSupported", "Incompatible")
    return -not ($hardFailures -contains [string]$signature.Status)
}

$nativeQtRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
if (-not $DistDir) {
    $DistDir = Join-Path $nativeQtRoot "dist"
}

if (-not (Test-Path $DistDir)) {
    Write-Host "Code signing: dist directory not found ($DistDir); skipping."
    exit 0
}

$signtoolPath = Resolve-SigntoolPath
if (-not $signtoolPath) {
    $message = "Code signing: signtool.exe not found; skipping."
    if ($FailOnError) {
        throw $message
    }
    Write-Warning $message
    exit 0
}

$pfxInfo = Resolve-PfxPath -ExplicitPfxPath $PfxPath -CodeSigningRepoPath $CodeSigningRepo
if (-not $pfxInfo.Path) {
    $message = "Code signing: no certificate bundle found; skipping."
    if ($FailOnError) {
        throw $message
    }
    Write-Warning $message
    exit 0
}

$password = Resolve-CodeSigningPassword -CodeSigningRepoPath $CodeSigningRepo -ExplicitPassword $Password
if (-not $password) {
    $message = "Code signing: certificate password missing; skipping."
    if ($FailOnError) {
        throw $message
    }
    Write-Warning $message
    if ($pfxInfo.Temp) {
        Remove-Item -Path $pfxInfo.Path -Force -ErrorAction SilentlyContinue
    }
    exit 0
}

$allExes = @()
if ($FilePaths -and $FilePaths.Count -gt 0) {
    foreach ($path in $FilePaths) {
        if ([string]::IsNullOrWhiteSpace($path)) {
            continue
        }
        if (Test-Path $path) {
            $allExes += Get-Item (Resolve-Path $path)
        } else {
            Write-Warning "Code signing: file path not found, skipping '$path'"
        }
    }
    $allExes = $allExes | Where-Object { $_.Extension -ieq ".exe" }
} else {
    $allExes = Get-ChildItem -Path $DistDir -File -Filter "*.exe" -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -like "vdocable*.exe" -and $_.Name -notlike "*uninstall*" }

    if ($Version) {
        $stableNames = @("vdocable-setup.exe", "vdocable-portable.exe")
        $versionPrefix = "vdocable-$Version-"
        $allExes = $allExes | Where-Object {
            ($stableNames -contains $_.Name) -or $_.Name.StartsWith($versionPrefix, [System.StringComparison]::OrdinalIgnoreCase)
        }
    }
}

$allExes = $allExes | Sort-Object FullName -Unique
if (-not $allExes) {
    Write-Host "Code signing: no matching VDO Cable EXEs found; skipping."
    if ($pfxInfo.Temp) {
        Remove-Item -Path $pfxInfo.Path -Force -ErrorAction SilentlyContinue
    }
    exit 0
}

$failures = @()
Write-Host "Code signing: signing $($allExes.Count) EXE artifact(s)..."
foreach ($file in $allExes) {
    try {
        Sign-File -signtoolPath $signtoolPath -pfxPath $pfxInfo.Path -password $password -filePath $file.FullName
        $signature = Get-AuthenticodeSignature -FilePath $file.FullName
        if (-not (Test-SignatureAcceptable -signature $signature)) {
            throw "Signature check failed (status=$($signature.Status), message=$($signature.StatusMessage))"
        }
        $subject = if ($signature.SignerCertificate) { $signature.SignerCertificate.Subject } else { "(none)" }
        Write-Host "  PASS $($file.Name) (status=$($signature.Status), signer=$subject)"
    } catch {
        $message = $_.Exception.Message
        Write-Warning "  FAIL $($file.Name): $message"
        $failures += [pscustomobject]@{
            Name = $file.Name
            Error = $message
        }
    }
}

if ($pfxInfo.Temp) {
    Remove-Item -Path $pfxInfo.Path -Force -ErrorAction SilentlyContinue
}

if ($failures.Count -gt 0) {
    Write-Warning "Code signing: $($failures.Count) artifact(s) failed to sign."
    if ($FailOnError) {
        exit 1
    }
}

Write-Host "Code signing: step complete."
exit 0
