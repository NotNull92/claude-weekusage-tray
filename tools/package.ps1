# Builds, tests, and packages a release.
#
#   pwsh -File tools/package.ps1
#
# Produces two files in dist/:
#   ClaudeWeekUsageTray-win-x64-v<version>.zip   the executable and uninstall.cmd
#   SHA256SUMS-v<version>.txt                    hashes of the executable and the zip
#
# The ZIP holds nothing else. No PDBs, no runtime redistributable, no secrets.

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root 'build'
$dist = Join-Path $root 'dist'
$version = '1.0.3'

Write-Host '== Building =='
& cmd /c "`"$(Join-Path $root 'build.cmd')`""
if ($LASTEXITCODE -ne 0) { throw 'build failed' }

Write-Host '== Self-test =='
& (Join-Path $build 'ClaudeWeekUsageTray.exe') --self-test
if ($LASTEXITCODE -ne 0) { throw 'self-test failed' }

Write-Host '== Security scan =='
& pwsh -NoProfile -File (Join-Path $root 'tools/security-scan.ps1')
if ($LASTEXITCODE -ne 0) { throw 'security scan failed' }

Write-Host '== Staging =='
$stage = Join-Path $dist 'stage'
if (Test-Path $stage) { Remove-Item -Recurse -Force -LiteralPath $stage }
New-Item -ItemType Directory -Force -Path $stage | Out-Null

$payload = @(
    (Join-Path $build 'ClaudeWeekUsageTray.exe'),
    (Join-Path $root 'uninstall.cmd')
)
foreach ($file in $payload) {
    if (-not (Test-Path $file)) { throw "missing $file" }
    Copy-Item -LiteralPath $file -Destination $stage
}

# Refuse to ship anything that should never be in a release.
$unexpected = Get-ChildItem -Path $stage -Recurse |
    Where-Object { $_.Name -notin @('ClaudeWeekUsageTray.exe', 'uninstall.cmd') }
if ($unexpected) { throw "unexpected files staged: $($unexpected.Name -join ', ')" }

Write-Host '== Zipping =='
$zip = Join-Path $dist "ClaudeWeekUsageTray-win-x64-v$version.zip"
if (Test-Path $zip) { Remove-Item -Force -LiteralPath $zip }
Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $zip

Write-Host '== SHA256SUMS =='
# The manifest ships beside the ZIP rather than inside it, so it can be used to
# check the download before anything is unpacked.
$lines = @(
    (Join-Path $stage 'ClaudeWeekUsageTray.exe'),
    $zip
) | ForEach-Object {
    $hash = (Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash.ToLower()
    "$hash  $(Split-Path -Leaf $_)"
}
$manifest = Join-Path $dist "SHA256SUMS-v$version.txt"
[System.IO.File]::WriteAllText($manifest, ($lines -join "`n") + "`n")
$lines | ForEach-Object { Write-Host "  $_" }

Write-Host ''
Write-Host "Release: $zip"
Write-Host "Manifest: $manifest"
Write-Host ''
Write-Host 'The executable is unsigned.'
