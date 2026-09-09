param([switch]$CpuOnly, [switch]$NoPackage)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$out = Join-Path $repoRoot 'out'
New-Item -ItemType Directory -Path $out -Force | Out-Null

function Invoke-GitLab([string[]]$Arguments) {
    $value = & git -c "safe.directory=$repoRoot" -C $repoRoot @Arguments
    if ($LASTEXITCODE -ne 0) { throw 'Git command failed.' }
    return $value
}
function Get-Bytecode([string]$Header, [string]$Symbol) {
    $text = Get-Content -LiteralPath $Header -Raw
    $match = [regex]::Match($text, [regex]::Escape($Symbol) + '\[\]\s*=\s*\{([^}]+)\}', 'Singleline')
    if (-not $match.Success) { throw "Shader array missing: $Symbol" }
    [byte[]]$bytes = @([regex]::Matches($match.Groups[1].Value, '\d+') | ForEach-Object { [byte]$_.Value })
    return ,$bytes
}
function Compile-Lab([string]$Name, [string]$Source, [string[]]$Extra, [string[]]$Libraries, [switch]$Dll) {
    $obj = Join-Path $out ($Name + '.obj')
    $target = Join-Path $out $Name
    $flags = @('/nologo', '/O2', '/Oi', '/MT', '/EHsc', '/std:c++17', '/D', 'NDEBUG', '/W4')
    if ($Dll) { $flags += @('/GL', '/D', '_WINDOWS', '/D', '_USRDLL') }
    & cl.exe @flags @Extra /c (Join-Path $repoRoot $Source) "/Fo$obj" 2>&1 | Tee-Object -FilePath (Join-Path $out ($Name + '-compile.log'))
    if ($LASTEXITCODE -ne 0) { throw "Compilation failed: $Name" }
    $linkFlags = @('/nologo', "/OUT:$target", $obj, '/OPT:REF', '/OPT:ICF')
    if ($Dll) { $linkFlags += @('/DLL', '/LTCG') }
    & link.exe @linkFlags @Libraries 2>&1 | Tee-Object -FilePath (Join-Path $out ($Name + '-link.log'))
    if ($LASTEXITCODE -ne 0) { throw "Link failed: $Name" }
    if (-not (Test-Path -LiteralPath $target -PathType Leaf)) { throw "Missing build output: $Name" }
}

Push-Location $repoRoot
try {
    $commit = (Invoke-GitLab @('rev-parse', 'HEAD')).Trim()
    $status = @(Invoke-GitLab @('status', '--porcelain=v1', '--untracked-files=all'))
    if (-not $NoPackage) {
        if ($CpuOnly) { throw 'Packaging requires the full software shader tests; use -NoPackage for CPU-only development.' }
        if ($status.Count) { throw "Commit source changes before packaging: $($status -join ', ')" }
    }

    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    $vsInstall = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if ($LASTEXITCODE -ne 0 -or -not $vsInstall) { throw 'MSVC C++ Build Tools are required.' }
    $vcvars = Join-Path $vsInstall 'VC/Auxiliary/Build/vcvars64.bat'
    $vsEnvironment = & $env:ComSpec /d /s /c ('"{0}" >nul && set' -f $vcvars)
    if ($LASTEXITCODE -ne 0) { throw 'MSVC environment initialization failed.' }
    # Some parent environments contain both PATH and Path. Keep the developer
    # shell value, not a later duplicate of the pre-initialization search path.
    $developerPath = @($vsEnvironment | Where-Object { $_ -match '^(?i:path)=' -and $_ -match 'VC\\Tools\\MSVC' })
    if (-not $developerPath.Count) { throw 'Developer shell did not supply an MSVC search path.' }
    foreach ($line in $vsEnvironment) {
        if ($line -match '^([^=]+)=(.*)$') {
            if ($Matches[1] -ine 'Path') { [Environment]::SetEnvironmentVariable($Matches[1], $Matches[2], 'Process') }
        }
    }
    $env:PATH = $developerPath[0].Substring($developerPath[0].IndexOf('=') + 1)
    $env:Path = $env:PATH
    if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) { throw 'MSVC environment initialization failed.' }

    $sdkBin = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits/10/bin'
    $fxc = Join-Path $sdkBin '10.0.19041.0/x64/fxc.exe'
    if (-not (Test-Path -LiteralPath $fxc)) {
        $candidate = @(Get-ChildItem -LiteralPath $sdkBin -Filter fxc.exe -File -Recurse | Where-Object { $_.Directory.Name -eq 'x64' } | Sort-Object FullName -Descending)
        if (-not $candidate.Count) { throw 'Windows SDK FXC compiler is required.' }
        $fxc = $candidate[0].FullName
    }
    $shaderChecks = @()
    foreach ($shader in @(
        @{ entry = 'CS_Downsample'; header = 'Downsample_Shader.h'; symbol = 'g_DownsampleShader' },
        @{ entry = 'CS_Resolve'; header = 'Resolve_Shader.h'; symbol = 'g_ResolveShader' }
    )) {
        $binary = Join-Path $out ($shader.entry + '.cso')
        & $fxc /nologo /T cs_5_0 /E $shader.entry /Fo $binary shaders.hlsl
        if ($LASTEXITCODE -ne 0) { throw "Shader compilation failed: $($shader.entry)" }
        $checkedIn = Get-Bytecode (Join-Path $repoRoot $shader.header) $shader.symbol
        $compiled = [IO.File]::ReadAllBytes($binary)
        if ([Convert]::ToBase64String($checkedIn) -ne [Convert]::ToBase64String($compiled)) {
            throw "Stale shader header or different compiler bytecode: $($shader.header). Regenerate and commit deliberately."
        }
        $shaderChecks += [ordered]@{ entry = $shader.entry; bytes = $compiled.Length; sha256 = (Get-FileHash -LiteralPath $binary).Hash.ToLowerInvariant(); matched_header = $true }
    }

    Compile-Lab 'nvngx_dlssnr.dll' 'proxy_main.cpp' @() @('d3d12.lib', 'dxgi.lib', 'kernel32.lib', 'user32.lib') -Dll
    Compile-Lab 'dlssnr-companion.addon64' 'companion/companion_main.cpp' @('/I', (Join-Path $repoRoot 'companion/include')) @('kernel32.lib', 'user32.lib', 'shell32.lib') -Dll
    $results = @()
    foreach ($test in @('nr_routing_test', 'lab_presets_test')) {
        Compile-Lab ($test + '.exe') ("tests/$test.cpp") @('/WX') @()
        & (Join-Path $out ($test + '.exe')) 2>&1 | Tee-Object -FilePath (Join-Path $out ($test + '-test.log'))
        if ($LASTEXITCODE -ne 0) { throw "CPU test failed: $test" }
        $results += [ordered]@{ name = $test; result = 'passed' }
    }
    & ./tests/Test-LabInstaller.ps1
    if (-not $?) { throw 'Installer test failed.' }
    $results += [ordered]@{ name = 'installer'; result = 'passed' }
    Compile-Lab 'cost_scaler_shader_warp.exe' 'tests/cost_scaler_shader_warp.cpp' @() @('d3d11.lib', 'd3dcompiler.lib', 'dxgi.lib', 'dxguid.lib')
    if (-not $CpuOnly) {
        & (Join-Path $out 'cost_scaler_shader_warp.exe') 2>&1 | Tee-Object -FilePath (Join-Path $out 'shader-warp-test.log')
        if ($LASTEXITCODE -ne 0) { throw 'Software shader regression test failed.' }
        $results += [ordered]@{ name = 'shader_warp'; result = 'passed' }
    }
    $record = [ordered]@{
        source_commit = $commit; source_dirty = $status.Count -ne 0; built_utc = (Get-Date).ToUniversalTime().ToString('o')
        msvc_tools = $env:VCToolsVersion; windows_sdk = $env:WindowsSDKVersion; fxc = $fxc
        suites = $results; shaders = $shaderChecks; warp_executed = -not $CpuOnly
        nr_model_loaded = $false; gaming_gpu_validated = $false
    }
    $record | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $out 'TEST-RESULTS.json') -Encoding utf8
    if ($NoPackage) { return }

    $stage = Join-Path $out ('cost-scaler-lab-' + $commit.Substring(0, 12))
    if (Test-Path -LiteralPath $stage) { throw 'Package staging directory already exists; preserve it and choose a fresh checkout/output.' }
    New-Item -ItemType Directory -Path (Join-Path $stage 'payload') | Out-Null
    foreach ($name in @('nvngx_dlssnr.dll', 'dlssnr-companion.addon64')) { Copy-Item -LiteralPath (Join-Path $out $name) -Destination (Join-Path $stage "payload/$name") }
    foreach ($doc in Get-ChildItem -LiteralPath (Join-Path $repoRoot 'docs/lab') -File) { Copy-Item -LiteralPath $doc.FullName -Destination (Join-Path $stage $doc.Name) }
    Copy-Item -LiteralPath (Join-Path $repoRoot 'LICENSE') -Destination (Join-Path $stage 'LICENSE')
    Copy-Item -LiteralPath (Join-Path $repoRoot 'nvngx_dlssnr.ini') -Destination (Join-Path $stage 'reference-defaults.ini')
    Copy-Item -LiteralPath (Join-Path $out 'TEST-RESULTS.json') -Destination (Join-Path $stage 'TEST-RESULTS.json')
    $files = @(Get-ChildItem -LiteralPath $stage -File -Recurse | ForEach-Object {
        [ordered]@{ path = $_.FullName.Substring($stage.Length + 1).Replace('\', '/'); bytes = $_.Length; sha256 = (Get-FileHash -LiteralPath $_.FullName).Hash.ToLowerInvariant() }
    })
    [ordered]@{
        name = 'Cost Scaler Lab 2026-09-09'; source_commit = $commit
        source_url = "https://github.com/paulhshort/DLSSNR-Cost-Scaler/tree/$commit"
        base_commit = '9bb03663d690b84ec00cdb55fb3a1a04dd881e02'
        supersampling_fix_commit = '7dc3dac672b1ad280292144b4edbe1833c9ea374'
        model_policy = 'No model, weights, ShortFuse, or ReShade runtime included. Only MIT source-built proxy and companion.'
        runtime_validation = 'CPU and WARP only. Game, RTX performance, and neural inference are untested.'
        files = $files
    } | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $stage 'BUILD-MANIFEST.json') -Encoding utf8
    Compress-Archive -Path (Join-Path $stage '*') -DestinationPath ($stage + '.zip') -CompressionLevel Optimal
    (Get-FileHash -LiteralPath ($stage + '.zip')).Hash.ToLowerInvariant() | Set-Content -LiteralPath ($stage + '.zip.sha256') -Encoding ascii
    Write-Output "Package: $stage.zip"
} finally { Pop-Location }
