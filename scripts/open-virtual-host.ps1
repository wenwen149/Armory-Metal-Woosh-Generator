param(
    [string]$ToolsRoot = 'D:\ArmoryVSTTools'
)

$ErrorActionPreference = 'Stop'

$hostExecutable = Join-Path $ToolsRoot 'JUCE\extras\AudioPluginHost\Builds\VisualStudio2022\x64\Release\App\AudioPluginHost.exe'
$pluginDirectory = Join-Path $ToolsRoot 'build\ArmoryVST-Release\ArmoryVST_artefacts\Release\VST3'

if (-not (Test-Path -LiteralPath $hostExecutable)) {
    throw "AudioPluginHost is not available: $hostExecutable"
}

if (-not (Test-Path -LiteralPath $pluginDirectory)) {
    throw "Build the Release VST3 first: $pluginDirectory"
}

Write-Host "Add this folder to AudioPluginHost's VST3 scan paths: $pluginDirectory"
Start-Process -FilePath $hostExecutable
