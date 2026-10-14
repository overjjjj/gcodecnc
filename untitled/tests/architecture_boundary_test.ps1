$ErrorActionPreference = "Stop"
$ProjectDir = Split-Path -Parent $PSScriptRoot
$SourceDir = Join-Path $ProjectDir "src"
$violations = [System.Collections.Generic.List[string]]::new()
$graph = @{}

# Inspect real quoted includes, including transitive dependencies. No enum-based
# declarations of ownership can substitute for this source dependency check.
foreach ($file in Get-ChildItem -LiteralPath $SourceDir -Recurse -File) {
    if ($file.Extension -notin @('.h', '.cpp')) { continue }
    $name = $file.FullName.Substring($SourceDir.Length + 1).Replace('\', '/')
    $dependencies = @()
    $source = Get-Content -LiteralPath $file.FullName -Raw
    foreach ($match in [regex]::Matches($source, '(?m)^\s*#include\s+"([^"]+)"')) {
        $target = [IO.Path]::GetFullPath((Join-Path $file.DirectoryName $match.Groups[1].Value))
        if ($target.StartsWith($SourceDir + [IO.Path]::DirectorySeparatorChar)) {
            $dependencies += $target.Substring($SourceDir.Length + 1).Replace('\', '/')
        }
    }
    $graph[$name] = $dependencies
}

$required = @(
    'app/AppController.h',
    'app/AppController.cpp',
    'core/StrategyParams.h',
    'workspaces/process_design/operations/MachiningOperation.h',
    'workspaces/process_design/operations/OperationProposal.h',
    'workspaces/process_design/operations/OperationProposal.cpp',
    'workspaces/process_design/ProcessDesignWorkspaceFacade.h',
    'workspaces/program_verification/ProgramVerificationWorkspaceFacade.h',
    'workspaces/machine_operation/MachineOperationWorkspaceFacade.h'
)
$project = Get-Content -LiteralPath (Join-Path $ProjectDir 'untitled.pro') -Raw
foreach ($path in $required) {
    if (!$graph.ContainsKey($path)) { $violations.Add("Missing module: $path") }
    if (!$project.Contains("src/$path")) { $violations.Add("Unregistered module: $path") }
}

foreach ($entry in $graph.Keys) {
    $forbidden = $null
    if ($entry -match '^(strategies|import)/') {
        $forbidden = '^(app|workspaces)/'
    } elseif ($entry -match '^workspaces/process_design/(operations|selection)/') {
        $forbidden = '^(app|strategies|postprocessor)/|^workspaces/(machine_operation|program_verification)/|/ui/'
    } elseif ($entry -match '^workspaces/shared/[^/]+\.h$') {
        $forbidden = '^(app|core|strategies|import|postprocessor)/|^workspaces/(process_design|program_verification|machine_operation)/'
    }
    if (!$forbidden) { continue }
    $pending = [System.Collections.Generic.Stack[string]]::new()
    $visited = [System.Collections.Generic.HashSet[string]]::new()
    foreach ($dependency in $graph[$entry]) { $pending.Push($dependency) }
    while ($pending.Count -gt 0) {
        $dependency = $pending.Pop()
        if (!$visited.Add($dependency)) { continue }
        if ($dependency -match $forbidden) {
            $violations.Add("Forbidden dependency: $entry -> $dependency")
        }
        if ($graph.ContainsKey($dependency)) {
            foreach ($next in $graph[$dependency]) { $pending.Push($next) }
        }
    }
}

if ($violations.Count -gt 0) {
    throw ($violations -join "`n")
}
Write-Host "PASS architecture_boundary_test ($($graph.Count) source files inspected)"
