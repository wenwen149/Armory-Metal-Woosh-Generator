param(
    [string]$ToolsRoot = 'D:\ArmoryVSTTools'
)

$ErrorActionPreference = 'Stop'

$executable = Join-Path $ToolsRoot 'build\ArmoryVST\ArmoryVST_artefacts\Debug\Standalone\Armory VST.exe'

if (-not (Test-Path -LiteralPath $executable)) {
    throw "Build the Debug target first: $executable"
}

Start-Process -FilePath $executable
