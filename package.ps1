# SpaceRescue packaging - only tools that ship with Windows (IExpress, Compress-Archive).
# Produces build\dist\SpaceRescue-Setup-x86.exe and build\dist\SpaceRescue-portable.zip.
# Usage: powershell -ExecutionPolicy Bypass -File package.ps1
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$exe = Join-Path $root 'build\SpaceRescue.exe'
if (-not (Test-Path $exe)) { & (Join-Path $root 'build.ps1'); if ($LASTEXITCODE) { exit 1 } }

$dist = Join-Path $root 'build\dist'
$stage = Join-Path $root 'build\stage'
foreach ($d in $dist, $stage) { if (Test-Path $d) { Remove-Item -Recurse -Force $d }; New-Item -ItemType Directory $d | Out-Null }

# Portable zip: an (empty) INI next to the exe switches on portable mode.
$portable = Join-Path $stage 'portable'
New-Item -ItemType Directory $portable | Out-Null
Copy-Item $exe $portable
Set-Content (Join-Path $portable 'SpaceRescue.ini') "[SpaceRescue]`r`n" -Encoding Unicode
Copy-Item (Join-Path $root 'LICENSE.md') $portable
if (Test-Path (Join-Path $root 'README.md')) { Copy-Item (Join-Path $root 'README.md') $portable }
Compress-Archive -Path (Join-Path $portable '*') -DestinationPath (Join-Path $dist 'SpaceRescue-portable.zip')

# Installer: IExpress self-extractor that runs "SpaceRescue.exe /install".
$setupSrc = Join-Path $stage 'setup'
New-Item -ItemType Directory $setupSrc | Out-Null
Copy-Item $exe $setupSrc
$target = Join-Path $dist 'SpaceRescue-Setup-x86.exe'
$sed = @"
[Version]
Class=IEXPRESS
SEDVersion=3
[Options]
PackagePurpose=InstallApp
ShowInstallProgramWindow=0
HideExtractAnimation=1
UseLongFileName=1
InsideCompressed=0
CAB_FixedSize=0
CAB_ResvCodeSigning=0
RebootMode=N
InstallPrompt=%InstallPrompt%
DisplayLicense=%DisplayLicense%
FinishMessage=%FinishMessage%
TargetName=%TargetName%
FriendlyName=%FriendlyName%
AppLaunched=%AppLaunched%
PostInstallCmd=%PostInstallCmd%
AdminQuietInstCmd=%AdminQuietInstCmd%
UserQuietInstCmd=%UserQuietInstCmd%
SourceFiles=SourceFiles
[Strings]
InstallPrompt=
DisplayLicense=
FinishMessage=
TargetName=$target
FriendlyName=SpaceRescue Setup
AppLaunched=SpaceRescue.exe /install
PostInstallCmd=<None>
AdminQuietInstCmd=SpaceRescue.exe /install /quiet
UserQuietInstCmd=SpaceRescue.exe /install /quiet
FILE0="SpaceRescue.exe"
[SourceFiles]
SourceFiles0=$setupSrc\
[SourceFiles0]
%FILE0%=
"@
$sedPath = Join-Path $stage 'setup.sed'
Set-Content $sedPath $sed -Encoding ASCII
& "$env:WINDIR\System32\iexpress.exe" /N /Q $sedPath | Out-Null
# IExpress returns before it finishes on some systems; wait for the output.
for ($i = 0; $i -lt 60 -and -not (Test-Path $target); $i++) { Start-Sleep -Milliseconds 500 }

Get-ChildItem $dist | ForEach-Object { "{0,-32} {1,8:N0} KB" -f $_.Name, ($_.Length / 1KB) }
