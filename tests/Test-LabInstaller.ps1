$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$installer = Join-Path $repo 'docs/lab/Install-Lab.ps1'
$passed = 0
function Sha([string]$p) { (Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash.ToLowerInvariant() }
function Assert([bool]$ok, [string]$msg) { if (-not $ok) { throw "ASSERT: $msg" } }
function New-File([string]$p, [string]$text) { New-Item -ItemType Directory -Force -Path (Split-Path -Parent $p) | Out-Null; Set-Content -LiteralPath $p -Value $text -NoNewline -Encoding ASCII }
function Rel([string]$root, [string]$path) { $base = New-Object System.Uri(($root.TrimEnd('\') + '\')); $target = New-Object System.Uri($path); return [Uri]::UnescapeDataString($base.MakeRelativeUri($target).ToString()).Replace('/','\') }
function Snapshot([string]$root) {
    if (-not (Test-Path -LiteralPath $root)) { return @() }
    @(Get-ChildItem -LiteralPath $root -Recurse -File -Force | ForEach-Object { [pscustomobject]@{ p=Rel $root $_.FullName; h=Sha $_.FullName; b=$_.Length } } | Sort-Object p | ConvertTo-Json -Compress)
}
function New-Package([string]$root, [string]$proxyText, [string]$compText) {
    New-Item -ItemType Directory -Force -Path (Join-Path $root 'payload') | Out-Null
    New-File (Join-Path $root 'payload/nvngx_dlssnr.dll') $proxyText
    New-File (Join-Path $root 'payload/dlssnr-companion.addon64') $compText
    $files = @()
    foreach($rel in @('payload/nvngx_dlssnr.dll','payload/dlssnr-companion.addon64')) {
        $p = Join-Path $root $rel
        $files += [ordered]@{ path=$rel; bytes=(Get-Item -LiteralPath $p).Length; sha256=Sha $p }
    }
    [ordered]@{ source_commit='test'; files=$files } | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $root 'BUILD-MANIFEST.json') -Encoding UTF8
}
function New-Game([string]$root, [string]$proxyText, [bool]$companion) {
    New-Item -ItemType Directory -Force -Path $root | Out-Null
    New-File (Join-Path $root 'nvngx_dlssnr.dll') $proxyText
    New-File (Join-Path $root 'nvngx_dlssnr_real.dll') 'real-model-preserve'
    New-File (Join-Path $root 'nvngx_dlssnr.ini') 'existing-ini-preserve'
    New-File (Join-Path $root 'renodx-dlss.addon64') 'shortfuse-preserve'
    if ($companion) { New-File (Join-Path $root 'dlssnr-companion.addon64') 'old-companion' }
}
function Invoke-Installer([string]$pkg, [string]$game, [switch]$Apply, [string]$RestoreBackup) {
    $installerArguments = @('-NoProfile','-ExecutionPolicy','Bypass','-File',(Join-Path $pkg 'Install-Lab.ps1'),'-GameDirectory',$game)
    if ($Apply) { $installerArguments += '-Apply' }
    if ($RestoreBackup) { $installerArguments += @('-RestoreBackup',$RestoreBackup) }
    $oldPreference = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try { $out = & powershell.exe @installerArguments 2>&1; $code = $LASTEXITCODE }
    finally { $ErrorActionPreference = $oldPreference }
    [pscustomobject]@{ ExitCode=$code; Output=($out -join "`n") }
}
function Invoke-InstallerWithCopyFailure([string]$pkg, [string]$game, [string]$restoreBackup, [string]$failName) {
    $wrapper = Join-Path $pkg 'fault-wrapper.ps1'
    $installerPath = Join-Path $pkg 'Install-Lab.ps1'
    @"
`$script:FailName = '$failName'
`$script:FailedOnce = `$false
function Copy-Item {
    param([string]`$LiteralPath, [string]`$Destination, [switch]`$Force, [Parameter(ValueFromRemainingArguments=`$true)]`$Rest)
    if (-not `$script:FailedOnce -and `$Destination -eq (Join-Path '$game' `$script:FailName)) {
        `$script:FailedOnce = `$true
        throw 'injected Copy-Item failure'
    }
    Microsoft.PowerShell.Management\Copy-Item -LiteralPath `$LiteralPath -Destination `$Destination -Force:`$Force
}
. '$installerPath' -GameDirectory '$game' -RestoreBackup '$restoreBackup' -Apply
"@ | Set-Content -LiteralPath $wrapper -Encoding UTF8
    $oldPreference = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try { $out = & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $wrapper 2>&1; $code = $LASTEXITCODE }
    finally { $ErrorActionPreference = $oldPreference }
    [pscustomobject]@{ ExitCode=$code; Output=($out -join "`n") }
}
function Invoke-InstallerInstallWithCopyFailure([string]$pkg, [string]$game, [string]$failName) {
    $wrapper = Join-Path $pkg 'fault-install-wrapper.ps1'
    $installerPath = Join-Path $pkg 'Install-Lab.ps1'
    @"
`$script:FailName = '$failName'
`$script:FailedOnce = `$false
function Copy-Item {
    param([string]`$LiteralPath, [string]`$Destination, [switch]`$Force, [Parameter(ValueFromRemainingArguments=`$true)]`$Rest)
    if (-not `$script:FailedOnce -and `$Destination -eq (Join-Path '$game' `$script:FailName)) {
        `$script:FailedOnce = `$true
        throw 'injected Copy-Item failure'
    }
    Microsoft.PowerShell.Management\Copy-Item -LiteralPath `$LiteralPath -Destination `$Destination -Force:`$Force
}
. '$installerPath' -GameDirectory '$game' -Apply
"@ | Set-Content -LiteralPath $wrapper -Encoding UTF8
    $oldPreference = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try { $out = & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $wrapper 2>&1; $code = $LASTEXITCODE }
    finally { $ErrorActionPreference = $oldPreference }
    [pscustomobject]@{ ExitCode=$code; Output=($out -join "`n") }
}
function Stage([string]$name, [bool]$companion=$true) {
    $root = Join-Path ([IO.Path]::GetTempPath()) ('cost-lab-installer-' + $name + '-' + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Force -Path $root | Out-Null
    $pkg = Join-Path $root 'pkg'; $game = Join-Path $root 'game'
    New-Package $pkg 'new-proxy' 'new-companion'
    Copy-Item -LiteralPath $installer -Destination (Join-Path $pkg 'Install-Lab.ps1')
    New-Game $game 'new-proxy' $companion
    [pscustomobject]@{ Root=$root; Package=$pkg; Game=$game }
}
function Test-PreviewNoChangesAnywhere {
    $s = Stage 'preview'
    $pkgBefore = Snapshot $s.Package; $gameBefore = Snapshot $s.Game
    $r = Invoke-Installer $s.Package $s.Game
    Assert ($r.ExitCode -eq 0) $r.Output
    Assert ((Snapshot $s.Package) -eq $pkgBefore) 'preview changed package tree'
    Assert ((Snapshot $s.Game) -eq $gameBefore) 'preview changed game tree'
    Assert (-not (Test-Path -LiteralPath (Join-Path $s.Package 'backups'))) 'preview created backups dir'
    $script:passed++
}
function Test-ApplyBacksUpOnlyProxyCompanionIni {
    $s = Stage 'apply'
    $r = Invoke-Installer $s.Package $s.Game -Apply
    Assert ($r.ExitCode -eq 0) $r.Output
    Assert ((Get-Content -Raw -LiteralPath (Join-Path $s.Game 'nvngx_dlssnr.dll')) -eq 'new-proxy') 'proxy not replaced'
    Assert ((Get-Content -Raw -LiteralPath (Join-Path $s.Game 'dlssnr-companion.addon64')) -eq 'new-companion') 'companion not replaced'
    Assert ((Get-Content -Raw -LiteralPath (Join-Path $s.Game 'nvngx_dlssnr.ini')) -eq 'existing-ini-preserve') 'INI changed'
    Assert ((Get-Content -Raw -LiteralPath (Join-Path $s.Game 'nvngx_dlssnr_real.dll')) -eq 'real-model-preserve') 'model changed'
    Assert ((Get-Content -Raw -LiteralPath (Join-Path $s.Game 'renodx-dlss.addon64')) -eq 'shortfuse-preserve') 'ShortFuse changed'
    $b = @(Get-ChildItem -LiteralPath (Join-Path $s.Package 'backups') -Directory)
    Assert ($b.Count -eq 1) 'expected one backup'
    Assert (Test-Path -LiteralPath (Join-Path $b[0].FullName 'nvngx_dlssnr.dll')) 'proxy not backed up'
    Assert (Test-Path -LiteralPath (Join-Path $b[0].FullName 'dlssnr-companion.addon64')) 'companion not backed up'
    Assert (Test-Path -LiteralPath (Join-Path $b[0].FullName 'nvngx_dlssnr.ini')) 'INI not backed up'
    Assert (-not (Test-Path -LiteralPath (Join-Path $b[0].FullName 'nvngx_dlssnr_real.dll'))) 'model was backed up'
    $script:passed++
}
function Test-RestorePreviewNoChanges {
    $s = Stage 'restorepreview'
    Invoke-Installer $s.Package $s.Game -Apply | Out-Null
    $backup = @(Get-ChildItem -LiteralPath (Join-Path $s.Package 'backups') -Directory)[0].FullName
    $pkgBefore = Snapshot $s.Package; $gameBefore = Snapshot $s.Game
    $r = Invoke-Installer $s.Package $s.Game -RestoreBackup $backup
    Assert ($r.ExitCode -eq 0) $r.Output
    Assert ((Snapshot $s.Package) -eq $pkgBefore) 'restore preview changed package tree'
    Assert ((Snapshot $s.Game) -eq $gameBefore) 'restore preview changed game tree'
    $script:passed++
}
function Test-RestoreLeavesChangedModelAndMovesIntroducedCompanion {
    $s = Stage 'restoremodel' $false
    Invoke-Installer $s.Package $s.Game -Apply | Out-Null
    $backup = @(Get-ChildItem -LiteralPath (Join-Path $s.Package 'backups') -Directory)[0].FullName
    New-File (Join-Path $s.Game 'nvngx_dlssnr.dll') 'mutated-proxy'
    New-File (Join-Path $s.Game 'nvngx_dlssnr_real.dll') 'changed-model-must-remain'
    New-File (Join-Path $s.Game 'dlssnr-companion.addon64') 'current-companion'
    $r = Invoke-Installer $s.Package $s.Game -Apply -RestoreBackup $backup
    Assert ($r.ExitCode -eq 0) $r.Output
    Assert ((Get-Content -Raw -LiteralPath (Join-Path $s.Game 'nvngx_dlssnr.dll')) -eq 'new-proxy') 'proxy restore failed'
    Assert ((Get-Content -Raw -LiteralPath (Join-Path $s.Game 'nvngx_dlssnr_real.dll')) -eq 'changed-model-must-remain') 'restore changed model'
    Assert (-not (Test-Path -LiteralPath (Join-Path $s.Game 'dlssnr-companion.addon64'))) 'introduced companion not moved away'
    $saved = @(Get-ChildItem -LiteralPath (Join-Path $s.Package 'backups') -Recurse -File -Filter 'current-dlssnr-companion.addon64')
    Assert ($saved.Count -eq 1) 'current companion not saved under held filename'
    $script:passed++
}
function Test-TamperedBackupPreflightZeroDestinationChange {
    $s = Stage 'tamperbackup'
    Invoke-Installer $s.Package $s.Game -Apply | Out-Null
    $backup = @(Get-ChildItem -LiteralPath (Join-Path $s.Package 'backups') -Directory)[0].FullName
    New-File (Join-Path $backup 'nvngx_dlssnr.ini') 'corrupt-ini'
    New-File (Join-Path $s.Game 'nvngx_dlssnr.dll') 'current-proxy'
    New-File (Join-Path $s.Game 'nvngx_dlssnr.ini') 'current-ini'
    $before = Snapshot $s.Game
    $r = Invoke-Installer $s.Package $s.Game -Apply -RestoreBackup $backup
    Assert ($r.ExitCode -ne 0) 'tampered backup accepted'
    Assert ((Snapshot $s.Game) -eq $before) 'tampered backup changed destination before full preflight'
    $script:passed++
}
function Test-BackupManifestMissingDuplicateReject {
    $s = Stage 'badmanifest'
    Invoke-Installer $s.Package $s.Game -Apply | Out-Null
    $backup = @(Get-ChildItem -LiteralPath (Join-Path $s.Package 'backups') -Directory)[0].FullName
    $bmPath = Join-Path $backup 'backup-manifest.json'
    $bm = Get-Content -LiteralPath $bmPath -Raw | ConvertFrom-Json
    $bm.files = @($bm.files | Where-Object { $_.path -ne 'nvngx_dlssnr.ini' })
    $bm | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $bmPath -Encoding UTF8
    $before = Snapshot $s.Game
    $r = Invoke-Installer $s.Package $s.Game -Apply -RestoreBackup $backup
    Assert ($r.ExitCode -ne 0) 'missing backup entry accepted'
    Assert ((Snapshot $s.Game) -eq $before) 'missing backup entry changed game'

    $s2 = Stage 'dupmanifest'
    Invoke-Installer $s2.Package $s2.Game -Apply | Out-Null
    $backup2 = @(Get-ChildItem -LiteralPath (Join-Path $s2.Package 'backups') -Directory)[0].FullName
    $bmPath2 = Join-Path $backup2 'backup-manifest.json'
    $bm2 = Get-Content -LiteralPath $bmPath2 -Raw | ConvertFrom-Json
    $dup = @($bm2.files) + @($bm2.files | Where-Object { $_.path -eq 'nvngx_dlssnr.dll' } | Select-Object -First 1)
    $bm2.files = $dup
    $bm2 | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $bmPath2 -Encoding UTF8
    $r2 = Invoke-Installer $s2.Package $s2.Game -Apply -RestoreBackup $backup2
    Assert ($r2.ExitCode -ne 0) 'duplicate backup entry accepted'
    $script:passed++
}
function Test-InstallRejections {
    $s = Stage 'reject'
    New-File (Join-Path $s.Game 'nvngx_dlssnr.dll') 'unknown-proxy'
    Assert ((Invoke-Installer $s.Package $s.Game -Apply).ExitCode -ne 0) 'unknown proxy accepted'
    $s2 = Stage 'tamper'
    New-File (Join-Path $s2.Package 'payload/nvngx_dlssnr.dll') 'tampered'
    Assert ((Invoke-Installer $s2.Package $s2.Game -Apply).ExitCode -ne 0) 'tampered payload accepted'
    $s3 = Stage 'dupmanifestpayload'
    $mPath = Join-Path $s3.Package 'BUILD-MANIFEST.json'
    $m = Get-Content -LiteralPath $mPath -Raw | ConvertFrom-Json
    $m.files = @($m.files) + @($m.files | Where-Object { $_.path -eq 'payload/nvngx_dlssnr.dll' } | Select-Object -First 1)
    $m | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $mPath -Encoding UTF8
    Assert ((Invoke-Installer $s3.Package $s3.Game -Apply).ExitCode -ne 0) 'duplicate payload manifest entry accepted'
    $script:passed++
}
function Test-RestoreCopyFailureRollsBackAllAttempted {
    $s = Stage 'restorefail'
    Invoke-Installer $s.Package $s.Game -Apply | Out-Null
    $backup = @(Get-ChildItem -LiteralPath (Join-Path $s.Package 'backups') -Directory)[0].FullName
    New-File (Join-Path $s.Game 'nvngx_dlssnr.dll') 'pre-restore-proxy'
    New-File (Join-Path $s.Game 'dlssnr-companion.addon64') 'pre-restore-companion'
    New-File (Join-Path $s.Game 'nvngx_dlssnr.ini') 'pre-restore-ini'
    $before = Snapshot $s.Game
    $r = Invoke-InstallerWithCopyFailure $s.Package $s.Game $backup 'dlssnr-companion.addon64'
    Assert ($r.ExitCode -ne 0) 'injected restore failure did not fail'
    Assert ($r.Output -match 'rollback completed') 'restore failure did not report rollback completion'
    Assert ((Snapshot $s.Game) -eq $before) 'restore failure did not restore pre-restore files'
    $script:passed++
}
function Test-InstallCopyFailureRollsBackAbsentCompanion {
    $s = Stage 'installfail' $false
    $before = Snapshot $s.Game
    $r = Invoke-InstallerInstallWithCopyFailure $s.Package $s.Game 'dlssnr-companion.addon64'
    Assert ($r.ExitCode -ne 0) 'injected install failure did not fail'
    Assert ($r.Output -match 'rollback completed') 'install failure did not report rollback completion'
    Assert ((Snapshot $s.Game) -eq $before) 'install failure did not restore absent-companion state'
    $stash = @(Get-ChildItem -LiteralPath (Join-Path $s.Package 'backups') -Recurse -Directory | Where-Object { $_.Name -like 'failed-current-*' })
    Assert ($stash.Count -ge 1) 'install failure did not retain failed current stash'
    $script:passed++
}
function Test-PackageInsideGameReject {
    $s = Stage 'inside'
    $inside = Join-Path $s.Game 'pkg'
    Copy-Item -LiteralPath $s.Package -Destination $inside -Recurse
    Assert ((Invoke-Installer $inside $s.Game -Apply).ExitCode -ne 0) 'package inside game accepted'
    $script:passed++
}
Test-PreviewNoChangesAnywhere
Test-ApplyBacksUpOnlyProxyCompanionIni
Test-RestorePreviewNoChanges
Test-RestoreLeavesChangedModelAndMovesIntroducedCompanion
Test-TamperedBackupPreflightZeroDestinationChange
Test-BackupManifestMissingDuplicateReject
Test-InstallRejections
Test-RestoreCopyFailureRollsBackAllAttempted
Test-InstallCopyFailureRollsBackAbsentCompanion
Test-PackageInsideGameReject
Write-Host "PASS: Test-LabInstaller tests=$passed"
