param(
    [Parameter(Mandatory=$true)][string]$GameDirectory,
    [switch]$Apply,
    [string]$RestoreBackup
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0
$KnownProxyHashes = @(
    '9f3fc1a5b7d0a0bb301fbf672383527bb398b92362551b7df142905b0974290b',
    'fa2deceb738672d2ffd08a0adb7fde909c8265093c2177da8d330df7737d9fd8'
)
$PayloadFiles = @('nvngx_dlssnr.dll','dlssnr-companion.addon64')
$BackupFiles = @('nvngx_dlssnr.dll','dlssnr-companion.addon64','nvngx_dlssnr.ini')
function FullPath([string]$Path) { [IO.Path]::GetFullPath($Path) }
function HashOf([string]$Path) { (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant() }
function Assert-NoReparseUpchain([string]$Path) {
    $full = FullPath $Path
    $probe = if (Test-Path -LiteralPath $full) { $full } else { Split-Path -Parent $full }
    if (-not $probe) { throw "Cannot inspect path: $Path" }
    $cur = Get-Item -LiteralPath $probe -Force
    while ($cur) {
        if (($cur.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw "Refusing reparse point in path: $($cur.FullName)" }
        $parent = Split-Path -Parent $cur.FullName
        if (-not $parent -or $parent -eq $cur.FullName) { break }
        $cur = Get-Item -LiteralPath $parent -Force
    }
}
function Assert-Writable([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { return }
    $s = $null
    try { $s = [IO.File]::Open($Path, [IO.FileMode]::Open, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None) }
    catch { throw "File is locked or not writable: $Path" }
    finally { if ($s) { $s.Close() } }
}
function Write-JsonFile([string]$Path, $Object) { $Object | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $Path -Encoding UTF8 }
function Load-Manifest([string]$PackageRoot) {
    $path = Join-Path $PackageRoot 'BUILD-MANIFEST.json'
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Missing package manifest: $path" }
    $m = Get-Content -LiteralPath $path -Raw | ConvertFrom-Json
    if (-not $m.files) { throw 'BUILD-MANIFEST.json missing files[]' }
    return $m
}
function Get-OnlyManifestEntry($Manifest, [string]$RelPath) {
    $norm = $RelPath.Replace('\\','/')
    $matches = @($Manifest.files | Where-Object { ([string]$_.path).Replace('\\','/') -eq $norm })
    if ($matches.Count -eq 0) { throw "Manifest missing fixed payload: $norm" }
    if ($matches.Count -ne 1) { throw "Manifest contains duplicate payload entry: $norm" }
    $e = $matches[0]
    if ([string]$e.path -ne $norm) { throw "Manifest path must use fixed relative form: $norm" }
    if (-not ([string]$e.sha256 -match '^[0-9a-fA-F]{64}$')) { throw "Manifest has invalid sha256: $norm" }
    return $e
}
function Assert-Payloads([string]$PackageRoot, $Manifest) {
    foreach ($name in $PayloadFiles) {
        $rel = "payload/$name"
        $entry = Get-OnlyManifestEntry $Manifest $rel
        $payload = Join-Path (Join-Path $PackageRoot 'payload') $name
        if (-not (Test-Path -LiteralPath $payload -PathType Leaf)) { throw "Missing payload: $payload" }
        Assert-NoReparseUpchain $payload
        $item = Get-Item -LiteralPath $payload
        if ([int64]$entry.bytes -ne $item.Length) { throw "Payload byte mismatch: $rel" }
        if ((HashOf $payload) -ne ([string]$entry.sha256).ToLowerInvariant()) { throw "Payload hash mismatch: $rel" }
    }
}
function New-BackupPath([string]$PackageRoot) {
    $root = Join-Path $PackageRoot 'backups'
    $id = (Get-Date).ToUniversalTime().ToString('yyyyMMdd-HHmmss') + '-' + [guid]::NewGuid().ToString('N').Substring(0,8)
    return Join-Path $root $id
}
function New-Backup([string]$Dir, [string]$GameRoot, [string[]]$Names) {
    Assert-NoReparseUpchain (Split-Path -Parent $Dir)
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Dir) | Out-Null
    New-Item -ItemType Directory -Path $Dir | Out-Null
    $files = @()
    foreach ($n in $Names) {
        $src = Join-Path $GameRoot $n
        Assert-NoReparseUpchain $src
        if (Test-Path -LiteralPath $src -PathType Leaf) {
            Copy-Item -LiteralPath $src -Destination (Join-Path $Dir $n)
            $it = Get-Item -LiteralPath $src
            $files += [ordered]@{ path=$n; existed=$true; bytes=$it.Length; sha256=HashOf $src }
        } else { $files += [ordered]@{ path=$n; existed=$false; bytes=0; sha256='' } }
    }
    Write-JsonFile (Join-Path $Dir 'backup-manifest.json') ([ordered]@{ game_directory=(FullPath $GameRoot); created_utc=(Get-Date).ToUniversalTime().ToString('o'); files=$files })
    return $Dir
}
function Read-BackupManifest([string]$BackupDir, [string]$GameRoot) {
    Assert-NoReparseUpchain $BackupDir
    $path = Join-Path $BackupDir 'backup-manifest.json'
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw 'Restore backup missing backup-manifest.json' }
    $bm = Get-Content -LiteralPath $path -Raw | ConvertFrom-Json
    if ((FullPath $bm.game_directory) -ne $GameRoot) { throw 'RestoreBackup game directory does not match -GameDirectory.' }
    $entries = @($bm.files)
    if ($entries.Count -ne $BackupFiles.Count) { throw 'Restore backup manifest has wrong file count.' }
    foreach ($n in $BackupFiles) {
        $matches = @($entries | Where-Object { [string]$_.path -eq $n })
        if ($matches.Count -ne 1) { throw "Restore backup manifest missing or duplicates: $n" }
        $e = $matches[0]
        if ($e.existed) {
            if (-not ([string]$e.sha256 -match '^[0-9a-fA-F]{64}$')) { throw "Backup has invalid sha256: $n" }
            $src = Join-Path $BackupDir $n
            if (-not (Test-Path -LiteralPath $src -PathType Leaf)) { throw "Backup file missing: $n" }
            Assert-NoReparseUpchain $src
            $item = Get-Item -LiteralPath $src
            if ([int64]$e.bytes -ne $item.Length) { throw "Backup byte mismatch: $n" }
            if ((HashOf $src) -ne ([string]$e.sha256).ToLowerInvariant()) { throw "Backup hash mismatch: $n" }
        }
    }
    return $entries
}
function Move-CurrentCompanionAside([string]$GameRoot, [string]$CurrentSave) {
    $cur = Join-Path $GameRoot 'dlssnr-companion.addon64'
    if (-not (Test-Path -LiteralPath $cur -PathType Leaf)) { return }
    New-Item -ItemType Directory -Force -Path $CurrentSave | Out-Null
    Move-Item -LiteralPath $cur -Destination (Join-Path $CurrentSave 'current-dlssnr-companion.addon64')
}
function Show-Plan([string]$PackageRoot, [string]$GameRoot, [string]$BackupDir, [bool]$Restore) {
    if ($Restore) { Write-Host 'Restore plan:' } else { Write-Host 'Install plan:' }
    Write-Host "  Package: $PackageRoot"
    Write-Host "  Game:    $GameRoot"
    Write-Host "  Backup:  $BackupDir"
    if ($Restore) { Write-Host '  Restore: nvngx_dlssnr.dll, dlssnr-companion.addon64 if originally present, and nvngx_dlssnr.ini. Never write nvngx_dlssnr_real.dll.' }
    else { Write-Host '  Replace: payload/nvngx_dlssnr.dll and payload/dlssnr-companion.addon64 only. Preserve existing INI, model, ShortFuse, and ReShade.' }
}
$PackageRoot = FullPath $PSScriptRoot
$GameRoot = FullPath $GameDirectory
Assert-NoReparseUpchain $PackageRoot
Assert-NoReparseUpchain $GameRoot
if (-not (Test-Path -LiteralPath $GameRoot -PathType Container)) { throw "GameDirectory not found: $GameRoot" }
if ($PackageRoot.StartsWith($GameRoot.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase) -or $PackageRoot -eq $GameRoot) { throw 'Refusing package located inside GameDirectory.' }
$manifest = Load-Manifest $PackageRoot
Assert-Payloads $PackageRoot $manifest
$payloadProxyHash = (Get-OnlyManifestEntry $manifest 'payload/nvngx_dlssnr.dll').sha256.ToLowerInvariant()
$allowed = @($KnownProxyHashes + $payloadProxyHash) | Select-Object -Unique
if ($RestoreBackup) {
    $backup = FullPath $RestoreBackup
    if (-not (Test-Path -LiteralPath $backup -PathType Container)) { throw "RestoreBackup not found: $backup" }
    $entries = Read-BackupManifest $backup $GameRoot
    foreach ($n in @('nvngx_dlssnr.dll','nvngx_dlssnr.ini')) { Assert-Writable (Join-Path $GameRoot $n) }
    Assert-Writable (Join-Path $GameRoot 'dlssnr-companion.addon64')
    $currentSave = New-BackupPath $PackageRoot
    Show-Plan $PackageRoot $GameRoot $backup $true
    if (-not $Apply) { Write-Host 'Preview only. Re-run with -Apply to restore.'; exit 0 }
    New-Backup $currentSave $GameRoot $BackupFiles | Out-Null
    $attempted = @()
    $originalFailure = $null
    try {
        foreach ($e in $entries) {
            $name = [string]$e.path
            $dst = Join-Path $GameRoot $name
            Assert-NoReparseUpchain $dst
            $attempted += $name
            if ($e.existed) {
                $src = Join-Path $backup $name
                Copy-Item -LiteralPath $src -Destination $dst -Force
                if ((HashOf $dst) -ne ([string]$e.sha256).ToLowerInvariant()) { throw "Restore destination hash verification failed: $name" }
            } elseif ($name -eq 'dlssnr-companion.addon64') {
                Move-CurrentCompanionAside $GameRoot $currentSave
            }
        }
    } catch {
        $originalFailure = $_.Exception.Message
        $rollbackFailures = @()
        $stash = Join-Path $currentSave ('failed-restore-current-' + [guid]::NewGuid().ToString('N').Substring(0,8))
        New-Item -ItemType Directory -Force -Path $stash | Out-Null
        foreach ($name in $attempted) {
            try {
                $dst = Join-Path $GameRoot $name
                $saved = Join-Path $currentSave $name
                $savedHeld = Join-Path $currentSave 'current-dlssnr-companion.addon64'
                if (Test-Path -LiteralPath $dst -PathType Leaf) { Move-Item -LiteralPath $dst -Destination (Join-Path $stash $name) -Force }
                if (Test-Path -LiteralPath $saved -PathType Leaf) {
                    Copy-Item -LiteralPath $saved -Destination $dst -Force
                    if ((HashOf $dst) -ne (HashOf $saved)) { throw "Rollback hash verification failed: $name" }
                } elseif ($name -eq 'dlssnr-companion.addon64' -and (Test-Path -LiteralPath $savedHeld -PathType Leaf)) {
                    Move-Item -LiteralPath $savedHeld -Destination $dst -Force
                }
            } catch { $rollbackFailures += ("${name}: " + $_.Exception.Message) }
        }
        if ($rollbackFailures.Count -gt 0) { throw ("Restore failed: $originalFailure; rollback failures: " + ($rollbackFailures -join ' | ')) }
        throw "Restore failed and rollback completed: $originalFailure"
    }
    Write-Host "Restored from backup: $backup"
    exit 0
}
foreach ($n in @('nvngx_dlssnr.dll','nvngx_dlssnr_real.dll','nvngx_dlssnr.ini')) {
    $p = Join-Path $GameRoot $n
    if (-not (Test-Path -LiteralPath $p -PathType Leaf)) { throw "Required existing Cost Scaler file missing: $n" }
    Assert-NoReparseUpchain $p
}
foreach ($n in $PayloadFiles) { Assert-NoReparseUpchain (Join-Path $GameRoot $n) }
$existingProxyHash = HashOf (Join-Path $GameRoot 'nvngx_dlssnr.dll')
if ($existingProxyHash -notin $allowed) { throw "Refusing unknown existing nvngx_dlssnr.dll hash: $existingProxyHash" }
foreach ($n in $PayloadFiles) { Assert-Writable (Join-Path $GameRoot $n) }
$backupDir = New-BackupPath $PackageRoot
Show-Plan $PackageRoot $GameRoot $backupDir $false
if (-not $Apply) { Write-Host 'Preview only. Re-run with -Apply to install.'; exit 0 }
New-Backup $backupDir $GameRoot $BackupFiles | Out-Null
$attempted = @()
try {
    foreach ($n in $PayloadFiles) {
        $src = Join-Path (Join-Path $PackageRoot 'payload') $n
        $dst = Join-Path $GameRoot $n
        $attempted += $n
        Copy-Item -LiteralPath $src -Destination $dst -Force
        if ((HashOf $dst) -ne (HashOf $src)) { throw "Destination hash verification failed: $n" }
    }
    Write-Host "Installed lab binaries. Backup: $backupDir"
} catch {
    $originalFailure = $_.Exception.Message
    $rollbackFailures = @()
    $stash = Join-Path $backupDir ('failed-current-' + [guid]::NewGuid().ToString('N').Substring(0,8))
    New-Item -ItemType Directory -Force -Path $stash | Out-Null
    foreach ($n in $attempted) {
        try {
            $dst = Join-Path $GameRoot $n
            $bak = Join-Path $backupDir $n
            if (Test-Path -LiteralPath $dst -PathType Leaf) { Move-Item -LiteralPath $dst -Destination (Join-Path $stash $n) -Force }
            if (Test-Path -LiteralPath $bak -PathType Leaf) {
                Copy-Item -LiteralPath $bak -Destination $dst -Force
                if ((HashOf $dst) -ne (HashOf $bak)) { throw "Rollback hash verification failed: $n" }
            }
        } catch { $rollbackFailures += ("${n}: " + $_.Exception.Message) }
    }
    if ($rollbackFailures.Count -gt 0) { throw ("Install failed: $originalFailure; rollback failures: " + ($rollbackFailures -join ' | ')) }
    throw "Install failed and rollback completed: $originalFailure"
}
