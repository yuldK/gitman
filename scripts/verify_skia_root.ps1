# 사용자가 빌드한 Skia가 Gitman과 맞는지 검사한다. CMake의 configure 검사와 같은
# 판정을 사람이 먼저 돌려볼 수 있게 하는 것이 목적이다. docs/skia-build.md를 본다.

[CmdletBinding()]
param(
    [string]$SkiaRoot,
    [string[]]$Configurations = @('Debug', 'Release')
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repository_root = Split-Path -Parent $PSScriptRoot
if (-not $SkiaRoot) {
    $SkiaRoot = Join-Path $repository_root 'third_party\skia'
}

# CMakeLists.txt의 GITMAN_SKIA_COMPONENTS와 같은 목록이다.
$components = @('skia', 'skcms', 'spirv_cross', 'd3d12allocator')
$failures = [System.Collections.Generic.List[string]]::new()

function Add-Result {
    param([string]$item, [bool]$ok, [string]$detail)

    $mark = if ($ok) { 'OK  ' } else { 'FAIL' }
    Write-Output ("[{0}] {1,-42} {2}" -f $mark, $item, $detail)
    if (-not $ok) {
        $script:failures.Add($item)
    }
}

Write-Output "Skia root: $SkiaRoot"
Write-Output ''

$header = Join-Path $SkiaRoot 'include\core\SkCanvas.h'
Add-Result 'source tree' (Test-Path -LiteralPath $header) $header

# 패치가 적용되지 않은 Skia는 Direct3D 빌드가 깨진다.
$patch_file = Join-Path $repository_root 'third_party\patches\skia-148-direct3d-operator-equals.patch'
if (Test-Path -LiteralPath $patch_file) {
    & git -C $SkiaRoot apply --reverse --check $patch_file 2>&1 | Out-Null
    Add-Result 'direct3d patch applied' ($LASTEXITCODE -eq 0) $patch_file
}

$dumpbin = Get-Command dumpbin -ErrorAction SilentlyContinue

foreach ($configuration in $Configurations) {
    Write-Output ''
    $directory = Join-Path $SkiaRoot ('out\gitman-{0}' -f $configuration.ToLowerInvariant())
    Write-Output "-- $configuration : $directory"

    foreach ($component in $components) {
        $library = Join-Path $directory "$component.lib"
        $exists = Test-Path -LiteralPath $library
        $detail = if ($exists) {
            '{0:N1} MB' -f ((Get-Item -LiteralPath $library).Length / 1MB)
        }
        else {
            'missing'
        }
        Add-Result "$configuration/$component.lib" $exists $detail
    }

    $arguments_file = Join-Path $directory 'args.gn'
    if (Test-Path -LiteralPath $arguments_file) {
        $arguments_text = Get-Content -Raw -LiteralPath $arguments_file
        Add-Result "$configuration/skia_use_direct3d" `
            ($arguments_text -match 'skia_use_direct3d\s*=\s*true') 'required by the renderer'
    }
    else {
        Add-Result "$configuration/args.gn" $false 'missing'
    }

    # 정적 CRT가 Gitman과 어긋나면 LNK2038로 드러난다. 미리 잡는다.
    $library = Join-Path $directory 'skia.lib'
    if ($dumpbin -and (Test-Path -LiteralPath $library)) {
        $expected = if ($configuration -eq 'Debug') { 'LIBCMTD' } else { 'LIBCMT' }
        $directives = & $dumpbin.Source /directives $library 2>$null |
            Select-String 'DEFAULTLIB' |
            ForEach-Object { $_.Line.Trim() } |
            Sort-Object -Unique
        $matched = $directives | Where-Object { $_ -match "/DEFAULTLIB:$expected$" }
        Add-Result "$configuration/static CRT" ([bool]$matched) "expects /DEFAULTLIB:$expected"
    }
}

Write-Output ''
if ($failures.Count -gt 0) {
    Write-Output "Skia verification failed: $($failures.Count) item(s)."
    Write-Output 'Build Skia as described in docs/skia-build.md.'
    exit 1
}

Write-Output 'Skia verification passed.'
