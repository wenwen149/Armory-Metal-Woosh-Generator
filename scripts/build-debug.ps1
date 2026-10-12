param(
    [string]$ToolsRoot = 'D:\ArmoryVSTTools'
)

$ErrorActionPreference = 'Stop'

$sourceRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$buildRoot = Join-Path $ToolsRoot 'build\ArmoryVST'
$linkParent = Join-Path $ToolsRoot 'work'
$sourceLink = Join-Path $linkParent 'ArmoryVST'
$devCmd = Join-Path $ToolsRoot 'BuildTools\Common7\Tools\VsDevCmd.bat'
$cmake = Join-Path $ToolsRoot 'CMake\bin\cmake.exe'
$ninja = Join-Path $ToolsRoot 'Ninja\ninja.exe'

foreach ($path in @($devCmd, $cmake, $ninja)) {
    if (-not (Test-Path -LiteralPath $path)) {
        throw "Missing build tool: $path"
    }
}

if (-not (Test-Path -LiteralPath $linkParent)) {
    New-Item -ItemType Directory -Path $linkParent | Out-Null
}

if (-not (Test-Path -LiteralPath $sourceLink)) {
    $mklink = 'mklink /J "' + $sourceLink + '" "' + $sourceRoot + '"'
    cmd.exe /d /c $mklink | Out-Host
    if ($LASTEXITCODE -ne 0) {
        throw 'Unable to create the ASCII build entry point.'
    }
}

$configure = 'call "' + $devCmd + '" -arch=x64 -host_arch=x64 >nul && "' + $cmake + '" -S "' + $sourceLink + '" -B "' + $buildRoot + '" -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_MAKE_PROGRAM="' + $ninja + '"'
cmd.exe /d /c $configure | Out-Host
if ($LASTEXITCODE -ne 0) {
    throw 'CMake configuration failed.'
}

$build = 'call "' + $devCmd + '" -arch=x64 -host_arch=x64 >nul && "' + $cmake + '" --build "' + $buildRoot + '" --parallel 2'
cmd.exe /d /c $build | Out-Host
if ($LASTEXITCODE -ne 0) {
    throw 'VST3 build failed.'
}

Write-Host "Output: $buildRoot\ArmoryVST_artefacts\Debug\VST3"
