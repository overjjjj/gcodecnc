$ErrorActionPreference = "Stop"

$ProjectDir = Split-Path -Parent $PSScriptRoot
$BuildScript = Join-Path $ProjectDir "scripts\build_and_test.ps1"
$QtDir = "D:\qt5\5.14.2\mingw73_64"
$MinGwDir = "D:\qt5\Tools\mingw730_64\bin"

$output = & $BuildScript -QtDir $QtDir -MinGwDir $MinGwDir -CheckEnvironment 2>&1
$text = $output -join "`n"
$expected = @(
    (Join-Path $QtDir "bin\qmake.exe"),
    (Join-Path $MinGwDir "mingw32-make.exe"),
    (Join-Path $ProjectDir "untitled.pro"),
    (Join-Path $ProjectDir "scripts\run_tests.ps1"),
    (Join-Path $ProjectDir "tests\build_pipeline_test.ps1")
)

foreach ($path in $expected) {
    if (!$text.Contains($path)) {
        throw "Environment check output is missing: $path"
    }
}

Write-Host "PASS build_pipeline_test"
