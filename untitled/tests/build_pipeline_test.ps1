$ErrorActionPreference = "Stop"

$ProjectDir = Split-Path -Parent $PSScriptRoot
$BuildScript = Join-Path $ProjectDir "scripts\build_and_test.ps1"
$QtDir = "D:\qt5\5.14.2\mingw73_64"
$MinGwDir = "D:\qt5\Tools\mingw730_64\bin"
$OccBinDir = "D:\msys64\mingw64\bin"

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

$projectText = Get-Content -LiteralPath (Join-Path $ProjectDir "untitled.pro") -Raw
$runtimeMatch = [regex]::Match(
    $projectText,
    '(?s)OCC_RUNTIME_DLLS\s*=\s*\\(.*?)\r?\n\s*\r?\n\s*for \(dll, OCC_RUNTIME_DLLS\)')
if (!$runtimeMatch.Success) {
    throw "cannot locate OCC_RUNTIME_DLLS in untitled.pro"
}
$runtimeDlls = @([regex]::Matches($runtimeMatch.Groups[1].Value, '[A-Za-z0-9+_.-]+\.dll') |
    ForEach-Object { $_.Value })
$runtimeDllSet = @{}
foreach ($dll in $runtimeDlls) { $runtimeDllSet[$dll.ToLowerInvariant()] = $true }

$ObjDump = Join-Path $MinGwDir "objdump.exe"
foreach ($dll in $runtimeDlls) {
    $dllPath = Join-Path $OccBinDir $dll
    if (!(Test-Path -LiteralPath $dllPath)) { continue }
    $imports = @(& $ObjDump -p $dllPath 2>$null |
        Select-String 'DLL Name:\s*(.+)$' |
        ForEach-Object { $_.Matches[0].Groups[1].Value.Trim() })
    foreach ($import in $imports) {
        if ((Test-Path -LiteralPath (Join-Path $OccBinDir $import)) -and
            !$runtimeDllSet.ContainsKey($import.ToLowerInvariant())) {
            throw "OCC runtime deployment is missing transitive dependency: $dll -> $import"
        }
    }
}

$runTestsText = Get-Content -LiteralPath (Join-Path $ProjectDir "scripts\run_tests.ps1") -Raw
$registeredNames = @([regex]::Matches($runTestsText, '(?m)^\s*-Name "([^"]+)"') |
    ForEach-Object { $_.Groups[1].Value })
$testFiles = @(Get-ChildItem -LiteralPath $PSScriptRoot -Filter '*_test.cpp' |
    ForEach-Object { $_.BaseName })
foreach ($testName in $testFiles) {
    if (@($registeredNames | Where-Object { $_ -eq $testName }).Count -ne 1) {
        throw "test must be registered exactly once: $testName"
    }
}
foreach ($testName in $registeredNames) {
    if ($testName -notin $testFiles) { throw "registered test source is missing: $testName" }
}
$unknownRejected = $false
try {
    & (Join-Path $ProjectDir "scripts\run_tests.ps1") -TestNames 'definitely_unknown_test'
} catch {
    if ($_.Exception.Message -notlike '*Unknown test name*definitely_unknown_test*') { throw }
    $unknownRejected = $true
}
if (!$unknownRejected) { throw "unknown test name must fail before running any tests" }

$programGenerationMatch = [regex]::Match(
    $runTestsText,
    '(?s)-Name "program_generation_service_test"\s*`\s*-Sources @\((.*?)\)')
if (!$programGenerationMatch.Success) {
    throw "cannot locate program_generation_service_test source list"
}
$programGenerationSources = $programGenerationMatch.Groups[1].Value
$programGenerationDependencies = @(
    "src\postprocessor\Cq8PostProcessor.cpp",
    "src\postprocessor\FanucPostProcessor.cpp",
    "src\gcode\Cq8MacroProgramBuilder.cpp",
    "src\gcode\GCodeModalOptimizer.cpp"
)
foreach ($source in $programGenerationDependencies) {
    if (!$programGenerationSources.Contains($source)) {
        throw "program_generation_service_test source list is missing: $source"
    }
}

foreach ($testName in @(
    "pocket_finish_strategy_test",
    "pocket_floor_finish_strategy_test",
    "acceptance_step_model_test"
)) {
    if (!$runTestsText.Contains("-Name `"$testName`"")) {
        throw "full test pipeline is missing: $testName"
    }
}

& (Join-Path $PSScriptRoot 'build_entry_forwarding_test.ps1')
Write-Host "PASS build_pipeline_test"
