@echo off
setlocal EnableExtensions DisableDelayedExpansion
rem Place this BAT in docs. Only files inside immediate subfolders are indexed.
rem Usage: rebuild_zayapp_index.bat [--no-pause]
set "ZAYAPP_INDEX_SELF=%~f0"
set "ZAYAPP_INDEX_ROOT=%~dp0"
set "ZAYAPP_INDEX_NO_PAUSE="
if /i "%~1"=="--no-pause" set "ZAYAPP_INDEX_NO_PAUSE=1"

"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoLogo -NoProfile -NonInteractive -Command "$ErrorActionPreference='Stop'; try { $source=[IO.File]::ReadAllText($env:ZAYAPP_INDEX_SELF); $marker='#==ZAYAPP_INDEX_POWERSHELL=='; $offset=$source.LastIndexOf($marker, [StringComparison]::Ordinal); if ($offset -lt 0) { throw 'Embedded PowerShell script was not found.' }; & ([ScriptBlock]::Create($source.Substring($offset + $marker.Length))) } catch { [Console]::Error.WriteLine(('[ERROR] ' + $_.Exception.Message)); exit 1 }"
set "ZAYAPP_INDEX_EXIT=%ERRORLEVEL%"
if not "%ZAYAPP_INDEX_EXIT%"=="0" echo [ERROR] zayapp_index.json was not rebuilt.
if not defined ZAYAPP_INDEX_NO_PAUSE pause
exit /b %ZAYAPP_INDEX_EXIT%

#==ZAYAPP_INDEX_POWERSHELL==
# Requires Windows PowerShell 5.1. No separate PS1 or other dependency is needed.
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0
$temporaryPath = $null
$exitCode = 1

try {
    $root = [IO.Path]::GetFullPath($env:ZAYAPP_INDEX_ROOT)
    $rootInfo = [IO.DirectoryInfo]::new($root)
    if (-not $rootInfo.Exists) {
        throw ('The BAT folder does not exist: ' + $root)
    }

    $rootPrefix = $rootInfo.FullName
    $separator = [string][IO.Path]::DirectorySeparatorChar
    if (-not $rootPrefix.EndsWith($separator, [StringComparison]::Ordinal)) {
        $rootPrefix += $separator
    }

    $targetPath = [IO.Path]::Combine($root, 'zayapp_index.json')
    $invariant = [Globalization.CultureInfo]::InvariantCulture
    $reparsePoint = [IO.FileAttributes]::ReparsePoint
    $pending = [Collections.Generic.Stack[IO.DirectoryInfo]]::new()
    $byPath = [Collections.Generic.SortedDictionary[string,object]]::new([StringComparer]::Ordinal)
    $folderCount = 0
    $skippedLinks = 0

    # Select folders dynamically. Files directly beside this BAT are excluded.
    foreach ($folder in $rootInfo.GetDirectories()) {
        if (($folder.Attributes -band $reparsePoint) -ne 0) {
            $skippedLinks++
            continue
        }
        $pending.Push($folder)
        $folderCount++
    }

    [Console]::WriteLine(('Scanning {0} folder(s) in: {1}' -f $folderCount, $root))

    while ($pending.Count -gt 0) {
        $directory = $pending.Pop()
        foreach ($item in $directory.GetFileSystemInfos()) {
            $item.Refresh()
            if (-not $item.Exists) {
                throw ('An item disappeared during the scan: ' + $item.FullName)
            }
            # Do not follow links or junctions outside the asset tree or in loops.
            if (($item.Attributes -band $reparsePoint) -ne 0) {
                $skippedLinks++
                continue
            }
            if ($item -is [IO.DirectoryInfo]) {
                $pending.Push($item)
                continue
            }

            $relativePath = $item.FullName.Substring($rootPrefix.Length).Replace('\', '/')
            $byPath.Add($relativePath, [ordered]@{
                atime = $item.LastAccessTimeUtc.ToFileTimeUtc().ToString($invariant)
                ctime = $item.CreationTimeUtc.ToFileTimeUtc().ToString($invariant)
                mtime = $item.LastWriteTimeUtc.ToFileTimeUtc().ToString($invariant)
                path  = $relativePath
                size  = $item.Length.ToString($invariant)
            })
        }
    }

    # Keep list an array even with zero or one file. Paths use ordinal sorting.
    $entries = [Collections.Generic.List[object]]::new()
    foreach ($entry in $byPath.Values) {
        $entries.Add($entry)
    }
    $document = [ordered]@{
        list = $entries.ToArray()
        version = 'zayapp_index_v1'
    }
    $json = ConvertTo-Json -InputObject $document -Depth 4

    # Write completely before replacing the old index; leave it intact on failure.
    $temporaryPath = [IO.Path]::Combine($root, ('.zayapp_index.' + [Guid]::NewGuid().ToString('N') + '.tmp'))
    $encoding = [Text.UTF8Encoding]::new($false)
    [IO.File]::WriteAllText($temporaryPath, ($json + [Environment]::NewLine), $encoding)

    if ([IO.File]::Exists($targetPath)) {
        if (([IO.File]::GetAttributes($targetPath) -band $reparsePoint) -ne 0) {
            throw 'The output index is a link. Use a regular zayapp_index.json file.'
        }
        [IO.File]::Replace($temporaryPath, $targetPath, [System.Management.Automation.Language.NullString]::Value)
    }
    else {
        [IO.File]::Move($temporaryPath, $targetPath)
    }
    $temporaryPath = $null
    $exitCode = 0

    [Console]::WriteLine(('OK: {0} folder(s), {1} file(s).' -f $folderCount, $entries.Count))
    [Console]::WriteLine(('Written: ' + $targetPath))
    if ($skippedLinks -gt 0) {
        [Console]::WriteLine(('Skipped {0} symbolic link / junction item(s).' -f $skippedLinks))
    }
}
catch {
    [Console]::Error.WriteLine(('[ERROR] ' + $_.Exception.Message))
}
finally {
    if ($temporaryPath -and [IO.File]::Exists($temporaryPath)) {
        try {
            [IO.File]::Delete($temporaryPath)
        }
        catch {
            [Console]::Error.WriteLine(('[WARNING] Could not remove temporary file: ' + $temporaryPath))
        }
    }
}

exit $exitCode
