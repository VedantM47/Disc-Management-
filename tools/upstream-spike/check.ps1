param([string]$Only = '', [int]$Show = 12)
$ErrorActionPreference = 'Continue'
$sp = $PSScriptRoot
$cxx = Get-ChildItem "$env:LOCALAPPDATA\Microsoft\WinGet\Packages" -Recurse -Filter x86_64-w64-mingw32-clang++.exe | Select-Object -First 1 -ExpandProperty FullName
$u = 'C:\Users\imved\AppData\Local\Temp\claude\c--Users-imved-OneDrive-Desktop-OS-CP\b32605fd-4c1c-48dd-840d-c8027f721075\scratchpad\spike\ws'
$incs = @($u, "$u\Controls", "$u\Views", "$u\Dialogs", "$u\Pages", "$u\res", "$sp\shim", $sp) | ForEach-Object { "-I`"$_`"" }
$files = Get-ChildItem $u -Recurse -Filter *.cpp | Where-Object { $_.Name -ne 'pch.cpp' -and $_.Name -match $Only }
$jobs = @()
foreach ($f in $files) {
    while (@($jobs | Where-Object { -not $_.P.HasExited }).Count -ge 12) { Start-Sleep -Milliseconds 50 }
    $psi = [Diagnostics.ProcessStartInfo]::new($cxx)
    $psi.Arguments = "-std=c++23 -fsyntax-only -ferror-limit=0 -w -fms-extensions -D_WIN32_WINNT=0x0A00 -DNTDDI_VERSION=0x0A00000C -DUNICODE -D_UNICODE -DNOMINMAX $($incs -join ' ') `"$($f.FullName)`""
    $psi.UseShellExecute = $false; $psi.RedirectStandardError = $true; $psi.RedirectStandardOutput = $true; $psi.CreateNoWindow = $true
    $p = [Diagnostics.Process]::Start($psi)
    $jobs += [pscustomobject]@{ P = $p; E = $p.StandardError.ReadToEndAsync(); F = $f.FullName.Substring($u.Length + 1) }
}
$jobs | ForEach-Object { $_.P.WaitForExit() }
$all = foreach ($j in $jobs) { ($j.E.Result -split "`n") | Where-Object { $_ -match ' error: ' } | ForEach-Object {
    [pscustomobject]@{ F = $j.F; Loc = ($_ -replace ': error: .*$', '' -replace '^.*\\windirstat\\', ''); E = ($_ -replace '^.*? error: ', '').Trim() } } }
$clean = @($jobs | Where-Object { $_.P.ExitCode -eq 0 }).Count
"files=$($jobs.Count) clean=$clean failing=$($jobs.Count - $clean) total_errors=$(@($all).Count) unique_sites=$(@($all | Sort-Object Loc -Unique).Count)"
'--- error kinds (unique sites):'
$all | Sort-Object Loc -Unique | Group-Object { ($_.E -replace "'[^']*'", "'X'") } | Sort-Object Count -Descending | Select-Object -First $Show |
    ForEach-Object { '{0,4}x  {1}' -f $_.Count, $_.Name.Substring(0, [Math]::Min(140, $_.Name.Length)) }
'--- sample sites:'
$all | Sort-Object Loc -Unique | Select-Object -First $Show | ForEach-Object { "$($_.Loc): $($_.E.Substring(0, [Math]::Min(130, $_.E.Length)))" }
