# Checks that the source and the built binaries stay inside the security
# boundary described in SECURITY.md: no credential store, no token, no network
# service, no loopback-only rule broken.
#
#   pwsh -File tools/security-scan.ps1
#
# Exits 0 when clean, 1 when something needs a human to look at it.

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$failures = @()
$checks = 0

function Strip-Comments([string]$text) {
    # Comments are allowed to name the things the code must not do.
    $text = [regex]::Replace($text, '/\*.*?\*/', '', 'Singleline')
    $text = [regex]::Replace($text, '(?m)//.*$', '')
    return $text
}

$forbidden = @(
    @{ Name = 'credential file';        Pattern = 'credentials\.json' },
    @{ Name = 'Anthropic API host';     Pattern = 'api\.anthropic\.com' },
    @{ Name = 'OAuth usage endpoint';   Pattern = 'oauth' },
    @{ Name = 'bearer token header';    Pattern = 'Bearer\s' },
    @{ Name = 'API key header';         Pattern = 'x-api-key' },
    @{ Name = 'API key variable';       Pattern = 'ANTHROPIC_API_KEY' },
    @{ Name = 'Credential Manager';     Pattern = 'Cred(Read|Write|Enumerate)' },
    @{ Name = 'DPAPI';                  Pattern = 'CryptUnprotectData' },
    @{ Name = 'WinHTTP';                Pattern = 'WinHttp' },
    @{ Name = 'WinINet';                Pattern = 'InternetOpen' },
    @{ Name = 'outbound helper';        Pattern = 'URLDownloadToFile|ShellExecute.*http' },
    @{ Name = 'wildcard socket bind';   Pattern = 'INADDR_ANY' },
    @{ Name = 'session transcript';     Pattern = 'transcript_path|\.jsonl' }
)

Write-Host 'Scanning source under src/ ...'
$sources = Get-ChildItem -Path (Join-Path $root 'src') -Recurse -Include *.cpp, *.h
foreach ($file in $sources) {
    $code = Strip-Comments (Get-Content -Raw -LiteralPath $file.FullName)
    foreach ($rule in $forbidden) {
        $checks++
        if ($code -match $rule.Pattern) {
            $failures += "$($file.Name): matches $($rule.Name) pattern '$($rule.Pattern)'"
        }
    }
}

Write-Host 'Checking the IPC socket is pinned to loopback ...'
$ipc = Get-Content -Raw -LiteralPath (Join-Path $root 'src/common/ipc.cpp')
$checks++
if ($ipc -notmatch 'htonl\(INADDR_LOOPBACK\)') {
    $failures += 'ipc.cpp does not bind the listener to INADDR_LOOPBACK'
}
$checks++
if ($ipc -notmatch 'ConstantTimeEquals') {
    $failures += 'ipc.cpp does not compare the token in constant time'
}

Write-Host 'Checking the data model holds only the four scalars ...'
$usage = Strip-Comments (Get-Content -Raw -LiteralPath (Join-Path $root 'src/common/usage.h'))
$checks++
if ($usage -match '(?i)token|secret|credential|apikey|session_id') {
    $failures += 'usage.h declares a field outside the four allowed scalars'
}

$binaries = @('ClaudeWeekUsageTray.exe', 'ClaudeUsageStatusLine.exe') |
    ForEach-Object { Join-Path $root "build/$_" } |
    Where-Object { Test-Path $_ }

if ($binaries.Count -eq 0) {
    Write-Host 'No built binaries found; run build.cmd to include them in the scan.'
} else {
    Write-Host 'Scanning built binaries ...'
    foreach ($binary in $binaries) {
        $bytes = [System.IO.File]::ReadAllBytes($binary)
        $ascii = [System.Text.Encoding]::ASCII.GetString($bytes)
        $utf16 = [System.Text.Encoding]::Unicode.GetString($bytes)
        foreach ($rule in $forbidden) {
            $checks++
            if ($ascii -match $rule.Pattern -or $utf16 -match $rule.Pattern) {
                $failures += "$(Split-Path -Leaf $binary): contains $($rule.Name) string"
            }
        }
    }
}

Write-Host ''
if ($failures.Count -eq 0) {
    Write-Host "Security scan passed. $checks checks, no findings."
    exit 0
}
Write-Host "Security scan found $($failures.Count) issue(s):"
$failures | ForEach-Object { Write-Host "  $_" }
exit 1
