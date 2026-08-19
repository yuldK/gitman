# Skia를 손으로 1회 빌드하는 보조 스크립트다.
#
# 이 스크립트는 사용자가 직접 실행한다. CMake와 CTest는 절대 이 스크립트를 호출하지
# 않는다. 빌드 체계가 스스로 취득하는 경로를 만들지 않는 것이 ADR-006의 전제이며,
# 그 경계가 바로 여기다. 자세한 내용은 docs/skia-build.md를 본다.
#
# 취득은 submodule과 gn·ninja 두 실행 파일로 끝난다. 이 스크립트는 아무것도
# 내려받지 않는다.

[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release',
    [string]$ArgumentFile,
    [string]$GnPath,
    [string]$NinjaPath,
    [string]$PythonPath,
    [switch]$CopyExternals
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repository_root = Split-Path -Parent $PSScriptRoot
$skia_root = Join-Path $repository_root 'third_party\skia'
$externals_source = Join-Path $repository_root 'third_party\skia-externals'
$externals_target = Join-Path $skia_root 'third_party\externals'
$patch_file = Join-Path $repository_root 'third_party\patches\skia-148-direct3d-operator-equals.patch'

function Resolve-Tool {
    param([string]$given, [string]$name, [string]$hint)

    if ($given) {
        if (-not (Test-Path -LiteralPath $given)) {
            throw "$name was not found at the given path: $given"
        }
        return (Resolve-Path -LiteralPath $given).Path
    }
    $command = Get-Command $name -ErrorAction SilentlyContinue
    if (-not $command) {
        throw "$name was not found on PATH. Pass the path explicitly. $hint"
    }
    return $command.Source
}

# Skia의 .gn은 script_executable을 "python3"로 두는데, Windows의 python3.exe는
# Microsoft Store 스텁인 경우가 많아 실행에 실패한다. 실제 인터프리터를 찾아
# --script-executable로 넘긴다.
function Resolve-Python {
    param([string]$given)

    if ($given) {
        if (-not (Test-Path -LiteralPath $given)) {
            throw "Python was not found at the given path: $given"
        }
        return (Resolve-Path -LiteralPath $given).Path
    }
    foreach ($candidate in @('python', 'python3')) {
        $command = Get-Command $candidate -ErrorAction SilentlyContinue
        if (-not $command) { continue }
        if ($command.Source -like '*\WindowsApps\*') { continue }
        return $command.Source
    }
    throw 'A real Python 3 interpreter was not found. Pass -PythonPath explicitly.'
}

if (-not (Test-Path -LiteralPath (Join-Path $skia_root 'include\core\SkCanvas.h'))) {
    throw @"
The Skia submodule is not initialized: $skia_root
Run: git submodule update --init third_party/skia
"@
}

if (-not $ArgumentFile) {
    $ArgumentFile = Join-Path $repository_root ('third_party\skia-args\gitman-{0}.gn' -f $Configuration.ToLowerInvariant())
}
if (-not (Test-Path -LiteralPath $ArgumentFile)) {
    throw "The GN argument file was not found: $ArgumentFile"
}
$argument_text = Get-Content -Raw -LiteralPath $ArgumentFile

# 필요한 external은 GN args가 정한다. 최소 구성은 셋이고, 텍스트 처리 구성은 넷이
# 추가된다. 쓰지 않는 external은 요구하지 않는다.
$required_externals = [System.Collections.Generic.List[string]]@(
    'd3d12allocator', 'spirv-cross', 'spirv-headers')
if ($argument_text -match 'skia_use_harfbuzz\s*=\s*true') {
    $required_externals.Add('harfbuzz')
}
if ($argument_text -match 'skia_use_libgrapheme\s*=\s*true') {
    $required_externals.Add('libgrapheme')
    $required_externals.Add('unicodetools')
    # libgrapheme backend도 BiDi는 ICU 소스를 컴파일한다. 데이터 파일은 쓰지 않는다.
    $required_externals.Add('icu')
}
if ($argument_text -match 'skia_use_icu\s*=\s*true') {
    $required_externals.Add('icu')
}

$gn = Resolve-Tool -given $GnPath -name 'gn' -hint 'Download it from the CIPD package page for gn/gn/windows-amd64.'
$ninja = Resolve-Tool -given $NinjaPath -name 'ninja' -hint 'Download ninja-win.zip from the ninja-build releases page.'
$python = Resolve-Python -given $PythonPath

Write-Output "Skia root      : $skia_root"
Write-Output "Configuration  : $Configuration"
Write-Output "Argument file  : $ArgumentFile"
Write-Output "gn             : $gn"
Write-Output "ninja          : $ninja"
Write-Output "python         : $python"

# 1. external 배치. Skia 저장소를 수정하지 않도록 junction으로 연결한다.
New-Item -ItemType Directory -Force -Path $externals_target | Out-Null
foreach ($name in ($required_externals | Sort-Object -Unique)) {
    $source = Join-Path $externals_source $name
    if (-not (Test-Path -LiteralPath $source) -or
        -not (Get-ChildItem -LiteralPath $source -Force | Where-Object { $_.Name -ne '.git' })) {
        throw @"
The external submodule is not initialized: third_party/skia-externals/$name
Run: git submodule update --init third_party/skia-externals/$name
"@
    }

    $target = Join-Path $externals_target $name
    if (Test-Path -LiteralPath $target) {
        Write-Output "external ready : $name"
        continue
    }
    if ($CopyExternals) {
        Copy-Item -Recurse -LiteralPath $source -Destination $target
        Write-Output "external copied: $name"
    }
    else {
        New-Item -ItemType Junction -Path $target -Target $source | Out-Null
        Write-Output "external linked: $name"
    }
}

# 2. 패치. 이미 적용된 경우 다시 적용하지 않는다.
$reverse_check = & git -C $skia_root apply --reverse --check $patch_file 2>&1
if ($LASTEXITCODE -eq 0) {
    Write-Output 'patch          : already applied'
}
else {
    & git -C $skia_root apply $patch_file
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to apply the Direct3D patch: $patch_file"
    }
    Write-Output 'patch          : applied'
}

# 3. gn gen. 인자 파일의 줄바꿈을 공백으로 바꿔 한 줄로 전달한다.
$output_directory = Join-Path $skia_root ('out\gitman-{0}' -f $Configuration.ToLowerInvariant())
$flat_arguments = ($argument_text -replace "`r`n", ' ') -replace "`n", ' '
Push-Location $skia_root
try {
    & $gn gen $output_directory "--script-executable=$python" "--args=$flat_arguments"
    if ($LASTEXITCODE -ne 0) {
        throw 'gn gen failed.'
    }

    # 4. ninja. 텍스트 처리 구성은 module 라이브러리도 함께 만든다.
    $targets = @('skia')
    if ($argument_text -match 'skia_use_harfbuzz\s*=\s*true') {
        $targets += 'modules'
    }
    & $ninja -C $output_directory @targets
    if ($LASTEXITCODE -ne 0) {
        throw 'ninja failed.'
    }
}
finally {
    Pop-Location
}

Write-Output ''
Write-Output "Skia build finished: $output_directory"
Write-Output 'Verify it with: scripts\verify_skia_root.ps1'
