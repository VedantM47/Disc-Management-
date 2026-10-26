# SpaceRescue build - uses the locally installed MinGW (g++ 6.3, 32-bit). No downloads needed.
# Usage: powershell -ExecutionPolicy Bypass -File build.ps1 [-Clean] [-Debug]
param([switch]$Clean, [switch]$Debug)

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$obj = Join-Path $root 'build\obj'
$out = Join-Path $root 'build\SpaceRescue.exe'
if ($Clean -and (Test-Path $obj)) { Remove-Item -Recurse -Force $obj }
New-Item -ItemType Directory -Force $obj | Out-Null

$flags = @('-std=gnu++14', '-Wall', '-Wextra', '-Wno-unused-parameter', '-Wno-missing-field-initializers',
           '-DUNICODE', '-D_UNICODE')
$flags += $(if ($Debug) { @('-O0', '-g') } else { @('-O2', '-DNDEBUG') })

$sources = Get-ChildItem (Join-Path $root 'src') -Recurse -Filter *.cpp
$headers = Get-ChildItem (Join-Path $root 'src') -Recurse -Filter *.h
$newestHeader = ($headers | Measure-Object -Property LastWriteTime -Maximum).Maximum

$objects = @()
$failed = $false
foreach ($src in $sources) {
    $rel = $src.FullName.Substring($root.Length + 5) -replace '[\\/]', '_'
    $o = Join-Path $obj ($rel -replace '\.cpp$', '.o')
    $objects += $o
    # Rebuild when the source or any header is newer than the object.
    if ((Test-Path $o) -and (Get-Item $o).LastWriteTime -gt $src.LastWriteTime -and
        (Get-Item $o).LastWriteTime -gt $newestHeader) { continue }
    Write-Host "  CXX $($src.FullName.Substring($root.Length + 1))"
    $output = & g++ @flags -c $src.FullName -o $o 2>&1
    if ($output) { $output | ForEach-Object { "$_" } | Write-Host }
    if ($LASTEXITCODE -ne 0) { $failed = $true }
}

$rc = Join-Path $root 'res\app.rc'
if (Test-Path $rc) {
    $res = Join-Path $obj 'app_res.o'
    Write-Host "  RC  res\app.rc"
    & windres -DUNICODE -I (Join-Path $root 'res') -i $rc -o $res
    if ($LASTEXITCODE -ne 0) { $failed = $true }
    $objects += $res
}
if ($failed) { Write-Host 'BUILD FAILED' -ForegroundColor Red; exit 1 }

$libs = @('-lcomctl32', '-lshell32', '-lole32', '-loleaut32', '-luuid', '-lpsapi', '-lshlwapi',
          '-luxtheme', '-lmsimg32', '-lgdi32', '-luser32', '-ladvapi32', '-lkernel32')
Write-Host "  LD  build\SpaceRescue.exe"
# Static runtime: the exe has no DLL dependencies, so it runs from a USB stick (portable mode).
& g++ -mwindows -static -static-libgcc -static-libstdc++ -o $out @objects @libs
if ($LASTEXITCODE -ne 0) { Write-Host 'LINK FAILED' -ForegroundColor Red; exit 1 }
Write-Host ("OK  build\SpaceRescue.exe ({0:N0} KB)" -f ((Get-Item $out).Length / 1KB)) -ForegroundColor Green
