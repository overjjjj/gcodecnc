param(
    [string]$QtDir = "D:\qt5\5.14.2\mingw73_64",
    [string]$OccPrefix = "D:\msys64\mingw64",
    [string[]]$TestNames = @(),
    [string]$BuildDir = "",
    [string]$MinGwDir = "D:\qt5\Tools\mingw730_64\bin"
)

$ErrorActionPreference = "Stop"
$QtDir = [IO.Path]::GetFullPath($QtDir)
$MinGwDir = [IO.Path]::GetFullPath($MinGwDir)
$OccPrefix = [IO.Path]::GetFullPath($OccPrefix)
if ($BuildDir) { $BuildDir = [IO.Path]::GetFullPath($BuildDir) }

$ProjectDir = Split-Path -Parent $PSScriptRoot
$registeredNames = @([regex]::Matches((Get-Content -LiteralPath $PSCommandPath -Raw),
    '(?m)^\s*-Name "([^"]+)"') | ForEach-Object { $_.Groups[1].Value })
foreach ($testName in $TestNames) {
    if ($testName -notin $registeredNames) { throw "Unknown test name: $testName" }
}
$executedTests = [System.Collections.Generic.List[string]]::new()
$DebugDir = if ($BuildDir) { Join-Path ([IO.Path]::GetFullPath($BuildDir)) "tests" } else { Join-Path $ProjectDir "debug" }
$Compiler = Join-Path $MinGwDir "g++.exe"
if (!(Test-Path -LiteralPath $Compiler)) { throw "Compiler not found: $Compiler" }
$QtBin = Join-Path $QtDir "bin"
$QtInclude = Join-Path $QtDir "include"
$QtLib = Join-Path $QtDir "lib"
$Mkspecs = Join-Path $QtDir "mkspecs\win32-g++"

. (Join-Path $PSScriptRoot "mainwindow_test.ps1")

function Resolve-MigratedSourcePath {
    param([string]$Path)
    $map = @{
        "src\services\ProgramGenerationService.cpp" = "src\workspaces\program_verification\generation\ProgramGenerationService.cpp"
        "src\simulation\SimulationController.cpp" = "src\workspaces\program_verification\simulation\SimulationController.cpp"
        "src\gcode\GCodeSafetyValidator.cpp" = "src\workspaces\program_verification\safety\GCodeSafetyValidator.cpp"
        "src\gcode\ProgramSnapshotFingerprint.cpp" = "src\workspaces\program_verification\fingerprint\ProgramSnapshotFingerprint.cpp"
    }
    if ($map.ContainsKey($Path)) { return $map[$Path] }
    return $Path
}

if (!(Test-Path $DebugDir)) {
    New-Item -ItemType Directory -Path $DebugDir | Out-Null
}

function Invoke-TestBuild {
    param(
        [string]$Name,
        [string[]]$Sources,
        [string[]]$Libs,
        [switch]$NeedsGui,
        [string[]]$Arguments = @()
    )

    if ($TestNames.Count -gt 0 -and $Name -notin $TestNames) { return }
    $Sources = @($Sources | ForEach-Object { Resolve-MigratedSourcePath $_ })
    $Output = Join-Path $DebugDir "$Name.exe"
    $Includes = @(
        "-I.",
        "-I$QtInclude",
        "-I$(Join-Path $QtInclude 'QtCore')",
        "-I$Mkspecs"
    )
    if ($NeedsGui) {
        $Includes += "-I$(Join-Path $QtInclude 'QtGui')"
        $Includes += "-I$(Join-Path $QtInclude 'QtWidgets')"
    }

    $LibArgs = @("-L$QtLib") + $Libs
    & $Compiler -std=gnu++17 @Includes @Sources @LibArgs -o $Output
    if ($LASTEXITCODE -ne 0) {
        throw "$Name build failed"
    }

    $testPreviousPath = $env:PATH
    try {
        # These tests use the Qt kit without the application's OCC runtime.
        $env:PATH = "$MinGwDir;$QtBin;$PreviousPath"
        Invoke-TestProcess -Name $Name -Executable $Output -Arguments $Arguments
    } finally {
        $env:PATH = $testPreviousPath
    }
    $executedTests.Add($Name)
    Write-Host "PASS $Name"
}

function Invoke-OccFixtureTest {
    param(
        [string]$Name,
        [string]$TestSource,
        [string[]]$ExtraSources = @()
    )

    if ($TestNames.Count -gt 0 -and $Name -notin $TestNames) { return }
    $ExtraSources = @($ExtraSources | ForEach-Object { Resolve-MigratedSourcePath $_ })
    $OccInclude = Join-Path $OccPrefix "include\opencascade"
    $OccLib = Join-Path $OccPrefix "lib"
    $OccBin = Join-Path $OccPrefix "bin"
    $StepLibrary = Join-Path $OccLib "libTKDESTEP.dll.a"
    foreach ($path in @($OccInclude, $StepLibrary)) {
        if (!(Test-Path -LiteralPath $path)) {
            throw "OpenCASCADE fixture-test dependency not found: $path"
        }
    }

    $Output = Join-Path $DebugDir "$Name.exe"
    $Includes = @(
        "-I.",
        "-I$OccInclude",
        "-I$QtInclude",
        "-I$(Join-Path $QtInclude 'QtCore')",
        "-I$(Join-Path $QtInclude 'QtGui')",
        "-I$Mkspecs"
    )
    $Sources = @(
        $TestSource,
        "src\import\StepImporter.cpp",
        "src\import\TopoAnalyzer.cpp",
        "src\import\FeatureRecognizer.cpp",
        "src\import\FeatureClassifier.cpp"
    ) + $ExtraSources
    $OccNames = @(
        "TKDESTEP", "TKDE", "TKXSBase", "TKXml", "TKXmlL", "TKCDF",
        "TKMesh", "TKTopAlgo", "TKGeomAlgo", "TKShHealing", "TKBool",
        "TKPrim", "TKBO", "TKBRep", "TKGeomBase", "TKG3d", "TKG2d",
        "TKMath", "TKernel"
    )
    $OccLibraries = @()
    foreach ($occName in $OccNames) {
        $OccLibraries += Join-Path $OccLib "lib$occName.dll.a"
    }

    & $Compiler -std=gnu++17 -DCNEXT_ENABLE_OCC @Includes @Sources @OccLibraries `
        "-L$OccLib" -lfreetype -ltbb12 -ltbbmalloc -lz `
        "-L$QtLib" -lQt5Core -lQt5Gui -o $Output
    if ($LASTEXITCODE -ne 0) {
        throw "$Name build failed"
    }

    $env:PATH = "$OccBin;$env:PATH"
    Invoke-TestProcess -Name $Name -Executable $Output -Arguments @($ProjectDir)
    $executedTests.Add($Name)
    Write-Host "PASS $Name"
}

Push-Location $ProjectDir
$PreviousPath = $env:PATH
$PreviousQtPlatform = $env:QT_QPA_PLATFORM
$PreviousQtPluginPath = $env:QT_PLUGIN_PATH
$PreviousQtPlatformPluginPath = $env:QT_QPA_PLATFORM_PLUGIN_PATH
try {
    $runtimeDir = if ($BuildDir) { Join-Path $BuildDir "debug" } else { $DebugDir }
    $env:PATH = "$runtimeDir;$MinGwDir;$QtBin;$(Join-Path $OccPrefix 'bin');$PreviousPath"
    $env:QT_QPA_PLATFORM = "offscreen"
    & (Join-Path $ProjectDir "tests\architecture_boundary_test.ps1")
    # GUI 测试使用所选 Qt kit 的插件，避免加载其他 Qt 安装中的 offscreen 插件。
    $env:QT_PLUGIN_PATH = Join-Path $QtDir "plugins"
    $env:QT_QPA_PLATFORM_PLUGIN_PATH = Join-Path $QtDir "plugins\platforms"
    Invoke-TestBuild `
        -Name "slot_b2v_ramp_test" `
        -Sources @("tests/slot_b2v_ramp_test.cpp", "src/strategies/mill/BlindSlotMillingStrategy.cpp", "src/postprocessor/Cq8PostProcessor.cpp", "src/postprocessor/FanucPostProcessor.cpp", "src/postprocessor/PostProcessorBase.cpp") `
        -Libs @("-lQt5Core", "-lQt5Gui") `
        -NeedsGui `
        -Arguments @((Join-Path $DebugDir "slot_b2v_ramp_test/output"))

    Invoke-MainWindowTest `
        -Name "slot_b2v_startup_test"
    Invoke-MainWindowTest `
        -Name "slot_output_gate_mainwindow_test"

    Invoke-TestBuild `
        -Name "cq8_macro_program_builder_test" `
        -Sources @("tests\cq8_macro_program_builder_test.cpp", "src\gcode\Cq8MacroProgramBuilder.cpp") `
        -Libs @("-lQt5Core")

    Invoke-TestBuild `
        -Name "gcode_modal_optimizer_test" `
        -Sources @("tests\gcode_modal_optimizer_test.cpp", "src\gcode\GCodeModalOptimizer.cpp") `
        -Libs @("-lQt5Core")

    Invoke-TestBuild `
        -Name "selection_chain_wiring_test" `
        -Sources @("tests\selection_chain_wiring_test.cpp") `
        -Libs @("-lQt5Core")

    Invoke-TestBuild `
        -Name "milling_batch_commit_test" `
        -Sources @("tests\milling_batch_commit_test.cpp", "src\app\MillingBatchCommit.cpp", "src\workspaces\process_design\operations\OperationProposal.cpp", "src\workspaces\process_design\selection\FeatureIdentity.cpp", "src\workspaces\process_design\selection\SelectionChainController.cpp", "src\strategies\mill\OpenContourMillingStrategy.cpp") `
        -Libs @("-lQt5Core", "-lQt5Gui") `
        -NeedsGui

    Invoke-TestBuild `
        -Name "selection_chain_dialog_test" `
        -Sources @("tests\selection_chain_dialog_test.cpp", "src\workspaces\process_design\ui\SelectionChainDialog.cpp", "src\workspaces\process_design\selection\SelectionChainController.cpp") `
        -Libs @("-lQt5Widgets", "-lQt5Gui", "-lQt5Core") `
        -NeedsGui

    Invoke-TestBuild `
        -Name "app_workspace_router_test" `
        -Sources @("tests\app_workspace_router_test.cpp", "src\app\controllers\AppWorkspaceRouter.cpp") `
        -Libs @("-lQt5Widgets", "-lQt5Gui", "-lQt5Core") `
        -NeedsGui

    Invoke-TestBuild `
        -Name "project_command_controller_test" `
        -Sources @("tests\project_command_controller_test.cpp", "src\app\controllers\ProjectCommandController.cpp") `
        -Libs @("-lQt5Core")

    Invoke-TestBuild `
        -Name "workspace_module_contract_test" `
        -Sources @("tests\workspace_module_contract_test.cpp") `
        -Libs @()

    Invoke-TestBuild `
        -Name "workspace_action_contract_test" `
        -Sources @("tests\workspace_action_contract_test.cpp") `
        -Libs @()

    Invoke-TestBuild `
        -Name "workspace_facade_contract_test" `
        -Sources @("tests\workspace_facade_contract_test.cpp") `
        -Libs @("-lQt5Core", "-lQt5Gui") `
        -NeedsGui

    Invoke-TestBuild `
        -Name "standalone_ui_contract_test" `
        -Sources @("tests\standalone_ui_contract_test.cpp") `
        -Libs @("-lQt5Core")

    Invoke-TestBuild `
        -Name "operation_list_panel_workflow_test" `
        -Sources @("tests\operation_list_panel_workflow_test.cpp") `
        -Libs @("-lQt5Core")

    Invoke-TestBuild `
        -Name "operation_lifecycle_wiring_test" `
        -Sources @("tests\operation_lifecycle_wiring_test.cpp") `
        -Libs @("-lQt5Core")

    Invoke-TestBuild `
        -Name "work_offset_parameter_wiring_test" `
        -Sources @("tests\work_offset_parameter_wiring_test.cpp") `
        -Libs @("-lQt5Core")

    Invoke-TestBuild `
        -Name "operation_workflow_model_test" `
        -Sources @("tests\operation_workflow_model_test.cpp", "src\workspaces\process_design\selection\FeatureIdentity.cpp", "src\workspaces\process_design\selection\HoleSelectionSession.cpp") `
        -Libs @("-lQt5Core", "-lQt5Gui") `
        -NeedsGui

    Invoke-TestBuild `
        -Name "selection_chain_controller_test" `
        -Sources @("tests\selection_chain_controller_test.cpp", "src\workspaces\process_design\selection\SelectionChainController.cpp") `
        -Libs @("-lQt5Core", "-lQt5Gui") `
        -NeedsGui

    Invoke-TestBuild `
        -Name "hole_selection_dialog_test" `
        -Sources @("tests\hole_selection_dialog_test.cpp", "src\workspaces\process_design\ui\HoleSelectionDialog.cpp", "src\workspaces\process_design\selection\HoleSelectionSession.cpp") `
        -Libs @("-lQt5Core", "-lQt5Gui", "-lQt5Widgets") `
        -NeedsGui

    Invoke-TestBuild `
        -Name "hole_selection_dialog_wiring_test" `
        -Sources @("tests\hole_selection_dialog_wiring_test.cpp") `
        -Libs @("-lQt5Core")

    Invoke-TestBuild `
        -Name "feature_list_filter_test" `
        -Sources @("tests\feature_list_filter_test.cpp", "src\workspaces\process_design\ui\FeatureDisplayFilter.cpp") `
        -Libs @("-lQt5Core", "-lQt5Gui") `
        -NeedsGui

    Invoke-TestBuild `
        -Name "hole_feature_grouping_test" `
        -Sources @("tests\hole_feature_grouping_test.cpp", "src\workspaces\process_design\ui\HoleFeatureGrouping.cpp") `
        -Libs @("-lQt5Core", "-lQt5Gui") `
        -NeedsGui

    Invoke-TestBuild `
        -Name "setup_origin_test" `
        -Sources @("tests\setup_origin_test.cpp", "src\core\SetupOrigin.cpp") `
        -Libs @("-lQt5Core", "-lQt5Gui") `
        -NeedsGui

    Invoke-TestBuild `
        -Name "stock_definition_test" `
        -Sources @("tests\stock_definition_test.cpp", "src\core\StockDefinition.cpp") `
        -Libs @("-lQt5Core", "-lQt5Gui") `
        -NeedsGui

    Invoke-TestBuild `
        -Name "gcode_safety_validator_test" `
        -Sources @("tests\gcode_safety_validator_test.cpp", "src\workspaces\program_verification\safety\GCodeSafetyValidator.cpp") `
        -Libs @("-lQt5Core")

    Invoke-TestBuild `
        -Name "cutter_compensation_strategy_test" `
        -Sources @("tests\cutter_compensation_strategy_test.cpp", "src\gcode\GCodeSafetyValidator.cpp", "src\strategies\mill\CircleMillingStrategy.cpp", "src\strategies\mill\ContourFinishStrategy.cpp", "src\strategies\mill\OpenContourMillingStrategy.cpp", "src\strategies\mill\ClosedContourMillingStrategy.cpp", "src\strategies\mill\SlotMillingStrategy.cpp", "src\strategies\mill\SlotMachiningGeometry.cpp", "src\strategies\mill\BlindSlotMillingStrategy.cpp") `
        -Libs @("-lQt5Core", "-lQt5Gui") `
        -NeedsGui

    Invoke-TestBuild `
        -Name "contour_feature_grouping_test" `
        -Sources @("tests\contour_feature_grouping_test.cpp", "src\workspaces\process_design\ui\ContourFeatureGrouping.cpp") `
        -Libs @("-lQt5Core", "-lQt5Gui") `
        -NeedsGui

    Invoke-TestBuild `
        -Name "contour_machining_choice_test" `
        -Sources @("tests\contour_machining_choice_test.cpp", "src\workspaces\process_design\ui\ContourMachiningChoice.cpp") `
        -Libs @("-lQt5Core", "-lQt5Gui") `
        -NeedsGui

    Invoke-TestBuild `
        -Name "tool_operation_compatibility_test" `
        -Sources @("tests\tool_operation_compatibility_test.cpp", "src\workspaces\process_design\ui\ToolOperationCompatibility.cpp") `
        -Libs @("-lQt5Core", "-lQt5Gui") `
        -NeedsGui

    Invoke-TestBuild `
        -Name "pocket_roughing_strategy_test" `
        -Sources @("tests\pocket_roughing_strategy_test.cpp", "src\strategies\mill\PocketRoughingStrategy.cpp") `
        -Libs @("-lQt5Core", "-lQt5Gui") `
        -NeedsGui

    Invoke-TestBuild `
        -Name "pocket_finish_strategy_test" `
        -Sources @("tests\pocket_finish_strategy_test.cpp", "src\strategies\StrategyBase.cpp", "src\strategies\mill\PocketFinishStrategy.cpp", "src\gcode\GCodeSafetyValidator.cpp") `
        -Libs @("-lQt5Core", "-lQt5Gui") `
        -NeedsGui

    Invoke-TestBuild `
        -Name "pocket_floor_finish_strategy_test" `
        -Sources @("tests\pocket_floor_finish_strategy_test.cpp", "src\strategies\StrategyBase.cpp", "src\strategies\mill\PocketFloorFinishStrategy.cpp", "src\gcode\GCodeSafetyValidator.cpp") `
        -Libs @("-lQt5Core", "-lQt5Gui") `
        -NeedsGui

    Invoke-TestBuild `
        -Name "postprocessor_header_test" `
        -Sources @("tests\postprocessor_header_test.cpp", "src\postprocessor\PostProcessorBase.cpp", "src\postprocessor\SiemensPostProcessor.cpp", "src\postprocessor\FanucPostProcessor.cpp") `
        -Libs @("-lQt5Core")

    Invoke-TestBuild `
        -Name "siemens_program_package_test" `
        -Sources @("tests\siemens_program_package_test.cpp", "src\gcode\SiemensProgramPackage.cpp") `
        -Libs @("-lQt5Core")

    Invoke-TestBuild `
        -Name "program_package_exporter_test" `
        -Sources @("tests\program_package_exporter_test.cpp", "src\gcode\ProgramPackageExporter.cpp") `
        -Libs @("-lQt5Core", "-lQt5Gui") `
        -NeedsGui

    Invoke-TestBuild `
        -Name "program_snapshot_fingerprint_test" `
        -Sources @("tests\program_snapshot_fingerprint_test.cpp", "src\workspaces\program_verification\fingerprint\ProgramSnapshotFingerprint.cpp", "src\import\StepImporter.cpp") `
        -Libs @("-lQt5Core", "-lQt5Gui") `
        -NeedsGui

    Invoke-TestBuild `
        -Name "program_snapshot_status_test" `
        -Sources @("tests\program_snapshot_status_test.cpp", "src\gcode\ProgramSnapshotStatus.cpp", "src\gcode\ProgramSnapshotFingerprint.cpp", "src\import\StepImporter.cpp") `
        -Libs @("-lQt5Core", "-lQt5Gui") `
        -NeedsGui

    Invoke-TestBuild `
        -Name "source_file_fingerprint_test" `
        -Sources @("tests\source_file_fingerprint_test.cpp", "src\core\SourceFileFingerprint.cpp") `
        -Libs @("-lQt5Core")

    Invoke-TestBuild `
        -Name "setup_orientation_test" `
        -Sources @("tests\setup_orientation_test.cpp", "src\core\SetupOrientation.cpp") `
        -Libs @("-lQt5Core", "-lQt5Gui") `
        -NeedsGui

    Invoke-TestBuild `
        -Name "operation_proposal_confirmation_test" `
        -Sources @("tests\operation_proposal_confirmation_test.cpp", "src\workspaces\process_design\operations\OperationProposal.cpp", "src\workspaces\process_design\selection\FeatureIdentity.cpp", "src\workspaces\process_design\selection\SelectionChainController.cpp") `
        -Libs @("-lQt5Core", "-lQt5Gui") `
        -NeedsGui

    Invoke-TestBuild `
        -Name "program_generation_service_test" `
        -Sources @("tests\program_generation_service_test.cpp", "src\workspaces\program_verification\generation\ProgramGenerationService.cpp", "src\strategies\hole\PeckDrillingStrategy.cpp", "src\workspaces\program_verification\safety\GCodeSafetyValidator.cpp", "src\gcode\GCodeModalOptimizer.cpp", "src\gcode\Cq8MacroProgramBuilder.cpp", "src\workspaces\program_verification\fingerprint\ProgramSnapshotFingerprint.cpp", "src\gcode\SiemensProgramPackage.cpp", "src\postprocessor\PostProcessorBase.cpp", "src\postprocessor\SiemensPostProcessor.cpp", "src\postprocessor\FanucPostProcessor.cpp", "src\postprocessor\Cq8PostProcessor.cpp", "src\workspaces\process_design\selection\SelectionChainController.cpp") `
        -Libs @("-lQt5Core", "-lQt5Gui") `
        -NeedsGui

    $SimulationControllerMoc = Join-Path $DebugDir "moc_SimulationController_test.cpp"
    & moc src\workspaces\program_verification\simulation\SimulationController.h -o $SimulationControllerMoc
    if ($LASTEXITCODE -ne 0) {
        throw "SimulationController moc failed"
    }

    $ToolLibraryMoc = Join-Path $DebugDir "moc_ToolLibrary_test.cpp"
    & moc src\tool\ToolLibrary.h -o $ToolLibraryMoc
    if ($LASTEXITCODE -ne 0) {
        throw "ToolLibrary moc failed"
    }

    Invoke-TestBuild `
        -Name "simulation_gcode_test" `
        -Sources @("tests\simulation_gcode_test.cpp", "src\workspaces\program_verification\simulation\SimulationController.cpp", "src\tool\ToolLibrary.cpp", "src\tool\ToolEntry.cpp", $SimulationControllerMoc, $ToolLibraryMoc) `
        -Libs @("-lQt5Core", "-lQt5Gui") `
        -NeedsGui

    Invoke-TestBuild `
        -Name "slot_safety_regression_test" `
        -Sources @("tests\slot_safety_regression_test.cpp", "src\services\ProgramGenerationService.cpp", "src\strategies\mill\BlindSlotMillingStrategy.cpp", "src\strategies\mill\TaperedSlotMillingStrategy.cpp", "src\app\MillingBatchCommit.cpp", "src\postprocessor\SiemensPostProcessor.cpp", "src\simulation\SimulationController.cpp", "src\tool\ToolLibrary.cpp", "src\tool\ToolEntry.cpp", "src\gcode\GCodeSafetyValidator.cpp", "src\gcode\GCodeModalOptimizer.cpp", "src\gcode\Cq8MacroProgramBuilder.cpp", "src\gcode\ProgramSnapshotFingerprint.cpp", "src\gcode\SiemensProgramPackage.cpp", "src\postprocessor\PostProcessorBase.cpp", "src\postprocessor\FanucPostProcessor.cpp", "src\postprocessor\Cq8PostProcessor.cpp", $SimulationControllerMoc, $ToolLibraryMoc) `
        -Libs @("-lQt5Core", "-lQt5Gui") `
        -NeedsGui

    Invoke-TestBuild `
        -Name "slot_partial_depth_test" `
        -Sources @("tests\slot_partial_depth_test.cpp", "src\services\ProgramGenerationService.cpp", "src\strategies\mill\BlindSlotMillingStrategy.cpp", "src\strategies\mill\SlotMillingStrategy.cpp", "src\strategies\mill\SlotMachiningGeometry.cpp", "src\strategies\mill\TaperedSlotMillingStrategy.cpp", "src\simulation\SimulationController.cpp", "src\tool\ToolLibrary.cpp", "src\tool\ToolEntry.cpp", "src\gcode\GCodeSafetyValidator.cpp", "src\gcode\GCodeModalOptimizer.cpp", "src\gcode\Cq8MacroProgramBuilder.cpp", "src\gcode\ProgramSnapshotFingerprint.cpp", "src\gcode\SiemensProgramPackage.cpp", "src\postprocessor\PostProcessorBase.cpp", "src\postprocessor\FanucPostProcessor.cpp", "src\postprocessor\Cq8PostProcessor.cpp", $SimulationControllerMoc, $ToolLibraryMoc) `
        -Libs @("-lQt5Core", "-lQt5Gui") `
        -NeedsGui

    Invoke-TestBuild `
        -Name "blind_slot_cq8_integration_test" `
        -Sources @("tests\blind_slot_cq8_integration_test.cpp", "src\services\ProgramGenerationService.cpp", "src\strategies\mill\BlindSlotMillingStrategy.cpp", "src\simulation\SimulationController.cpp", "src\tool\ToolLibrary.cpp", "src\tool\ToolEntry.cpp", "src\gcode\GCodeSafetyValidator.cpp", "src\gcode\GCodeModalOptimizer.cpp", "src\gcode\Cq8MacroProgramBuilder.cpp", "src\gcode\ProgramSnapshotFingerprint.cpp", "src\gcode\SiemensProgramPackage.cpp", "src\postprocessor\PostProcessorBase.cpp", "src\postprocessor\FanucPostProcessor.cpp", "src\postprocessor\Cq8PostProcessor.cpp", $SimulationControllerMoc, $ToolLibraryMoc) `
        -Libs @("-lQt5Core", "-lQt5Gui") `
        -NeedsGui

    Invoke-TestBuild `
        -Name "open_slot_cq8_integration_test" `
        -Sources @("tests\open_slot_cq8_integration_test.cpp", "src\services\ProgramGenerationService.cpp", "src\strategies\mill\SlotMillingStrategy.cpp", "src\strategies\mill\SlotMachiningGeometry.cpp", "src\simulation\SimulationController.cpp", "src\tool\ToolLibrary.cpp", "src\tool\ToolEntry.cpp", "src\gcode\GCodeSafetyValidator.cpp", "src\gcode\GCodeModalOptimizer.cpp", "src\gcode\Cq8MacroProgramBuilder.cpp", "src\gcode\ProgramSnapshotFingerprint.cpp", "src\gcode\SiemensProgramPackage.cpp", "src\postprocessor\PostProcessorBase.cpp", "src\postprocessor\FanucPostProcessor.cpp", "src\postprocessor\Cq8PostProcessor.cpp", $SimulationControllerMoc, $ToolLibraryMoc) `
        -Libs @("-lQt5Core", "-lQt5Gui") `
        -NeedsGui

    Invoke-TestBuild `
        -Name "tapered_slot_cq8_integration_test" `
        -Sources @("tests\tapered_slot_cq8_integration_test.cpp", "src\app\MillingBatchCommit.cpp", "src\postprocessor\SiemensPostProcessor.cpp", "src\services\ProgramGenerationService.cpp", "src\strategies\mill\TaperedSlotMillingStrategy.cpp", "src\simulation\SimulationController.cpp", "src\tool\ToolLibrary.cpp", "src\tool\ToolEntry.cpp", "src\gcode\GCodeSafetyValidator.cpp", "src\gcode\GCodeModalOptimizer.cpp", "src\gcode\Cq8MacroProgramBuilder.cpp", "src\gcode\ProgramSnapshotFingerprint.cpp", "src\gcode\SiemensProgramPackage.cpp", "src\postprocessor\PostProcessorBase.cpp", "src\postprocessor\FanucPostProcessor.cpp", "src\postprocessor\Cq8PostProcessor.cpp", $SimulationControllerMoc, $ToolLibraryMoc) `
        -Libs @("-lQt5Core", "-lQt5Gui") `
        -NeedsGui

    Invoke-TestBuild `
        -Name "hole_spot_peck_cq8_integration_test" `
        -Sources @("tests\hole_spot_peck_cq8_integration_test.cpp", "src\services\ProgramGenerationService.cpp", "src\strategies\hole\SpotDrillingStrategy.cpp", "src\strategies\hole\PeckDrillingStrategy.cpp", "src\simulation\SimulationController.cpp", "src\tool\ToolLibrary.cpp", "src\tool\ToolEntry.cpp", "src\gcode\GCodeSafetyValidator.cpp", "src\gcode\GCodeModalOptimizer.cpp", "src\gcode\Cq8MacroProgramBuilder.cpp", "src\gcode\ProgramSnapshotFingerprint.cpp", "src\gcode\SiemensProgramPackage.cpp", "src\postprocessor\PostProcessorBase.cpp", "src\postprocessor\FanucPostProcessor.cpp", "src\postprocessor\Cq8PostProcessor.cpp", $SimulationControllerMoc, $ToolLibraryMoc) `
        -Libs @("-lQt5Core", "-lQt5Gui") `
        -NeedsGui

    Invoke-TestBuild `
        -Name "hole_peck_ream_cq8_integration_test" `
        -Sources @("tests\hole_peck_ream_cq8_integration_test.cpp", "src\services\ProgramGenerationService.cpp", "src\strategies\hole\PeckDrillingStrategy.cpp", "src\strategies\hole\ReamingStrategy.cpp", "src\simulation\SimulationController.cpp", "src\tool\ToolLibrary.cpp", "src\tool\ToolEntry.cpp", "src\gcode\GCodeSafetyValidator.cpp", "src\gcode\GCodeModalOptimizer.cpp", "src\gcode\Cq8MacroProgramBuilder.cpp", "src\gcode\ProgramSnapshotFingerprint.cpp", "src\gcode\SiemensProgramPackage.cpp", "src\postprocessor\PostProcessorBase.cpp", "src\postprocessor\FanucPostProcessor.cpp", "src\postprocessor\Cq8PostProcessor.cpp", $SimulationControllerMoc, $ToolLibraryMoc) `
        -Libs @("-lQt5Core", "-lQt5Gui") `
        -NeedsGui

    Invoke-TestBuild `
        -Name "hole_peck_chamfer_cq8_integration_test" `
        -Sources @("tests\hole_peck_chamfer_cq8_integration_test.cpp", "src\services\ProgramGenerationService.cpp", "src\strategies\hole\PeckDrillingStrategy.cpp", "src\strategies\hole\ChamferStrategy.cpp", "src\simulation\SimulationController.cpp", "src\tool\ToolLibrary.cpp", "src\tool\ToolEntry.cpp", "src\gcode\GCodeSafetyValidator.cpp", "src\gcode\GCodeModalOptimizer.cpp", "src\gcode\Cq8MacroProgramBuilder.cpp", "src\gcode\ProgramSnapshotFingerprint.cpp", "src\gcode\SiemensProgramPackage.cpp", "src\postprocessor\PostProcessorBase.cpp", "src\postprocessor\FanucPostProcessor.cpp", "src\postprocessor\Cq8PostProcessor.cpp", $SimulationControllerMoc, $ToolLibraryMoc) `
        -Libs @("-lQt5Core", "-lQt5Gui") `
        -NeedsGui

    Invoke-TestBuild `
        -Name "hole_peck_tap_cq8_integration_test" `
        -Sources @("tests\hole_peck_tap_cq8_integration_test.cpp", "src\services\ProgramGenerationService.cpp", "src\strategies\hole\PeckDrillingStrategy.cpp", "src\strategies\hole\TappingStrategy.cpp", "src\simulation\SimulationController.cpp", "src\tool\ToolLibrary.cpp", "src\tool\ToolEntry.cpp", "src\gcode\GCodeSafetyValidator.cpp", "src\gcode\GCodeModalOptimizer.cpp", "src\gcode\Cq8MacroProgramBuilder.cpp", "src\gcode\ProgramSnapshotFingerprint.cpp", "src\gcode\SiemensProgramPackage.cpp", "src\postprocessor\PostProcessorBase.cpp", "src\postprocessor\FanucPostProcessor.cpp", "src\postprocessor\Cq8PostProcessor.cpp", $SimulationControllerMoc, $ToolLibraryMoc) `
        -Libs @("-lQt5Core", "-lQt5Gui") `
        -NeedsGui

    Invoke-OccFixtureTest `
        -Name "hole_step_fixture_test" `
        -TestSource "tests\hole_step_fixture_test.cpp"

    Invoke-OccFixtureTest `
        -Name "acceptance_step_model_test" `
        -TestSource "tests\acceptance_step_model_test.cpp" `
        -ExtraSources @(
            "src\workspaces\process_design\selection\FeatureIdentity.cpp",
            "src\services\ProgramGenerationService.cpp",
            "src\strategies\StrategyBase.cpp",
            "src\strategies\hole\PeckDrillingStrategy.cpp",
            "src\strategies\mill\PocketRoughingStrategy.cpp",
            "src\gcode\GCodeSafetyValidator.cpp",
            "src\gcode\GCodeModalOptimizer.cpp",
            "src\gcode\Cq8MacroProgramBuilder.cpp",
            "src\gcode\ProgramSnapshotFingerprint.cpp",
            "src\gcode\SiemensProgramPackage.cpp",
            "src\postprocessor\PostProcessorBase.cpp",
            "src\postprocessor\FanucPostProcessor.cpp",
            "src\postprocessor\Cq8PostProcessor.cpp",
            "src\simulation\SimulationController.cpp",
            "src\tool\ToolLibrary.cpp",
            "src\tool\ToolEntry.cpp",
            $SimulationControllerMoc,
            $ToolLibraryMoc
        )

    Invoke-TestBuild `
        -Name "machine_profile_validator_test" `
        -Sources @("tests\machine_profile_validator_test.cpp", "src\workspaces\machine_operation\model\MachineProfileValidator.cpp") `
        -Libs @("-lQt5Core")

    $ProjectManagerMoc = Join-Path $DebugDir "moc_ProjectManager_test.cpp"
    & moc src\core\ProjectManager.h -o $ProjectManagerMoc
    if ($LASTEXITCODE -ne 0) {
        throw "ProjectManager moc failed"
    }

    if ($TestNames.Count -eq 0 -or "slot_workflow_ui_test" -in $TestNames) {
        $SlotUiMocs = @()
        foreach ($header in @("src/workspaces/process_design/ui/StrategyPanel.h", "src/workspaces/process_design/ui/ParameterEditorDialog.h", "src/workspaces/process_design/ui/CircleMillDialog.h", "src/workspaces/process_design/ui/OperationListPanel.h", "src/workspaces/process_design/ui/FeatureListPanel.h", "src/app/controllers/SlotOperationController.h")) {
            $outputMoc = Join-Path $DebugDir ("moc_slot_ui_" + [IO.Path]::GetFileNameWithoutExtension($header) + ".cpp")
            & moc $header -o $outputMoc
            if ($LASTEXITCODE -ne 0) { throw "slot UI moc failed" }
            $SlotUiMocs += $outputMoc
        }
        $SlotUiStrategies = @(Get-ChildItem src/strategies -Recurse -Filter *.cpp | ForEach-Object { $_.FullName })
        $SlotUiGeneration = @("src/workspaces/program_verification/generation/ProgramGenerationService.cpp", "src/gcode/ProgramSnapshotStatus.cpp", "src/workspaces/program_verification/safety/GCodeSafetyValidator.cpp", "src/gcode/GCodeModalOptimizer.cpp", "src/gcode/Cq8MacroProgramBuilder.cpp", "src/gcode/SiemensProgramPackage.cpp", "src/postprocessor/PostProcessorBase.cpp", "src/postprocessor/FanucPostProcessor.cpp", "src/postprocessor/Cq8PostProcessor.cpp")
        Invoke-TestBuild `
            -Name "slot_workflow_ui_test" `
            -Sources (@("tests/slot_workflow_ui_test.cpp", "src/workspaces/process_design/ui/StrategyPanel.cpp", "src/workspaces/process_design/ui/ParameterEditorDialog.cpp", "src/workspaces/process_design/ui/CircleMillDialog.cpp", "src/workspaces/process_design/ui/ToolOperationCompatibility.cpp", "src/core/Settings.cpp", "src/core/ProjectManager.cpp", "src/core/SetupOrigin.cpp", "src/core/StockDefinition.cpp", "src/import/StepImporter.cpp", "src/workspaces/program_verification/fingerprint/ProgramSnapshotFingerprint.cpp", "src/workspaces/process_design/ui/OperationListPanel.cpp", "src/workspaces/process_design/ui/FeatureListPanel.cpp", "src/workspaces/process_design/ui/FeatureDisplayFilter.cpp", "src/workspaces/process_design/ui/HoleFeatureGrouping.cpp", "src/workspaces/process_design/ui/ContourFeatureGrouping.cpp", "src/app/controllers/SlotOperationController.cpp", "src/app/MillingBatchCommit.cpp", "src/workspaces/process_design/ui/SelectionChainDialog.cpp", "src/workspaces/process_design/selection/SelectionChainController.cpp", "src/workspaces/process_design/selection/FeatureIdentity.cpp", "src/workspaces/process_design/operations/OperationProposal.cpp", $ProjectManagerMoc, "src/core/SetupOrientation.cpp", "src/tool/ToolLibrary.cpp", "src/tool/ToolEntry.cpp", $ToolLibraryMoc) + $SlotUiMocs + $SlotUiStrategies + $SlotUiGeneration) `
            -Libs @("-lQt5Widgets", "-lQt5Gui", "-lQt5Core") `
            -NeedsGui
    }

    if ($TestNames.Count -eq 0 -or "slot_dimensions_persistence_test" -in $TestNames -or "slot_operation_edit_test" -in $TestNames) {
        $OperationListPanelMoc = Join-Path $DebugDir "moc_OperationListPanel_test.cpp"
        & moc src\workspaces\process_design\ui\OperationListPanel.h -o $OperationListPanelMoc
        if ($LASTEXITCODE -ne 0) { throw "OperationListPanel moc failed" }
        $SlotDimensionStrategies = @(Get-ChildItem src\strategies -Recurse -Filter *.cpp | ForEach-Object { $_.FullName })
        Invoke-TestBuild `
            -Name "slot_dimensions_persistence_test" `
            -Sources (@("tests\slot_dimensions_persistence_test.cpp", "src\workspaces\process_design\ui\OperationListPanel.cpp", "src\core\Settings.cpp", "src\workspaces\process_design\selection\SelectionChainController.cpp", "src\tool\ToolLibrary.cpp", "src\tool\ToolEntry.cpp", "src\core\ProjectManager.cpp", "src\core\SetupOrigin.cpp", "src\core\StockDefinition.cpp", "src\gcode\ProgramSnapshotFingerprint.cpp", "src\import\StepImporter.cpp", $OperationListPanelMoc, $ToolLibraryMoc, $ProjectManagerMoc) + $SlotDimensionStrategies) `
            -Libs @("-lQt5Widgets", "-lQt5Gui", "-lQt5Core") `
            -NeedsGui
        Invoke-TestBuild `
            -Name "slot_operation_edit_test" `
            -Sources (@("tests\slot_operation_edit_test.cpp", "src\workspaces\process_design\ui\OperationListPanel.cpp", "src\core\Settings.cpp", "src\workspaces\process_design\selection\SelectionChainController.cpp", "src\tool\ToolLibrary.cpp", "src\tool\ToolEntry.cpp", "src\core\ProjectManager.cpp", "src\core\SetupOrigin.cpp", "src\core\StockDefinition.cpp", "src\gcode\ProgramSnapshotFingerprint.cpp", "src\import\StepImporter.cpp", $OperationListPanelMoc, $ToolLibraryMoc, $ProjectManagerMoc) + $SlotDimensionStrategies) `
            -Libs @("-lQt5Widgets", "-lQt5Gui", "-lQt5Core") `
            -NeedsGui
    }

    Invoke-TestBuild `
        -Name "project_manager_serialization_test" `
        -Sources @("tests\project_manager_serialization_test.cpp", "src\core\ProjectManager.cpp", "src\core\SetupOrigin.cpp", "src\core\StockDefinition.cpp", "src\gcode\ProgramSnapshotFingerprint.cpp", "src\import\StepImporter.cpp", $ProjectManagerMoc) `
        -Libs @("-lQt5Core", "-lQt5Gui") `
        -NeedsGui

    Invoke-TestBuild `
        -Name "program_tool_snapshot_test" `
        -Sources @("tests\program_tool_snapshot_test.cpp", "src\core\ProjectManager.cpp", "src\core\SetupOrigin.cpp", "src\core\StockDefinition.cpp", "src\gcode\ProgramSnapshotFingerprint.cpp", "src\gcode\ProgramSnapshotStatus.cpp", "src\import\StepImporter.cpp", $ProjectManagerMoc) `
        -Libs @("-lQt5Core", "-lQt5Gui") `
        -NeedsGui

    Invoke-TestBuild `
        -Name "project_operation_invalidation_test" `
        -Sources @("tests\project_operation_invalidation_test.cpp", "src\core\ProjectManager.cpp", "src\core\SetupOrigin.cpp", "src\core\StockDefinition.cpp", "src\gcode\ProgramSnapshotFingerprint.cpp", "src\import\StepImporter.cpp", $ProjectManagerMoc) `
        -Libs @("-lQt5Core", "-lQt5Gui") `
        -NeedsGui

    $AppControllerMoc = Join-Path $DebugDir "moc_AppController_test.cpp"
    & moc src\app\AppController.h -o $AppControllerMoc
    if ($LASTEXITCODE -ne 0) {
        throw "AppController moc failed"
    }

    Invoke-TestBuild `
        -Name "app_controller_project_load_test" `
        -Sources @("tests\app_controller_project_load_test.cpp", "src\app\AppController.cpp", "src\core\ProjectManager.cpp", "src\core\SetupOrigin.cpp", "src\core\StockDefinition.cpp", "src\core\SourceFileFingerprint.cpp", "src\import\StepImporter.cpp", $ProjectManagerMoc, $AppControllerMoc) `
        -Libs @("-lQt5Core", "-lQt5Gui") `
        -NeedsGui
    $expectedCount = if ($TestNames.Count -gt 0) {
        @($TestNames | Select-Object -Unique).Count
    } else { $registeredNames.Count }
    if ($executedTests.Count -eq 0 -or $executedTests.Count -ne $expectedCount) {
        throw "Executed $($executedTests.Count) C++ tests; expected $expectedCount"
    }
    Write-Host "Executed $($executedTests.Count) C++ tests: $($executedTests -join ', ')"
}
finally {
    $env:PATH = $PreviousPath
    $env:QT_QPA_PLATFORM = $PreviousQtPlatform
    $env:QT_PLUGIN_PATH = $PreviousQtPluginPath
    $env:QT_QPA_PLATFORM_PLUGIN_PATH = $PreviousQtPlatformPluginPath
    Pop-Location
}
