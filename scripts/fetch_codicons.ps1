[CmdletBinding()]
param(
    [string]$destination = (Join-Path $PSScriptRoot '..\assets\codicons')
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$package_uri = 'https://registry.npmjs.org/@vscode/codicons/-/codicons-0.0.46-24.tgz'
$expected_hashes = @{
    'archive' = 'D77BF2ED152E82C4B81288C5271A3481C61559D1A5416593756E3B0FE8A02BF1'
    'codicon.ttf' = '3819E4AE4B87350E7C37A5D8F24E71ADA2F1F2EE58F7CE5EBC1F88E3C8C38C80'
    'mapping.json' = 'C9C9C568B3D166B22C9B21073CBAD3928AEFE8FBBEF9262B336296985B520092'
    'LICENSE' = 'AF5E030844EFDDBC7AB00DCFEA8B019703753D4D9F5172D727C533A492AEC665'
    'LICENSE-CODE' = '9906940F61B1F0B533FA7D99BAF55178B2808FBE113EA51DFBFAD8572CCD5F2B'
}

function assert_hash {
    param(
        [Parameter(Mandatory)]
        [string]$path,
        [Parameter(Mandatory)]
        [string]$expected
    )

    $actual = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
    if ($actual -ne $expected) {
        throw "SHA-256 mismatch: $path`nExpected: $expected`nActual: $actual"
    }
}

$temp_base = [System.IO.Path]::GetFullPath([System.IO.Path]::GetTempPath())
$temp_name = 'gitman-codicons-' + [guid]::NewGuid().ToString('N')
$temp_root = [System.IO.Path]::GetFullPath((Join-Path $temp_base $temp_name))
if (-not $temp_root.StartsWith($temp_base, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw 'The temporary path is outside the system temporary directory.'
}

New-Item -ItemType Directory -Path $temp_root | Out-Null
try {
    $archive = Join-Path $temp_root 'codicons.tgz'
    Invoke-WebRequest -UseBasicParsing -Uri $package_uri -OutFile $archive
    assert_hash -path $archive -expected $expected_hashes['archive']

    & tar -xzf $archive -C $temp_root
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to extract the Codicons package: exit code $LASTEXITCODE"
    }

    $source_root = Join-Path $temp_root 'package'
    $files = @{
        'codicon.ttf' = (Join-Path $source_root 'dist\codicon.ttf')
        'mapping.json' = (Join-Path $source_root 'src\template\mapping.json')
        'LICENSE' = (Join-Path $source_root 'LICENSE')
        'LICENSE-CODE' = (Join-Path $source_root 'LICENSE-CODE')
    }

    foreach ($name in $files.Keys) {
        assert_hash -path $files[$name] -expected $expected_hashes[$name]
    }

    New-Item -ItemType Directory -Path $destination -Force | Out-Null
    foreach ($name in $files.Keys) {
        Copy-Item -LiteralPath $files[$name] -Destination (Join-Path $destination $name) -Force
    }

    Write-Host 'Codicons v0.0.46-24 assets and checksums verified.'
}
finally {
    $resolved_temp = [System.IO.Path]::GetFullPath($temp_root)
    if ($resolved_temp.StartsWith($temp_base, [System.StringComparison]::OrdinalIgnoreCase) -and
        (Split-Path $resolved_temp -Leaf) -eq $temp_name) {
        Remove-Item -LiteralPath $resolved_temp -Recurse -Force -ErrorAction SilentlyContinue
    }
}

