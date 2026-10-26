# SpaceRescue build - llvm-mingw clang (x86_64, C++23), per-user install, no Visual Studio, no admin.
#   powershell -ExecutionPolicy Bypass -File build.ps1            # default: -O2, incremental, parallel
#   build.ps1 -Fast       # -O0, quickest compile for UI iteration
#   build.ps1 -Release    # -O2 + LTO-free size tweaks, stripped
#   build.ps1 -Clean      # wipe objects first
#   build.ps1 -Gcc        # legacy MinGW.org GCC 6.3 (32-bit, gnu++14) if clang is unavailable
param([switch]$Clean, [switch]$Fast, [switch]$Release, [switch]$Gcc, [int]$Jobs = [Environment]::ProcessorCount)

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot

function Find-Clang {
    $cmd = Get-Command x86_64-w64-mingw32-clang++ -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    $pkg = Join-Path $env:LOCALAPPDATA 'Microsoft\WinGet\Packages'
    $hit = Get-ChildItem $pkg -Directory -Filter 'MartinStorsjo.LLVM-MinGW*' -ErrorAction SilentlyContinue |
        ForEach-Object { Get-ChildItem $_.FullName -Recurse -Filter 'x86_64-w64-mingw32-clang++.exe' -ErrorAction SilentlyContinue } |
        Select-Object -First 1
    if ($hit) { return $hit.FullName }
    return $null
}

if ($Gcc) {
    $cxx = 'g++'; $rc = 'windres'; $tag = 'gcc'
    $std = '-std=gnu++14'
} else {
    $cxx = Find-Clang
    if (-not $cxx) { Write-Host 'llvm-mingw not found. Install: winget install MartinStorsjo.LLVM-MinGW.UCRT (or use -Gcc)' -ForegroundColor Red; exit 1 }
    $rc = Join-Path (Split-Path $cxx) 'x86_64-w64-mingw32-windres.exe'
    $tag = 'clang'
    $std = '-std=c++23'
}

$mode = if ($Fast) { 'fast' } elseif ($Release) { 'release' } else { 'default' }
$obj = Join-Path $root "build\obj-$tag-$mode"
$out = Join-Path $root 'build\SpaceRescue.exe'
if ($Clean -and (Test-Path $obj)) { Remove-Item -Recurse -Force $obj }
New-Item -ItemType Directory -Force $obj | Out-Null

$flags = @($std, '-Wall', '-Wextra', '-Wno-unused-parameter', '-Wno-missing-field-initializers',
           '-DUNICODE', '-D_UNICODE', '-MMD')
if ($tag -eq 'clang') { $flags += @('-Wno-unknown-pragmas', '-fno-ms-extensions') }
$flags += $(if ($Fast) { @('-O0', '-g') } elseif ($Release) { @('-O2', '-DNDEBUG', '-ffunction-sections', '-fdata-sections') } else { @('-O2', '-DNDEBUG') })

# Incremental: a source is rebuilt when it or any header listed in its .d dependency file is newer than its object.
function Test-Stale([string]$src, [string]$o) {
    if (-not (Test-Path $o)) { return $true }
    $ot = (Get-Item $o).LastWriteTimeUtc
    if ((Get-Item $src).LastWriteTimeUtc -gt $ot) { return $true }
    $d = [IO.Path]::ChangeExtension($o, '.d')
    if (-not (Test-Path $d)) { return $true }
    $deps = (Get-Content $d -Raw) -replace '\\\r?\n', ' ' -split '\s+' | Where-Object { $_ -match '\.(h|hpp)$' }
    foreach ($dep in $deps) {
        $p = $dep -replace '\\ ', ' '
        if ((Test-Path $p) -and (Get-Item $p).LastWriteTimeUtc -gt $ot) { return $true }
    }
    return $false
}

$sources = Get-ChildItem (Join-Path $root 'src') -Recurse -Filter *.cpp
$objects = @(); $todo = @()
foreach ($src in $sources) {
    $rel = $src.FullName.Substring($root.Length + 5) -replace '[\\/]', '_'
    $o = Join-Path $obj ($rel -replace '\.cpp$', '.o')
    $objects += $o
    if (Test-Stale $src.FullName $o) { $todo += [pscustomobject]@{ Src = $src.FullName; Obj = $o } }
}

# Parallel compile: up to $Jobs compiler processes at once.
$failed = $false
$running = @()
$queue = [System.Collections.Queue]::new($todo)
$sw = [Diagnostics.Stopwatch]::StartNew()
while ($queue.Count -gt 0 -or $running.Count -gt 0) {
    while ($queue.Count -gt 0 -and $running.Count -lt $Jobs) {
        $t = $queue.Dequeue()
        Write-Host "  CXX $($t.Src.Substring($root.Length + 1))"
        $psi = [Diagnostics.ProcessStartInfo]::new($cxx)
        $psi.Arguments = (($flags + @('-c', "`"$($t.Src)`"", '-o', "`"$($t.Obj)`"")) -join ' ')
        $psi.UseShellExecute = $false; $psi.RedirectStandardError = $true; $psi.RedirectStandardOutput = $true
        $psi.CreateNoWindow = $true
        $p = [Diagnostics.Process]::Start($psi)
        $running += [pscustomobject]@{ P = $p; Err = $p.StandardError.ReadToEndAsync(); T = $t }
    }
    Start-Sleep -Milliseconds 30
    $still = @()
    foreach ($r in $running) {
        if ($r.P.HasExited) {
            $text = $r.Err.Result
            if ($text) { Write-Host $text.TrimEnd() }
            if ($r.P.ExitCode -ne 0) { $failed = $true; if (Test-Path $r.T.Obj) { Remove-Item $r.T.Obj } }
        } else { $still += $r }
    }
    $running = $still
}
if ($todo.Count) { Write-Host ("  compiled {0} file(s) in {1:N1} s" -f $todo.Count, $sw.Elapsed.TotalSeconds) }

$rcFile = Join-Path $root 'res\app.rc'
if (Test-Path $rcFile) {
    $res = Join-Path $obj 'app_res.o'
    $rcInputs = Get-ChildItem (Join-Path $root 'res') -File | Where-Object { $_.Extension -in '.rc', '.h', '.ico', '.manifest' }
    $rcNewest = ($rcInputs | Measure-Object -Property LastWriteTimeUtc -Maximum).Maximum
    if (-not (Test-Path $res) -or (Get-Item $res).LastWriteTimeUtc -lt $rcNewest) {
        Write-Host '  RC  res\app.rc'
        & $rc -DUNICODE -I (Join-Path $root 'res') -i $rcFile -o $res
        if ($LASTEXITCODE -ne 0) { $failed = $true }
    }
    $objects += $res
}
if ($failed) { Write-Host 'BUILD FAILED' -ForegroundColor Red; exit 1 }

$libs = @('-lcomctl32', '-lshell32', '-lole32', '-loleaut32', '-luuid', '-lpsapi', '-lshlwapi',
          '-luxtheme', '-lmsimg32', '-lgdi32', '-luser32', '-ladvapi32', '-lkernel32')
if ($tag -eq 'clang') { $libs += @('-ld2d1', '-ldwrite', '-ldwmapi', '-lwindowscodecs') }
$ldflags = @('-mwindows', '-static')
if ($tag -eq 'gcc') { $ldflags += @('-static-libgcc', '-static-libstdc++') }
if ($Release) { $ldflags += @('-s', '-Wl,--gc-sections') }

Write-Host "  LD  build\SpaceRescue.exe ($tag, $mode)"
& $cxx @ldflags -o $out @objects @libs
if ($LASTEXITCODE -ne 0) { Write-Host 'LINK FAILED' -ForegroundColor Red; exit 1 }
Write-Host ("OK  build\SpaceRescue.exe ({0:N0} KB) in {1:N1} s" -f ((Get-Item $out).Length / 1KB), $sw.Elapsed.TotalSeconds) -ForegroundColor Green
