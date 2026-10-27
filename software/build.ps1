#Requires -Version 5.1
[CmdletBinding()]
param(
	[Parameter(Position = 0)]
	[ValidateSet('firmware', 'touch', 'led', 'tests', 'mercuryio', 'updater', 'all')]
	[string]$Target = 'firmware'
)
$ErrorActionPreference = 'Stop'
$projectDirectory = $PSScriptRoot

function Find-BuildTool([string]$Name, [string]$Fallback) {
	$found = Get-Command $Name -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
	if ($found) { return $found.Source }
	if (Test-Path -LiteralPath $Fallback -PathType Leaf) { return $Fallback }
	throw "Cannot find $Name. Install CMake, Ninja and GCC, or put them on PATH"
}

$cmakeFile = Find-BuildTool 'cmake.exe' 'C:/msys64/ucrt64/bin/cmake.exe'
$ninjaFile = Find-BuildTool 'ninja.exe' 'C:/msys64/ucrt64/bin/ninja.exe'

function Invoke-CMake([string[]]$CmakeArguments) {
	& $cmakeFile @CmakeArguments
	if ($LASTEXITCODE -ne 0) { throw "CMake failed (exit $LASTEXITCODE)" }
}

if ($Target -notin @('tests', 'mercuryio', 'updater')) {
	$compilerFile = Join-Path $projectDirectory '.tools/risc-none-embed-gcc-8.2.0/bin/riscv-none-embed-gcc.exe'
	if (-not (Test-Path -LiteralPath $compilerFile -PathType Leaf)) {
		throw 'WCH compiler missing: restore software/.tools/risc-none-embed-gcc-8.2.0'
	}
	$buildDirectory = Join-Path $projectDirectory 'build/firmware'
	$toolchainFile = Join-Path $projectDirectory 'cmake/wch-gcc.cmake'
	Write-Host 'Building MCU firmware...'
	$firmwareArguments = @('-S', $projectDirectory, '-B', $buildDirectory, '-G', 'Ninja',
		"-DCMAKE_MAKE_PROGRAM=$ninjaFile", '-DCMAKE_BUILD_TYPE=MinSizeRel')
	# CMake remembers the compiler after the first configuration.
	if (-not (Test-Path -LiteralPath (Join-Path $buildDirectory 'CMakeCache.txt'))) {
		$firmwareArguments += "-DCMAKE_TOOLCHAIN_FILE=$toolchainFile"
	}
	Invoke-CMake $firmwareArguments
	$programs = if ($Target -eq 'touch' -or $Target -eq 'led') { @($Target) } else { @('touch', 'led') }
	Invoke-CMake (@('--build', $buildDirectory, '--parallel', '--target') + $programs)
	foreach ($program in $programs) {
		foreach ($suffix in @('factory.hex', 'wcfu')) {
			$image = Join-Path $buildDirectory "$program.$suffix"
			if (-not (Test-Path -LiteralPath $image -PathType Leaf)) { throw "Build did not produce $image" }
			Write-Host "Ready: $image" -ForegroundColor Green
		}
	}
	Write-Host 'Building does not flash either MCU'
}

if ($Target -ne 'mercuryio') {
	$hostCompiler = Find-BuildTool 'gcc.exe' 'C:/msys64/ucrt64/bin/gcc.exe'
	$ctestFile = Find-BuildTool 'ctest.exe' (Join-Path (Split-Path $cmakeFile) 'ctest.exe')
	$buildDirectory = Join-Path $projectDirectory 'build/host'
	Write-Host 'Building the PC USB updater...'
	Invoke-CMake @('-S', $projectDirectory, '-B', $buildDirectory, '-G', 'Ninja',
		"-DCMAKE_MAKE_PROGRAM=$ninjaFile", "-DCMAKE_C_COMPILER=$hostCompiler",
		'-DCMAKE_BUILD_TYPE=Debug')
	if ($Target -eq 'tests' -or $Target -eq 'all') {
		Invoke-CMake @('--build', $buildDirectory, '--parallel')
		& $ctestFile --test-dir $buildDirectory --output-on-failure
		if ($LASTEXITCODE -ne 0) { throw "Tests failed (exit $LASTEXITCODE)" }
	} else {
		Invoke-CMake @('--build', $buildDirectory, '--parallel', '--target', 'wacca_flash')
	}
	Write-Host "USB updater: $(Join-Path $buildDirectory 'wacca_flash.exe')" -ForegroundColor Green
}

if ($Target -eq 'mercuryio') {
	& (Join-Path $projectDirectory 'mercuryio/build.ps1')
}
