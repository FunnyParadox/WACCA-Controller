#Requires -Version 5.1
[CmdletBinding()]
param([switch]$SkipTests)
$ErrorActionPreference = 'Stop'
$projectDirectory = $PSScriptRoot
$buildDirectory = Join-Path $projectDirectory 'build'
function Find-Tool([string]$Name) {
	$found = Get-Command $Name -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
	if ($found) { return $found.Source }
	$fallback = Join-Path 'C:/msys64/ucrt64/bin' $Name
	if (Test-Path -LiteralPath $fallback -PathType Leaf) { return $fallback }
	throw "Cannot find $Name. Install 64-bit Windows GCC, CMake and Ninja, or put them on PATH."
}
$cmakeFile = Find-Tool 'cmake.exe'
$ninjaFile = Find-Tool 'ninja.exe'
$compilerFile = Find-Tool 'gcc.exe'
& $cmakeFile -S $projectDirectory -B $buildDirectory -G Ninja "-DCMAKE_MAKE_PROGRAM=$ninjaFile" "-DCMAKE_C_COMPILER=$compilerFile" -DCMAKE_BUILD_TYPE=Release
if ($LASTEXITCODE -ne 0) { throw "DLL configuration failed (exit $LASTEXITCODE)." }
& $cmakeFile --build $buildDirectory --parallel
if ($LASTEXITCODE -ne 0) { throw "DLL build failed (exit $LASTEXITCODE)." }
if (-not $SkipTests) {
	$ctestFile = Find-Tool 'ctest.exe'
	& $ctestFile --test-dir $buildDirectory --output-on-failure
	if ($LASTEXITCODE -ne 0) { throw "DLL tests failed (exit $LASTEXITCODE)." }
}
Write-Host "Ready: $(Join-Path $buildDirectory 'mercuryio.dll')" -ForegroundColor Green
Write-Host 'Copy mercuryio.example.ini to mercuryio.ini beside the DLL and set the real COM ports.'
Write-Host 'The build does not install anything into the game.'
