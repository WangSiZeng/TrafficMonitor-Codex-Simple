param([switch]$DllOnly)

$ErrorActionPreference = 'Stop'
$repository = Split-Path -Parent $PSScriptRoot
$version = '0.1.3'
$projectName = 'TrafficMonitor-Codex-Simple'
$distribution = Join-Path $repository 'dist'
New-Item -ItemType Directory -Force -Path $distribution | Out-Null
$standaloneDll = Join-Path $distribution 'CodexUsage.dll'
Copy-Item -LiteralPath (Join-Path $repository 'build/CodexUsage.dll') -Destination $standaloneDll -Force
$dllHash = (Get-FileHash -LiteralPath $standaloneDll -Algorithm SHA256).Hash.ToLowerInvariant()
"$dllHash  CodexUsage.dll" | Set-Content -LiteralPath ($standaloneDll + '.sha256') -Encoding ASCII
$licenseText = @('LICENSE', 'DISCLAIMER.md', 'THIRD_PARTY_NOTICES.md', 'third_party/yyjson/LICENSE') | ForEach-Object {
    "=== $_ ===`r`n" + [IO.File]::ReadAllText((Join-Path $repository $_))
}
$licenseText -join "`r`n`r`n" | Set-Content -LiteralPath (Join-Path $distribution 'CodexUsage-LICENSE.txt') -Encoding UTF8
if ($DllOnly) {
    Write-Output 'Created CodexUsage.dll, its SHA-256 file and license notices.'
    return
}
$package = Join-Path $distribution "$projectName-$version-win64"
New-Item -ItemType Directory -Force -Path $package | Out-Null
$files = @('build/CodexUsage.dll', 'CodexUsage.example.ini', 'README.md', 'LICENSE', 'DISCLAIMER.md', 'SECURITY.md', 'THIRD_PARTY_NOTICES.md')
foreach ($file in $files) {
    Copy-Item -LiteralPath (Join-Path $repository $file) -Destination $package -Force
}
Copy-Item -LiteralPath (Join-Path $repository 'third_party/yyjson/LICENSE') -Destination (Join-Path $package 'yyjson-LICENSE.txt') -Force
$packageFiles = @($files | ForEach-Object { Join-Path $package ([IO.Path]::GetFileName($_)) }) + (Join-Path $package 'yyjson-LICENSE.txt')
foreach ($image in @('docs/images/taskbar-usage.png', 'docs/images/plugin-settings.png')) {
    $imageTarget = Join-Path $package $image
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $imageTarget) | Out-Null
    Copy-Item -LiteralPath (Join-Path $repository $image) -Destination $imageTarget -Force
    $packageFiles += $imageTarget
}
$hashes = Get-Item -LiteralPath $packageFiles | Sort-Object Name | ForEach-Object {
    $hash = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    $relativePath = $_.FullName.Substring($package.Length + 1).Replace('\', '/')
    "$hash  $relativePath"
}
$hashes | Set-Content -LiteralPath (Join-Path $package 'SHA256SUMS.txt') -Encoding ASCII
$archive = Join-Path $distribution "$projectName-$version-win64.zip"
Add-Type -AssemblyName System.IO.Compression.FileSystem
$archiveStream = [IO.File]::Open($archive, [IO.FileMode]::Create)
try {
    $zip = [IO.Compression.ZipArchive]::new($archiveStream, [IO.Compression.ZipArchiveMode]::Create, $true)
    try {
        foreach ($filePath in ($packageFiles + (Join-Path $package 'SHA256SUMS.txt'))) {
            $entryName = $filePath.Substring($package.Length + 1).Replace('\', '/')
            [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip, $filePath, $entryName,
                [IO.Compression.CompressionLevel]::Optimal) | Out-Null
        }
    } finally { $zip.Dispose() }
} finally { $archiveStream.Dispose() }
$archiveHash = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
"$archiveHash  $([IO.Path]::GetFileName($archive))" | Set-Content -LiteralPath ($archive + '.sha256') -Encoding ASCII
Write-Output "Created $([IO.Path]::GetFileName($archive))"
