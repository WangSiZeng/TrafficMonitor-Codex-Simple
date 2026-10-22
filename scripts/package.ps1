$ErrorActionPreference = 'Stop'
$repository = Split-Path -Parent $PSScriptRoot
$version = '1.2.0'
$distribution = Join-Path $repository 'dist'
$package = Join-Path $distribution "CodexUsage-$version-win64"
New-Item -ItemType Directory -Force -Path $package | Out-Null
$files = @('build/CodexUsage.dll', 'CodexUsage.example.ini', 'README.md', 'LICENSE', 'DISCLAIMER.md', 'SECURITY.md', 'THIRD_PARTY_NOTICES.md')
foreach ($file in $files) {
    Copy-Item -LiteralPath (Join-Path $repository $file) -Destination $package -Force
}
Copy-Item -LiteralPath (Join-Path $repository 'third_party/yyjson/LICENSE') -Destination (Join-Path $package 'yyjson-LICENSE.txt') -Force
$packageFiles = @($files | ForEach-Object { Join-Path $package ([IO.Path]::GetFileName($_)) }) + (Join-Path $package 'yyjson-LICENSE.txt')
$hashes = Get-Item -LiteralPath $packageFiles | Sort-Object Name | ForEach-Object {
    $hash = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    "$hash  $($_.Name)"
}
$hashes | Set-Content -LiteralPath (Join-Path $package 'SHA256SUMS.txt') -Encoding ASCII
$archive = Join-Path $distribution "CodexUsage-$version-win64.zip"
Compress-Archive -LiteralPath ($packageFiles + (Join-Path $package 'SHA256SUMS.txt')) -DestinationPath $archive -Force
$archiveHash = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
"$archiveHash  $([IO.Path]::GetFileName($archive))" | Set-Content -LiteralPath ($archive + '.sha256') -Encoding ASCII
Write-Output "Created $([IO.Path]::GetFileName($archive))"
