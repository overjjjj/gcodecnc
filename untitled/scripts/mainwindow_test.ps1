# The timeout includes QApplication construction and modal dialogs.
function Invoke-TestProcess {
    param([string]$Name, [string]$Executable, [string[]]$Arguments = @())
    $logDir = Join-Path $DebugDir $Name
    New-Item -ItemType Directory -Path $logDir -Force | Out-Null
    $start = @{
        FilePath = $Executable; WorkingDirectory = $ProjectDir; PassThru = $true
        WindowStyle = 'Hidden'; RedirectStandardOutput = (Join-Path $logDir 'stdout.log')
        RedirectStandardError = (Join-Path $logDir 'stderr.log')
    }
    if ($Arguments.Count) { $start.ArgumentList = @($Arguments | ForEach-Object { '"' + $_ + '"' }) }
    $process = Start-Process @start
    try {
        # Keep the native handle available for ExitCode on Windows PowerShell 5.1.
        $null = $process.Handle
        if (!$process.WaitForExit(60000)) {
            $process.Kill()
            $process.WaitForExit()
            throw "$Name timed out after 60 seconds (PID $($process.Id))"
        }
        $exitCode = $process.ExitCode
        Get-Content -LiteralPath (Join-Path $logDir 'stdout.log')
        Get-Content -LiteralPath (Join-Path $logDir 'stderr.log')
        if ($exitCode -ne 0) { throw "$Name failed with exit code $exitCode" }
    } finally {
        if (!$process.HasExited) { $process.Kill(); $process.WaitForExit() }
        $process.Dispose()
    }
}

function Invoke-MainWindowTest {
    param([string]$Name)
    if ($TestNames.Count -gt 0 -and $Name -notin $TestNames) { return }
    if (!$BuildDir) { throw "$Name requires -BuildDir pointing to a completed fresh application build" }
    $buildRoot = [IO.Path]::GetFullPath($BuildDir)
    $makefile = Join-Path $buildRoot 'Makefile.Debug'
    if (!(Test-Path -LiteralPath $makefile)) { throw "New build Makefile.Debug is missing: $makefile" }
    $makeText = Get-Content -LiteralPath $makefile -Raw
    $responseMatches = [regex]::Matches($makeText, '(?m)^\s*\$\(LINKER\).*?@("[^"]+"|[^\s]+)')
    if ($responseMatches.Count -ne 1) { throw 'Expected exactly one application linker response file' }
    $response = Join-Path $buildRoot $responseMatches[0].Groups[1].Value.Trim('"')
    if (!(Test-Path -LiteralPath $response)) { throw "New build object response file is missing: $response" }
    $objectLines = @(Get-Content -LiteralPath $response)
    $mainLines = @($objectLines | Where-Object { $_.Trim().Trim('"') -match '(^|[\\/])main\.o$' })
    if ($mainLines.Count -ne 1) { throw 'Expected exactly one main.o in application response file' }
    foreach ($line in $objectLines) {
        $objectPath = $line.Trim().Trim('"')
        if (!$objectPath) { continue }
        if (!(Test-Path -LiteralPath (Join-Path $buildRoot $objectPath))) {
            throw "Fresh application object missing; finish the clean build first: $objectPath"
        }
    }
    $testDir = Join-Path (Join-Path $DebugDir $Name) ('run-' + [Guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $testDir -Force | Out-Null
    Write-Host "ARTIFACTS $Name $testDir"
    if (Test-Path -LiteralPath (Join-Path $testDir 'tools.json')) { throw "Isolated test directory must not contain tools.json: $testDir" }
    $testObject = (Join-Path $testDir "$Name.o").Replace('\', '/')
    $testResponse = Join-Path $testDir 'objects.rsp'
    $replacement = '"' + $testObject + '"'
    $objectLines = @($objectLines | ForEach-Object { if ($_ -eq $mainLines[0]) { $replacement } else { $_ } })
    [IO.File]::WriteAllLines($testResponse, $objectLines, [Text.UTF8Encoding]::new($false))
    $source = (Join-Path $ProjectDir "tests/$Name.cpp").Replace('\', '/')
    $executable = Join-Path $testDir "$Name.exe"
    $recipe = @'
.PHONY: b2i_mainwindow_test
b2i_mainwindow_test:
	$(CXX) -c $(CXXFLAGS) $(INCPATH) -o "@OBJECT@" "@SOURCE@"
	$(LINKER) $(LFLAGS) -o "@EXE@" @"@RESPONSE@" $(LIBS)
'@
    $recipe = $recipe.Replace('@OBJECT@', $testObject).Replace('@SOURCE@', $source).Replace('@EXE@', $executable.Replace('\', '/')).Replace('@RESPONSE@', $testResponse.Replace('\', '/'))
    $testMakefile = Join-Path $testDir 'test.mk'
    [IO.File]::WriteAllText($testMakefile, $recipe + "`n", [Text.UTF8Encoding]::new($false))
    Push-Location $buildRoot
    try {
        & (Join-Path $MinGwDir 'mingw32-make.exe') -f Makefile.Debug -f $testMakefile b2i_mainwindow_test "CXX=$($Compiler.Replace('\', '/'))" "LINKER=$($Compiler.Replace('\', '/'))"
        if ($LASTEXITCODE -ne 0) { throw "$Name build failed" }
    } finally { Pop-Location }
    Invoke-TestProcess -Name $Name -Executable $executable -Arguments @($ProjectDir, (Join-Path $testDir 'output'))
    $executedTests.Add($Name)
    Write-Host "PASS $Name"
}
