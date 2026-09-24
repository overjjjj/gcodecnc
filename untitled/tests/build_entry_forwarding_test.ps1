param([string]$BuildScript = (Join-Path $PSScriptRoot '../scripts/build_and_test.ps1'))
$ErrorActionPreference = 'Stop'
$fixture = Join-Path $PSScriptRoot ('outputs/build-entry-' + [guid]::NewGuid().ToString('N'))
$project = Join-Path $fixture 'project'
$kit = Join-Path $fixture 'qt'
$compiler = Join-Path $fixture 'compiler'
foreach ($dir in @("$project/scripts", "$project/tests", "$kit/bin", $compiler)) {
    New-Item -ItemType Directory -Path $dir -Force | Out-Null
}
# Substitute only native executable file extensions to run shell mocks.
$scriptText = (Get-Content -LiteralPath $BuildScript -Raw).Replace('qmake.exe', 'qmake.ps1').Replace('mingw32-make.exe', 'mingw32-make.ps1')
Set-Content -LiteralPath "$project/scripts/build_and_test.ps1" -Value $scriptText
Set-Content -LiteralPath "$project/untitled.pro" -Value ''
Set-Content -LiteralPath "$project/tests/build_pipeline_test.ps1" -Value ''
Set-Content -LiteralPath "$kit/bin/qmake.ps1" -Value '(Get-Location).Path | Set-Content qmake-location.txt; $global:LASTEXITCODE = 0'
Set-Content -LiteralPath "$compiler/mingw32-make.ps1" -Value '(Get-Location).Path | Set-Content make-location.txt; $global:LASTEXITCODE = 0'
Set-Content -LiteralPath "$project/scripts/run_tests.ps1" -Value @'
param([string]$QtDir, [string]$MinGwDir, [string]$BuildDir)
@{ QtDir=$QtDir; MinGwDir=$MinGwDir; BuildDir=$BuildDir; WorkingDirectory=(Get-Location).Path } | ConvertTo-Json | Set-Content test-invocation.json
'@
foreach ($explicit in @($false, $true)) {
    $expectedRoot = if ($explicit) { Join-Path $fixture 'fresh build' } else { $fixture }
    $parameters = @{ QtDir=$kit; MinGwDir=$compiler }
    if ($explicit) { $parameters.BuildDir = $expectedRoot.Replace('\', '/') }
    $previousPath = $env:PATH
    & "$project/scripts/build_and_test.ps1" @parameters | Out-Null
    if ($env:PATH -ne $previousPath) { throw 'Build entry did not restore PATH' }
    $expectedRoot = [IO.Path]::GetFullPath($expectedRoot)
    $invocation = Get-Content -LiteralPath "$expectedRoot/test-invocation.json" -Raw | ConvertFrom-Json
    foreach ($name in @('BuildDir', 'WorkingDirectory')) {
        if ($invocation.$name -ne $expectedRoot) { throw "Build entry did not forward $name to tests" }
    }
    if ($invocation.QtDir -ne [IO.Path]::GetFullPath($kit) -or $invocation.MinGwDir -ne [IO.Path]::GetFullPath($compiler)) {
        throw 'Build entry did not forward the chosen toolchain to tests'
    }
    foreach ($name in @('qmake', 'make')) {
        if ((Get-Content -LiteralPath "$expectedRoot/$name-location.txt") -ne $expectedRoot) {
            throw "$name did not use the test build directory"
        }
    }
}
Write-Host 'PASS build_entry_forwarding_test'
