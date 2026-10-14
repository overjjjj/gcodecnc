param([string]$TestName = 'ui_suite_interaction_test', [string[]]$Arguments = @())
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$build = Join-Path $root 'outputs/ui-implementation-20260910/build'
$qt = 'D:/qt5/5.14.2/mingw73_64'
$compiler = 'D:/qt5/Tools/mingw730_64/bin/g++.exe'
$env:PATH = "$qt/bin;D:/qt5/Tools/mingw730_64/bin;D:/msys64/mingw64/bin;$env:PATH"
$includes = @("-ID:/msys64/mingw64/include/opencascade", "-I$qt/include", "-I$qt/include/QtCore", "-I$qt/include/QtGui", "-I$qt/include/QtWidgets", "-I$qt/include/QtOpenGL", "-I$qt/mkspecs/win32-g++")
$test = Join-Path $root "untitled/tests/$TestName.cpp"
$object = Join-Path $build "$TestName.o"
& $compiler -std=gnu++17 -fPIC -DCNEXT_ENABLE_OCC @includes -c $test -o $object
if ($LASTEXITCODE -ne 0) { throw 'Test compilation failed' }
$makefile = Get-Content (Join-Path $build 'Makefile.Debug') -Raw
$libs = [regex]::Match($makefile, '(?m)^LIBS\s*=\s*(.*)$').Groups[1].Value.Trim()
$objects = Get-ChildItem (Join-Path $build 'debug') -Filter '*.o' | Where-Object Name -ne 'main.o'
$response = @('"' + $object.Replace('\','/') + '"')
$response += $objects | ForEach-Object { '"' + $_.FullName.Replace('\','/') + '"' }
$response += $libs.Replace('\','/')
$rsp = Join-Path $build "$TestName.rsp"
[System.IO.File]::WriteAllLines($rsp, $response)
$exe = Join-Path $build "debug/$TestName.exe"
& $compiler "@$rsp" -o $exe
if ($LASTEXITCODE -ne 0) { throw 'Test link failed' }
& $exe @Arguments
if ($LASTEXITCODE -ne 0) { throw "Test failed: $LASTEXITCODE" }
Write-Output "PASS $TestName"




