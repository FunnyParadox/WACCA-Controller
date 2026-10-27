#Requires -Version 5.1
[CmdletBinding(DefaultParameterSetName = 'Update')]
param(
	[Parameter(Mandatory = $true, Position = 0)]
	[ValidateSet('touch', 'led')]
	[string]$Target,
	[Parameter(Mandatory = $true, Position = 1, ParameterSetName = 'Update')]
	[ValidatePattern('^COM[1-9][0-9]{0,3}$')]
	[string]$Port,
	[Parameter(Mandatory = $true, ParameterSetName = 'Factory')]
	[switch]$Factory,
	[Parameter(ParameterSetName = 'Factory')]
	[string]$IspTool
)
$ErrorActionPreference = 'Stop'

if ($Factory) {
	$image = Join-Path $PSScriptRoot "build/firmware/$Target.factory.hex"
	if (-not (Test-Path -LiteralPath $image -PathType Leaf)) { throw 'Build the firmware first: .\build.ps1' }
	if (-not $IspTool) {
		$IspTool = Join-Path $PSScriptRoot '.tools/wchisp/wchisp.exe'
		if (-not (Test-Path -LiteralPath $IspTool -PathType Leaf)) {
			# Pinned portable dependency; never install/change a Windows device driver.
			$toolDirectory = Split-Path -Parent $IspTool
			New-Item -ItemType Directory -Path $toolDirectory -Force | Out-Null
			$archivePath = Join-Path $toolDirectory 'wchisp.zip'
			Invoke-WebRequest -UseBasicParsing -Uri 'https://github.com/ch32-rs/wchisp/releases/download/nightly/wchisp-win-x64.zip' -OutFile $archivePath
			if ((Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash -ne 'EDD01F0EE4E7E665F65BAF30A0A9AAE4E6024B2665F1725731295A3E6D8190CA') {
				throw 'wchisp download changed; checksum verification failed. No flashing was attempted'
			}
			Add-Type -AssemblyName System.IO.Compression.FileSystem
			$archive = [System.IO.Compression.ZipFile]::OpenRead($archivePath)
			try {
				$entry = $archive.GetEntry('wchisp-win-x64/wchisp.exe')
				if (-not $entry) { throw 'wchisp.exe missing from the verified archive' }
				[System.IO.Compression.ZipFileExtensions]::ExtractToFile($entry, $IspTool, $false)
			} finally { $archive.Dispose() }
			Remove-Item -LiteralPath $archivePath
		}
	}
	if (-not (Test-Path -LiteralPath $IspTool -PathType Leaf)) { throw 'Cannot find the selected wchisp executable' }
	Write-Host "First flash / full recovery: $Target" -ForegroundColor Cyan
	Write-Host 'On that MCU: hold UPDATE (BOOT0), tap RESET, then release UPDATE. Leave the other MCU in normal mode'
	# Factory ROM cannot identify touch vs LED. Require exactly one ISP target;
	# the physical button selects the MCU, and Target selects its image.
	$probeOutput = @(& $IspTool probe)
	if ($LASTEXITCODE -ne 0) { throw 'Cannot probe WCH USB ISP. Check BOOT0, USB-C and the WinUSB driver (see README.md)' }
	$probeOutput | ForEach-Object { Write-Host $_ }
	$deviceCount = [regex]::Match(($probeOutput -join "`n"), 'Found (\d+) USB devices?\b')
	if (-not $deviceCount.Success -or [int]$deviceCount.Groups[1].Value -ne 1) {
		throw 'Exactly one MCU must be in factory USB ISP mode. No flashing was attempted'
	}
	& $IspTool info --chip CH32V203C8
	if ($LASTEXITCODE -ne 0) { throw 'The USB ISP target is not the expected CH32V203C8, or it cannot be opened' }
	& $IspTool flash $image
	if ($LASTEXITCODE -ne 0) { throw 'Factory USB flashing failed. Leave BOOT0 available and retry the complete factory image' }
	Write-Host 'Factory image written and verified. Release UPDATE and tap RESET if the application has not started' -ForegroundColor Green
	return
}

$uploader = Join-Path $PSScriptRoot 'build/host/wacca_flash.exe'
$image = Join-Path $PSScriptRoot "build/firmware/$Target.wcfu"
if (-not (Test-Path -LiteralPath $uploader -PathType Leaf) -or -not (Test-Path -LiteralPath $image -PathType Leaf)) {
	throw 'Build the firmware first: .\build.ps1'
}
& $uploader $Port $image
if ($LASTEXITCODE -ne 0) { throw 'USB update did not complete. See the updater message above.' }
