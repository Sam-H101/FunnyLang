# FunnyLang installer — NATIVE_PLAN.md N10 task 7.
#
#   irm https://raw.githubusercontent.com/Sam-H101/FunnyLang/master/install.ps1 | iex
#
# Downloads the Windows binary, verifies its SHA-256 against the release's own
# SHA256SUMS, drops it in %LOCALAPPDATA%\Programs\funny, and adds that to the
# user PATH. Refuses to proceed on a checksum mismatch — a corrupted or
# substituted download is exactly what a checksum is for, so there is no
# -Force.

#Requires -Version 5.1
$ErrorActionPreference = 'Stop'

$Repo = 'Sam-H101/FunnyLang'
$Version = if ($env:FUNNY_VERSION) { $env:FUNNY_VERSION } else { 'latest' }
$InstallDir = if ($env:FUNNY_INSTALL_DIR) { $env:FUNNY_INSTALL_DIR } else { "$env:LOCALAPPDATA\Programs\funny" }

function Fail($msg) {
    Write-Host "install.ps1: $msg" -ForegroundColor Red
    exit 1
}

# Only x86_64 is published today. Say so rather than downloading something
# that cannot run.
$arch = $env:PROCESSOR_ARCHITECTURE
if ($arch -ne 'AMD64') {
    Fail "no prebuilt binary for '$arch'. Build from source: https://github.com/$Repo"
}

$asset = 'funny-windows-x86_64.exe'
$stub = 'funnyrt-windows-x86_64.exe'
$base = if ($Version -eq 'latest') {
    "https://github.com/$Repo/releases/latest/download"
} else {
    "https://github.com/$Repo/releases/download/$Version"
}

$tmp = Join-Path ([System.IO.Path]::GetTempPath()) ("funny-install-" + [guid]::NewGuid().ToString('N').Substring(0, 8))
New-Item -ItemType Directory -Path $tmp | Out-Null
try {
    Write-Host "FunnyLang: fetching $asset ($Version)"
    # TLS 1.2 explicitly: Windows PowerShell 5.1 still defaults to something
    # older on unpatched machines, and GitHub refuses it.
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
    try {
        Invoke-WebRequest -Uri "$base/$asset" -OutFile "$tmp\$asset" -UseBasicParsing
        Invoke-WebRequest -Uri "$base/SHA256SUMS" -OutFile "$tmp\SHA256SUMS" -UseBasicParsing
    } catch {
        Fail "couldn't download from $base -- $($_.Exception.Message)"
    }

    $line = Get-Content "$tmp\SHA256SUMS" | Where-Object { $_ -match "\s$([regex]::Escape($asset))$" }
    if (-not $line) { Fail "SHA256SUMS has no entry for $asset. Refusing to install." }
    $expected = ($line -split '\s+')[0].ToLower()
    $actual = (Get-FileHash "$tmp\$asset" -Algorithm SHA256).Hash.ToLower()
    if ($expected -ne $actual) {
        Write-Host "  expected $expected"
        Write-Host "  got      $actual"
        Fail "checksum mismatch. Refusing to install."
    }
    Write-Host "  checksum ok"

    # The stub is optional: only `funny yeet` needs it, so failing to get it
    # is a warning rather than a failed install.
    $haveStub = $false
    try {
        Invoke-WebRequest -Uri "$base/$stub" -OutFile "$tmp\$stub" -UseBasicParsing
        $stubLine = Get-Content "$tmp\SHA256SUMS" | Where-Object { $_ -match "\s$([regex]::Escape($stub))$" }
        if ($stubLine -and ($stubLine -split '\s+')[0].ToLower() -eq (Get-FileHash "$tmp\$stub" -Algorithm SHA256).Hash.ToLower()) {
            $haveStub = $true
        } else {
            Write-Host "  warning: the yeet stub failed its checksum; skipping it. 'funny yeet' will not work."
        }
    } catch {
        Write-Host "  warning: couldn't fetch the yeet stub; 'funny yeet' will not work."
    }

    New-Item -ItemType Directory -Path $InstallDir -Force | Out-Null
    Move-Item "$tmp\$asset" "$InstallDir\funny.exe" -Force
    if ($haveStub) { Move-Item "$tmp\$stub" "$InstallDir\funnyrt.exe" -Force }

    Write-Host ""
    Write-Host "installed: $InstallDir\funny.exe"
    & "$InstallDir\funny.exe" --version

    # User PATH, not machine PATH: no elevation, and nothing that needs
    # undoing by an administrator later.
    $userPath = [Environment]::GetEnvironmentVariable('Path', 'User')
    if (($userPath -split ';') -notcontains $InstallDir) {
        [Environment]::SetEnvironmentVariable('Path', "$userPath;$InstallDir", 'User')
        Write-Host ""
        Write-Host "added $InstallDir to your user PATH. Open a new terminal for it to take effect."
    }

    Write-Host ""
    Write-Host "try:  funny vibe"
} finally {
    Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue
}
