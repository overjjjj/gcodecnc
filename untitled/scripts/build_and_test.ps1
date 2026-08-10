param(
    [string]$QtDir = "D:\qt5\5.14.2\mingw73_64",
    [string]$MinGwDir = "D:\qt5\Tools\mingw730_64\bin",
    [switch]$CheckEnvironment
)

$ErrorActionPreference = "Stop"

$ProjectDir = Split-Path -Parent $PSScriptRoot
$RepoDir = Split-Path -Parent $ProjectDir
$ProjectFile = Join-Path $ProjectDir "untitled.pro"
$TestScript = Join-Path $PSScriptRoot "run_tests.ps1"
$PipelineTest = Join-Path $ProjectDir "tests\build_pipeline_test.ps1"
$QMake = Join-Path $QtDir "bin\qmake.exe"
$Make = Join-Path $MinGwDir "mingw32-make.exe"

foreach ($path in @($QMake, $Make, $ProjectFile, $TestScript, $PipelineTest)) {
    if (!(Test-Path -LiteralPath $path)) {
        throw "Required build input not found: $path"
    }
    Write-Output $path
}

if ($CheckEnvironment) {
    return
}

$env:PATH = "$($QtDir)\bin;$MinGwDir;$env:PATH"

Push-Location $RepoDir
try {
    & $QMake -o Makefile $ProjectFile
    if ($LASTEXITCODE -ne 0) {
        throw "qmake failed with exit code $LASTEXITCODE"
    }

    & $Make -f Makefile.Debug
    if ($LASTEXITCODE -ne 0) {
        throw "Debug build failed with exit code $LASTEXITCODE"
    }

    & $TestScript -QtDir $QtDir
    & $PipelineTest
}
finally {
    Pop-Location
}
