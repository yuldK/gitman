# Skia를 손으로 1회 빌드하는 보조 스크립트다.
#
# 이 스크립트는 사용자가 직접 실행한다. CMake와 CTest는 절대 이 스크립트를 호출하지
# 않는다. 빌드 체계가 스스로 취득하는 경로를 만들지 않는 것이 ADR-006의 전제이며,
# 그 경계가 바로 여기다. 자세한 내용은 docs/skia-build.md를 본다.
#
# 취득은 submodule과 gn·ninja 두 실행 파일로 끝난다. 두 실행 파일은 PATH에 없는 것이
# 보통이므로 알려진 로컬 경로를 먼저 훑는다. 그래도 없고 사람이 -FetchTools를 준
# 경우에만 Skia의 bin/fetch-gn·bin/fetch-ninja로 내려받는다. 기본 동작은 여전히
# 아무것도 내려받지 않는 것이다.

[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release',
    [string]$ArgumentFile,
    [string]$GnPath,
    [string]$NinjaPath,
    [string]$PythonPath,
    [switch]$CopyExternals,
    [switch]$FetchTools
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repository_root = Split-Path -Parent $PSScriptRoot
$skia_root = Join-Path $repository_root 'third_party\skia'
$externals_source = Join-Path $repository_root 'third_party\skia-externals'
$externals_target = Join-Path $skia_root 'third_party\externals'
$patch_file = Join-Path $repository_root 'third_party\patches\skia-148-direct3d-operator-equals.patch'

# 브라우저로 받은 실행 파일을 두는 자리다. 저장소는 이 디렉터리를 추적하지 않는다.
$tool_directory = Join-Path $repository_root 'third_party\skia-tools'
$minimum_ninja_version = [version]'1.13'

# Visual Studio는 CMake 지원의 일부로 ninja를 함께 설치한다. 이미 있는 것을 쓰면
# ninja는 받을 필요가 없다. 버전이 낮은 설치본은 뒤의 검사에서 걸러진다.
function Get-VisualStudioNinjaPath {
    $program_files = ${env:ProgramFiles(x86)}
    if (-not $program_files) {
        return @()
    }
    $vswhere = Join-Path $program_files 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswhere -PathType Leaf)) {
        return @()
    }

    try {
        $installations = & $vswhere -products '*' -prerelease -property installationPath
    }
    catch {
        return @()
    }
    if ($LASTEXITCODE -ne 0 -or -not $installations) {
        return @()
    }
    return @($installations |
        Where-Object { $_ } |
        ForEach-Object {
            Join-Path $_ 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'
        })
}

# 찾을 자리는 고정이다. 사람이 둔 자리가 먼저고, 다음이 Skia의 fetch 스크립트가 두는
# 자리, 마지막이 Visual Studio다. 순서가 곧 우선순위다.
function Get-ToolCandidate {
    param([string]$name)

    $candidates = [System.Collections.Generic.List[string]]::new()
    $candidates.Add((Join-Path $tool_directory ('{0}.exe' -f $name)))
    if ($name -eq 'gn') {
        $candidates.Add((Join-Path $skia_root 'bin\gn.exe'))
        $candidates.Add((Join-Path $skia_root 'third_party\gn\gn.exe'))
    }
    else {
        $candidates.Add((Join-Path $skia_root 'third_party\ninja\ninja.exe'))
        $candidates.Add((Join-Path $skia_root 'bin\ninja.exe'))
        foreach ($path in (Get-VisualStudioNinjaPath)) {
            $candidates.Add($path)
        }
    }
    return $candidates.ToArray()
}

function Test-NinjaVersion {
    param([string]$path)

    try {
        $output = & $path --version
    }
    catch {
        return $false
    }
    if ($LASTEXITCODE -ne 0 -or -not $output) {
        return $false
    }
    if (@($output)[0] -notmatch '(\d+(?:\.\d+){1,2})') {
        return $false
    }
    return ([version]$Matches[1] -ge $minimum_ninja_version)
}

function Find-Tool {
    param([string]$name, [string[]]$candidates, [scriptblock]$accept)

    foreach ($candidate in $candidates) {
        if (-not $candidate -or -not (Test-Path -LiteralPath $candidate -PathType Leaf)) {
            continue
        }
        $resolved = (Resolve-Path -LiteralPath $candidate).Path
        if ($accept -and -not (& $accept $resolved)) {
            Write-Verbose "$name rejected: $resolved"
            continue
        }
        return $resolved
    }

    $command = Get-Command -Name $name -CommandType Application -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if ($command) {
        if (-not $accept -or (& $accept $command.Source)) {
            return $command.Source
        }
        Write-Verbose "$name rejected: $($command.Source)"
    }
    return ''
}

# Skia의 fetch 스크립트는 사람이 -FetchTools로 지시했을 때만 돈다. 산출 위치는
# bin/gn.exe와 third_party/ninja/ninja.exe이며 Skia의 .gitignore가 둘 다 무시한다.
function Invoke-SkiaFetch {
    param([string]$fetch_script)

    $script_path = Join-Path $skia_root ('bin\{0}' -f $fetch_script)
    if (-not (Test-Path -LiteralPath $script_path -PathType Leaf)) {
        throw "The Skia fetch script was not found: $script_path"
    }

    Write-Output "fetching       : bin/$fetch_script"
    & $python $script_path
    if ($LASTEXITCODE -ne 0) {
        throw @"
bin/$fetch_script failed with exit code $LASTEXITCODE.
It downloads from chrome-infra-packages.appspot.com. If that is blocked, fetch the
executable with a browser and put it in: $tool_directory
"@
    }
}

function Resolve-Tool {
    param([string]$given, [string]$name, [string]$fetch_script, [string]$hint,
        [scriptblock]$accept)

    if ($given) {
        if (-not (Test-Path -LiteralPath $given -PathType Leaf)) {
            throw "$name was not found at the given path: $given"
        }
        return (Resolve-Path -LiteralPath $given).Path
    }

    $candidates = Get-ToolCandidate -name $name
    $found = Find-Tool -name $name -candidates $candidates -accept $accept
    if ($found) {
        return $found
    }

    if ($FetchTools) {
        Invoke-SkiaFetch -fetch_script $fetch_script
        $found = Find-Tool -name $name -candidates $candidates -accept $accept
        if ($found) {
            return $found
        }
        throw "bin/$fetch_script reported success but $name was still not found."
    }

    $searched = (@($candidates) + '(PATH)') -join "`n  "
    throw @"
$name was not found. Searched:
  $searched

Do one of these:
  1. Re-run with -FetchTools to let Skia's bin/$fetch_script download it.
     It needs access to chrome-infra-packages.appspot.com.
  2. Fetch it with a browser and put it in: $tool_directory
     $hint
  3. Pass the path with -GnPath or -NinjaPath.
"@
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

# python을 먼저 정한다. -FetchTools가 fetch 스크립트를 돌릴 때 쓰는 것도 이것이다.
$python = Resolve-Python -given $PythonPath
$gn = Resolve-Tool -given $GnPath -name 'gn' -fetch_script 'fetch-gn' `
    -hint 'The CIPD package page for gn/gn/windows-amd64 has it.'
$ninja = Resolve-Tool -given $NinjaPath -name 'ninja' -fetch_script 'fetch-ninja' `
    -hint 'The ninja-build releases page has ninja-win.zip.' `
    -accept { param($path) Test-NinjaVersion -path $path }
if ($NinjaPath -and -not (Test-NinjaVersion -path $ninja)) {
    Write-Warning "The given ninja is older than $minimum_ninja_version or reported no version: $ninja"
}

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
