[CmdletBinding()]
param(
    [string]$root = (Join-Path $PSScriptRoot '..')
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$resolved_root = (Resolve-Path -LiteralPath $root).Path
$utf8 = [System.Text.UTF8Encoding]::new($false, $true)
$excluded_directories = @('.git', '.vs', 'bin', 'build', 'vcpkg_installed')
$checked_extensions = @('.cmake', '.cpp', '.h', '.json', '.md', '.ps1', '.rc', '.verison-list', '.xml')
$special_names = @('.clang-format', '.clang-tidy', '.editorconfig', '.gitattributes', '.gitignore', 'CMakeLists.txt')
$errors = [System.Collections.Generic.List[string]]::new()

function Add-MultilineBraceClosureErrors {
    param(
        [Parameter(Mandatory)]
        [string]$text,

        [Parameter(Mandatory)]
        [string]$relative,

        [Parameter(Mandatory)]
        [AllowEmptyCollection()]
        [System.Collections.Generic.List[string]]$errors
    )

    $brace_lines = [System.Collections.Generic.Stack[int]]::new()
    $state = 'code'
    $escaped_state = ''
    $raw_terminator = ''
    $line_number = 1
    $line_start = 0

    for ($index = 0; $index -lt $text.Length; $index++) {
        $character = $text[$index]
        if ($character -eq "`n") {
            $line_number++
            $line_start = $index + 1
            if ($state -eq 'line_comment') {
                $state = 'code'
            }
            elseif ($state -eq 'escape') {
                $state = $escaped_state
            }
            continue
        }

        if ($state -eq 'line_comment') {
            continue
        }
        if ($state -eq 'block_comment') {
            if ($character -eq '*' -and $index + 1 -lt $text.Length -and
                $text[$index + 1] -eq '/') {
                $state = 'code'
                $index++
            }
            continue
        }
        if ($state -eq 'raw_string') {
            if ($index + $raw_terminator.Length -le $text.Length -and
                $text.Substring($index, $raw_terminator.Length) -ceq $raw_terminator) {
                $state = 'code'
                $index += $raw_terminator.Length - 1
            }
            continue
        }
        if ($state -eq 'escape') {
            $state = $escaped_state
            continue
        }
        if ($state -eq 'string') {
            if ($character -eq '\') {
                $escaped_state = 'string'
                $state = 'escape'
            }
            elseif ($character -eq '"') {
                $state = 'code'
            }
            continue
        }
        if ($state -eq 'character') {
            if ($character -eq '\') {
                $escaped_state = 'character'
                $state = 'escape'
            }
            elseif ($character -eq "'") {
                $state = 'code'
            }
            continue
        }

        $next_character = if ($index + 1 -lt $text.Length) { $text[$index + 1] } else { '' }
        if ($character -eq '/' -and $next_character -eq '/') {
            $state = 'line_comment'
            $index++
            continue
        }
        if ($character -eq '/' -and $next_character -eq '*') {
            $state = 'block_comment'
            $index++
            continue
        }
        if ($character -eq 'R' -and $next_character -eq '"') {
            $delimiter_start = $index + 2
            $delimiter_end = $text.IndexOf('(', $delimiter_start)
            if ($delimiter_end -ge $delimiter_start -and $delimiter_end - $delimiter_start -le 16) {
                $delimiter = $text.Substring($delimiter_start, $delimiter_end - $delimiter_start)
                if ($delimiter -notmatch '[\s\\()]') {
                    $raw_terminator = ')' + $delimiter + '"'
                    $state = 'raw_string'
                    $index = $delimiter_end
                    continue
                }
            }
        }
        if ($character -eq '"') {
            $state = 'string'
            continue
        }
        if ($character -eq "'") {
            $state = 'character'
            continue
        }
        if ($character -eq '{') {
            $brace_lines.Push($line_number)
            continue
        }
        if ($character -eq '}' -and $brace_lines.Count -gt 0) {
            $opening_line = $brace_lines.Pop()
            if ($opening_line -ne $line_number) {
                $line_prefix = $text.Substring($line_start, $index - $line_start)
                if ($line_prefix -match '\S') {
                    $errors.Add(
                        "$relative`:$line_number`: The closing brace of a multiline block must be on its own line.")
                }
            }
        }
    }
}

$files = Get-ChildItem -LiteralPath $resolved_root -Recurse -File | Where-Object {
    $relative = [System.IO.Path]::GetRelativePath($resolved_root, $_.FullName)
    $segments = $relative -split '[\\/]'
    $is_excluded = $false
    foreach ($directory in $excluded_directories) {
        if ($segments -contains $directory) {
            $is_excluded = $true
            break
        }
    }
    if ($relative -like 'assets\codicons\*' -or $relative -like 'assets/codicons/*') {
        $is_excluded = $true
    }
    (-not $is_excluded) -and
        (($_.Extension -in $checked_extensions) -or ($_.Name -in $special_names))
}

foreach ($file in $files) {
    $relative = [System.IO.Path]::GetRelativePath($resolved_root, $file.FullName)
    $bytes = [System.IO.File]::ReadAllBytes($file.FullName)
    if ($bytes.Length -ge 3 -and $bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and
        $bytes[2] -eq 0xBF) {
        $errors.Add("$relative`: UTF-8 BOM found.")
    }

    try {
        $text = $utf8.GetString($bytes)
    }
    catch {
        $errors.Add("$relative`: Invalid UTF-8.")
        continue
    }

    if ($text -match '(?<!\r)\n' -or $text -match '\r(?!\n)') {
        $errors.Add("$relative`: Non-CRLF line ending found.")
    }
    if ($text.Contains("`t")) {
        $errors.Add("$relative`: Tab character found.")
    }
    if ($text -match '[ ]+\r\n') {
        $errors.Add("$relative`: Trailing whitespace found.")
    }

    if ($file.Extension -in @('.cpp', '.h')) {
        if ($text -match 'template[ \t]*<[^\r\n]+>[ \t]+\S') {
            $errors.Add("$relative`: A template declaration and its signature must be on separate lines.")
        }
        Add-MultilineBraceClosureErrors -text $text -relative $relative -errors $errors

        $declarations = [regex]::Matches(
            $text,
            '\b(?:namespace|class|struct|enum\s+class)\s+([A-Za-z_][A-Za-z0-9_]*)')
        foreach ($declaration in $declarations) {
            $identifier = $declaration.Groups[1].Value
            if ($identifier -notmatch '^[a-z][a-z0-9]*(?:_[a-z0-9]+)*$') {
                $errors.Add("$relative`: Type or namespace is not snake_case: $identifier")
            }
        }
    }
}

if ($errors.Count -gt 0) {
    $errors | ForEach-Object { Write-Host $_ -ForegroundColor Red }
    throw "Source style check failed: $($errors.Count) violation(s)"
}

Write-Host "Source style check passed: $($files.Count) file(s)"

