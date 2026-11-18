[CmdletBinding()]
param(
    [string]$Version = "v5.19",
    [switch]$Latest
)

$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot
$project = Join-Path $repo "MeatyEFTRelease"
$headers = @{ "User-Agent" = "MeatyEFT-MemProcFS-importer" }
$releaseUri = if ($Latest) {
    "https://api.github.com/repos/ufrisk/MemProcFS/releases/latest"
} else {
    "https://api.github.com/repos/ufrisk/MemProcFS/releases/tags/$Version"
}

Write-Host "Reading official MemProcFS release metadata..."
$release = Invoke-RestMethod -Headers $headers -Uri $releaseUri
$tag = [string]$release.tag_name
$asset = $release.assets |
    Where-Object { $_.name -match '^MemProcFS_files_and_binaries_v.+-win_x64-.+\.zip$' } |
    Sort-Object name -Descending |
    Select-Object -First 1

if ($null -eq $asset) {
    throw "The $tag release does not contain a versioned Windows x64 bundle."
}

$temp = Join-Path ([System.IO.Path]::GetTempPath()) ("MeatyEFT-MemProcFS-" + [guid]::NewGuid().ToString("N"))
$archive = Join-Path $temp "memprocfs.zip"
$extracted = Join-Path $temp "extracted"

try {
    New-Item -ItemType Directory -Path $temp -Force | Out-Null
    Write-Host "Downloading $($asset.name)..."
    Invoke-WebRequest -Headers $headers -Uri $asset.browser_download_url -OutFile $archive
    Expand-Archive -LiteralPath $archive -DestinationPath $extracted -Force

    $runtime = Join-Path $project "runtime\memprocfs"
    $libs = Join-Path $project "libs"
    New-Item -ItemType Directory -Path $runtime -Force | Out-Null

    foreach ($name in @("vmm.dll", "leechcore.dll", "FTD3XX.dll", "FTD3XXWU.dll", "info.db")) {
        $source = Join-Path $extracted $name
        if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
            throw "$name was not present in $($asset.name)."
        }
        Copy-Item -LiteralPath $source -Destination (Join-Path $runtime $name) -Force
    }

    foreach ($name in @("vmm.lib", "leechcore.lib")) {
        Copy-Item -LiteralPath (Join-Path $extracted $name) -Destination (Join-Path $libs $name) -Force
    }

    Write-Host "Downloading headers from the matching $tag source tag..."
    Invoke-WebRequest -Headers $headers -Uri "https://raw.githubusercontent.com/ufrisk/MemProcFS/$tag/includes/vmmdll.h" -OutFile (Join-Path $libs "vmmdll.h")
    Invoke-WebRequest -Headers $headers -Uri "https://raw.githubusercontent.com/ufrisk/MemProcFS/$tag/includes/leechcore.h" -OutFile (Join-Path $libs "leechcore.h")

    $vmmVersion = [System.Diagnostics.FileVersionInfo]::GetVersionInfo((Join-Path $runtime "vmm.dll"))
    $leechVersion = [System.Diagnostics.FileVersionInfo]::GetVersionInfo((Join-Path $runtime "leechcore.dll"))
    $hashes = @{}
    foreach ($name in @("vmm.dll", "leechcore.dll", "FTD3XX.dll", "FTD3XXWU.dll", "info.db")) {
        $hashes[$name] = (Get-FileHash -LiteralPath (Join-Path $runtime $name) -Algorithm SHA256).Hash.ToLowerInvariant()
    }

    $generated = @"
#pragma once

#include <array>
#include <cstdint>

namespace MemProcFSBundle
{
    struct ExpectedFile
    {
        const wchar_t* name;
        const char* sha256;
    };

    inline constexpr const char* Tag = "$tag";
    inline constexpr const char* Name = "MemProcFS $($vmmVersion.FileMajorPart).$($vmmVersion.FileMinorPart).$($vmmVersion.FileBuildPart) / LeechCore $($leechVersion.FileMajorPart).$($leechVersion.FileMinorPart).$($leechVersion.FileBuildPart)";
    inline constexpr std::uint64_t VmmMajor = $($vmmVersion.FileMajorPart);
    inline constexpr std::uint64_t VmmMinor = $($vmmVersion.FileMinorPart);
    inline constexpr std::uint64_t VmmRevision = $($vmmVersion.FileBuildPart);
    inline constexpr std::array<ExpectedFile, 5> Files{{
        {L"vmm.dll", "$($hashes['vmm.dll'])"},
        {L"leechcore.dll", "$($hashes['leechcore.dll'])"},
        {L"FTD3XX.dll", "$($hashes['FTD3XX.dll'])"},
        {L"FTD3XXWU.dll", "$($hashes['FTD3XXWU.dll'])"},
        {L"info.db", "$($hashes['info.db'])"}
    }};
}
"@
    $generatedPath = Join-Path $project "Core\Dependencies\MemProcFSBundle.generated.h"
    [System.IO.File]::WriteAllText($generatedPath, $generated, [System.Text.UTF8Encoding]::new($false))

    $releaseOutput = Join-Path $repo "x64\Release"
    if (Test-Path -LiteralPath $releaseOutput -PathType Container) {
        foreach ($name in @("vmm.dll", "leechcore.dll", "FTD3XX.dll", "FTD3XXWU.dll", "info.db")) {
            Copy-Item -LiteralPath (Join-Path $runtime $name) -Destination (Join-Path $releaseOutput $name) -Force
        }
    }

    Write-Host ""
    Write-Host "Imported $tag successfully."
    Write-Host "Bundle: MemProcFS $($vmmVersion.FileVersion) / LeechCore $($leechVersion.FileVersion)"
    Write-Host "Next: rebuild Release x64 and distribute the executable with all five runtime files from this bundle."
}
finally {
    if (Test-Path -LiteralPath $temp) {
        Remove-Item -LiteralPath $temp -Recurse -Force
    }
}
