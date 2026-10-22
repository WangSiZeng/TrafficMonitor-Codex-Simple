$ErrorActionPreference = 'Stop'
$repository = Split-Path -Parent $PSScriptRoot
Push-Location $repository
try {
    $files = @(git ls-files --cached)
    if (-not $files.Count) { throw 'Stage the intended release files before running the check.' }
    $forbidden = $files | Where-Object {
        $_ -match '(^|/)(auth\.json|\.env(?:\..*)?|\.codex|sessions|\.local-backup|build|dist)(/|$)' -or
        $_ -match '\.(dll|exe|obj|pdb|lib|exp|jsonl|log|zip)$' -or
        ($_ -match '\.ini$' -and $_ -ne 'CodexUsage.example.ini')
    }
    if ($forbidden) { throw "Forbidden release files: $($forbidden -join ', ')" }
    $secretPatterns = @(
        '-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----',
        '\bgh[pousr]_[A-Za-z0-9_]{20,}\b',
        '\bgithub_pat_[A-Za-z0-9_]{20,}\b',
        '\bsk-[A-Za-z0-9_-]{20,}\b',
        '\beyJ[A-Za-z0-9_-]+\.[A-Za-z0-9_-]+\.[A-Za-z0-9_-]+\b'
    )
    $personalValues = @($env:USERPROFILE, $env:USERNAME, $repository) | Where-Object { $_ -and $_.Length -ge 4 }
    foreach ($file in $files) {
        $text = (git show ":$file") -join "`n"
        if ($LASTEXITCODE -ne 0) { throw "Unable to read staged file $file" }
        foreach ($pattern in $secretPatterns) {
            if ($text -match $pattern) { throw "Potential credential found in $file (value suppressed)." }
        }
        # Public third-party copyright attributions are intentionally retained.
        foreach ($personal in $personalValues) {
            if ($text.Contains($personal) -or $text.Contains($personal.Replace('\', '\\'))) {
                throw "Local personal identifier found in $file (value suppressed)."
            }
        }
    }
    $binary = Join-Path $repository 'build/CodexUsage.dll'
    if (Test-Path -LiteralPath $binary) {
        $bytes = [IO.File]::ReadAllBytes($binary)
        foreach ($encoding in @([Text.Encoding]::UTF8, [Text.Encoding]::Unicode)) {
            $text = $encoding.GetString($bytes)
            foreach ($personal in $personalValues) {
                if ($text.Contains($personal)) { throw 'Local personal identifier found in DLL (value suppressed).' }
            }
            foreach ($pattern in $secretPatterns) {
                if ($text -match $pattern) { throw 'Potential credential found in DLL (value suppressed).' }
            }
        }
    }
    $identity = @(git config --local user.name; git config --local user.email)
    if ($identity[0] -ne 'CodexUsage contributors' -or $identity[1] -ne 'noreply@example.com') {
        throw 'Release commit must use the configured non-personal identity.'
    }
    Write-Output "Release privacy check passed: $($files.Count) staged files; DLL checked when present."
} finally { Pop-Location }
