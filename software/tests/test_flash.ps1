#Requires -Version 5.1
param([Parameter(Mandatory = $true)][string]$WorkDirectory)
$ErrorActionPreference = 'Stop'
$testDirectory = Join-Path ([IO.Path]::GetFullPath($WorkDirectory)) ('flash-script-' + [guid]::NewGuid().ToString('N'))
$originalCount = $env:WACCA_FACTORY_MOCK_COUNT
$originalFailure = $env:WACCA_FACTORY_MOCK_FAILURE
$originalLog = $env:WACCA_FACTORY_MOCK_LOG
try {
	New-Item -ItemType Directory -Path (Join-Path $testDirectory 'build/firmware') -Force | Out-Null
	Copy-Item -LiteralPath (Join-Path $PSScriptRoot '../flash.ps1') -Destination (Join-Path $testDirectory 'flash.ps1')
	foreach ($target in @('touch','led')) {
		Set-Content -LiteralPath (Join-Path $testDirectory "build/firmware/$target.factory.hex") -Value ':00000001FF' -Encoding Ascii
	}
	$mockPath = Join-Path $testDirectory 'mock-isp.ps1'
	$logPath = Join-Path $testDirectory 'commands.txt'
	$mock = @(
		'$ErrorActionPreference = "Stop"',
		'Add-Content -LiteralPath $env:WACCA_FACTORY_MOCK_LOG -Value ($args -join "|")',
		'if ($args[0] -eq $env:WACCA_FACTORY_MOCK_FAILURE) { exit 7 }',
		'if ($args[0] -eq "probe") { Write-Output "[INFO] Found $env:WACCA_FACTORY_MOCK_COUNT USB devices" }',
		'exit 0'
	) -join "`r`n"
	Set-Content -LiteralPath $mockPath -Value $mock -Encoding Ascii
	$env:WACCA_FACTORY_MOCK_LOG = $logPath
	$powerShell = (Get-Command powershell.exe -CommandType Application).Source
	function Run-FactoryCase([string]$Target, [string]$Count, [string]$Failure, [bool]$Success, [bool]$Writes) {
		$env:WACCA_FACTORY_MOCK_COUNT = $Count
		$env:WACCA_FACTORY_MOCK_FAILURE = $Failure
		Set-Content -LiteralPath $logPath -Value '' -Encoding Ascii
		$ErrorActionPreference = 'Continue'
		$output = & $powerShell -NoProfile -File (Join-Path $testDirectory 'flash.ps1') $Target -Factory -IspTool $mockPath 2>&1 | Out-String
		$result = $LASTEXITCODE
		$ErrorActionPreference = 'Stop'
		if (($result -eq 0) -ne $Success) { throw "Unexpected factory result ($Target, $Count, $Failure): $output" }
		$commands = @(Get-Content -LiteralPath $logPath)
		$flashCommands = @($commands | Where-Object { $_ -like 'flash|*' })
		if (($flashCommands.Count -gt 0) -ne $Writes) { throw "Unexpected flash operation: $commands" }
		if ($Writes -and $flashCommands[0] -ne ('flash|' + (Join-Path $testDirectory "build/firmware/$Target.factory.hex"))) {
			throw 'Factory command selected the wrong image'
		}
		if ($Writes -and $commands -notcontains 'info|--chip|CH32V203C8') { throw 'Missing chip identity check' }
	}
	Run-FactoryCase 'touch' '1' '' $true $true
	Run-FactoryCase 'led' '1' '' $true $true
	Run-FactoryCase 'touch' '0' '' $false $false
	Run-FactoryCase 'touch' '2' '' $false $false
	Run-FactoryCase 'touch' '1' 'probe' $false $false
	Run-FactoryCase 'touch' '1' 'info' $false $false
	Run-FactoryCase 'touch' '1' 'flash' $false $true
	Write-Output 'Factory USB script: both images, zero/multiple devices, failed probe, wrong chip and failed flash passed; no USB hardware was accessed'
} finally {
	$env:WACCA_FACTORY_MOCK_COUNT = $originalCount
	$env:WACCA_FACTORY_MOCK_FAILURE = $originalFailure
	$env:WACCA_FACTORY_MOCK_LOG = $originalLog
	$absoluteRoot = [IO.Path]::GetFullPath($WorkDirectory).TrimEnd('\') + '\'
	$absoluteTarget = [IO.Path]::GetFullPath($testDirectory)
	if (-not $absoluteTarget.StartsWith($absoluteRoot, [StringComparison]::OrdinalIgnoreCase)) { throw 'Test cleanup path leaves its intended directory' }
	if (Test-Path -LiteralPath $absoluteTarget) { Remove-Item -LiteralPath $absoluteTarget -Recurse -Force }
}
