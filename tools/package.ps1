# Builds, tests, and packages a release ZIP with a SHA256SUMS manifest.
#
#   pwsh -File tools/package.ps1
#
# The ZIP contains the two executables, the cleanup command, the licence, and
# the documentation. It contains no PDBs, no runtime redistributable, and no
# secrets.

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root 'build'
$dist = Join-Path $root 'dist'
$version = '1.0.0'
$name = "ClaudeWeekUsageTray-v$version-win-x64"

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
$stage = Join-Path $dist $name
if (Test-Path $stage) { Remove-Item -Recurse -Force -LiteralPath $stage }
New-Item -ItemType Directory -Force -Path $stage | Out-Null

$payload = @(
    (Join-Path $build 'ClaudeWeekUsageTray.exe'),
    (Join-Path $build 'ClaudeUsageStatusLine.exe'),
    (Join-Path $root 'tools/cleanup-tray-icons.cmd'),
    (Join-Path $root 'README.md'),
    (Join-Path $root 'README.ko.md'),
    (Join-Path $root 'SECURITY.md'),
    (Join-Path $root 'LICENSE')
)
foreach ($file in $payload) {
    if (-not (Test-Path $file)) { throw "missing $file" }
    Copy-Item -LiteralPath $file -Destination $stage
}

# Refuse to ship anything that should never be in a release.
$forbidden = Get-ChildItem -Path $stage -Recurse -Include *.pdb, *.ilk, *.obj, *.json
if ($forbidden) { throw "unexpected files staged: $($forbidden.Name -join ', ')" }

Write-Host '== SHA256SUMS =='
$lines = Get-ChildItem -Path $stage -File | Sort-Object Name | ForEach-Object {
    $hash = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLower()
    "$hash  $($_.Name)"
}
$manifest = Join-Path $stage 'SHA256SUMS'
[System.IO.File]::WriteAllText($manifest, ($lines -join "`n") + "`n")
$lines | ForEach-Object { Write-Host "  $_" }

Write-Host '== Zipping =='
$zip = Join-Path $dist "$name.zip"
if (Test-Path $zip) { Remove-Item -Force -LiteralPath $zip }
Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $zip

$zipHash = (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash.ToLower()
Write-Host ''
Write-Host "Release: $zip"
Write-Host "SHA256:  $zipHash"
Write-Host ''
Write-Host 'Both executables are unsigned.'
