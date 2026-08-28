param(
    [string]$QtDir = "D:\qt5\5.14.2\mingw73_64",
    [string]$OccPrefix = "D:\msys64\mingw64"
)

$ErrorActionPreference = "Stop"

$ProjectDir = Split-Path -Parent $PSScriptRoot
$DebugDir = Join-Path $ProjectDir "test-debug"
$QtBin = Join-Path $QtDir "bin"
$QtInclude = Join-Path $QtDir "include"
$QtLib = Join-Path $QtDir "lib"
$Mkspecs = Join-Path $QtDir "mkspecs\win32-g++"

$env:PATH = "$QtBin;$env:PATH"

if (!(Test-Path $DebugDir)) {
    New-Item -ItemType Directory -Path $DebugDir | Out-Null
}

function Invoke-TestBuild {
    param(
        [string]$Name,
        [string[]]$Sources,
        [string[]]$Libs,
        [switch]$NeedsGui,
        [switch]$NeedsWidgets
    )

    $Output = Join-Path $DebugDir "$Name.exe"
    $Includes = @(
        "-I.",
        "-I$QtInclude",
        "-I$(Join-Path $QtInclude 'QtCore')",
        "-I$Mkspecs"
    )
    if ($NeedsGui -or $NeedsWidgets) {
        $Includes += "-I$(Join-Path $QtInclude 'QtGui')"
    }
    if ($NeedsWidgets) {
        $Includes += "-I$(Join-Path $QtInclude 'QtWidgets')"
    }

    $BuildsStrategy = @($Sources | Where-Object {
        $_ -like "src\strategies\*.cpp" -or
        $_ -like "src\strategies\*\*.cpp"
    }).Count -gt 0
    if ($BuildsStrategy) {
        if ($Sources -notcontains "src\strategies\StrategyBase.cpp") {
            $Sources += "src\strategies\StrategyBase.cpp"
        }
        if ($Sources -notcontains "src\core\ProcessParameterSchema.cpp") {
            $Sources += "src\core\ProcessParameterSchema.cpp"
        }
    }

    $LibArgs = @("-L$QtLib") + $Libs
    & g++ -std=gnu++17 @Includes @Sources @LibArgs -o $Output
    if ($LASTEXITCODE -ne 0) {
        throw "$Name build failed"
    }

    & $Output
    if ($LASTEXITCODE -ne 0) {
        throw "$Name failed"
    }
    Write-Host "PASS $Name"
}

function Invoke-OccFixtureTest {
    param(
        [string]$Name,
        [string]$TestSource,
        [string[]]$ExtraSources = @()
    )

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

    & g++ -std=gnu++17 -DCNEXT_ENABLE_OCC @Includes @Sources @OccLibraries `
        "-L$OccLib" -lfreetype -ltbb12 -ltbbmalloc -lz `
        "-L$QtLib" -lQt5Core -lQt5Gui -o $Output
    if ($LASTEXITCODE -ne 0) {
        throw "$Name build failed"
    }

    $env:PATH = "$OccBin;$env:PATH"
    & $Output
    if ($LASTEXITCODE -ne 0) {
        throw "$Name failed"
    }
    Write-Host "PASS $Name"
}

Push-Location $ProjectDir
try {
    Invoke-TestBuild `
        -Name "process_parameter_schema_test" `
        -Sources @("tests\process_parameter_schema_test.cpp", "src\core\ProcessParameterSchema.cpp") `
        -Libs @("-lQt5Core", "-lQt5Gui") `
        -NeedsGui

    Invoke-TestBuild `
        -Name "toolpath_geometry2d_kernel_test" `
        -Sources @("tests\toolpath_geometry2d_kernel_test.cpp", "src\core\geometry2d\ToolpathGeometry2D.cpp") `
        -Libs @("-lQt5Core", "-lQt5Gui") `
        -NeedsGui

    Invoke-TestBuild `
        -Name "toolpath_geometry2d_region_test" `
        -Sources @("tests\toolpath_geometry2d_region_test.cpp", "src\core\geometry2d\ToolpathGeometry2D.cpp") `
        -Libs @("-lQt5Core", "-lQt5Gui") `
        -NeedsGui

    Invoke-TestBuild `
        -Name "operation_factory_test" `
        -Sources @("tests\operation_factory_test.cpp", "src\services\OperationFactory.cpp", "src\core\ProcessContext.cpp", "src\core\ProcessParameterSchema.cpp", "src\strategies\OperationProposal.cpp", "src\core\FeatureIdentity.cpp") `
        -Libs @("-lQt5Core", "-lQt5Gui") `
        -NeedsGui

    Invoke-TestBuild `
        -Name "process_context_test" `
        -Sources @("tests\process_context_test.cpp", "src\core\ProcessContext.cpp", "src\services\OperationFactory.cpp", "src\core\ProcessParameterSchema.cpp", "src\strategies\OperationProposal.cpp", "src\core\FeatureIdentity.cpp") `
        -Libs @("-lQt5Core", "-lQt5Gui") `
        -NeedsGui

    Invoke-TestBuild `
        -Name "restricted_formula_evaluator_test" `
        -Sources @("tests\restricted_formula_evaluator_test.cpp", "src\services\RestrictedFormulaEvaluator.cpp") `
        -Libs @("-lQt5Core")

    Invoke-TestBuild `
        -Name "automation_template_json_test" `
        -Sources @("tests\automation_template_json_test.cpp", "src\core\automation\AutomationTemplateDocument.cpp", "src\services\RestrictedFormulaEvaluator.cpp") `
        -Libs @("-lQt5Core")

    Invoke-TestBuild `
        -Name "auto_hole_rule_matching_service_test" `
        -Sources @("tests\auto_hole_rule_matching_service_test.cpp", "src\services\AutoHoleRuleMatchingService.cpp", "src\services\RestrictedFormulaEvaluator.cpp") `
        -Libs @("-lQt5Core")

    Invoke-TestBuild `
        -Name "tool_cutting_parameter_service_test" `
        -Sources @("tests\tool_cutting_parameter_service_test.cpp", "src\services\ToolCuttingParameterService.cpp") `
        -Libs @("-lQt5Core")

    Invoke-TestBuild `
        -Name "process_template_library_test" `
        -Sources @("tests\process_template_library_test.cpp", "src\core\ProcessTemplateLibrary.cpp", "src\services\ProcessTemplateService.cpp", "src\services\OperationFactory.cpp", "src\core\ProcessContext.cpp", "src\core\ProcessParameterSchema.cpp", "src\strategies\OperationProposal.cpp", "src\core\FeatureIdentity.cpp") `
        -Libs @("-lQt5Core", "-lQt5Gui") `
        -NeedsGui

    Invoke-TestBuild `
        -Name "auto_hole_planning_service_test" `
        -Sources @("tests\auto_hole_planning_service_test.cpp", "src\core\CompoundHoleFeature.cpp", "src\import\CompoundHoleRecognizer.cpp", "src\services\AutoHolePlanningService.cpp", "src\services\ProcessTemplateService.cpp", "src\services\RestrictedFormulaEvaluator.cpp", "src\services\OperationFactory.cpp", "src\core\ProcessTemplateLibrary.cpp", "src\core\ProcessContext.cpp", "src\core\ProcessParameterSchema.cpp", "src\strategies\OperationProposal.cpp", "src\core\FeatureIdentity.cpp") `
        -Libs @("-lQt5Core", "-lQt5Gui") `
        -NeedsGui

    Invoke-TestBuild `
        -Name "compound_auto_hole_planning_test" `
        -Sources @("tests\compound_auto_hole_planning_test.cpp", "src\core\CompoundHoleFeature.cpp", "src\import\CompoundHoleRecognizer.cpp", "src\services\AutoHolePlanningService.cpp", "src\services\ProcessTemplateService.cpp", "src\services\RestrictedFormulaEvaluator.cpp", "src\services\OperationFactory.cpp", "src\core\ProcessTemplateLibrary.cpp", "src\core\ProcessContext.cpp", "src\core\ProcessParameterSchema.cpp", "src\strategies\OperationProposal.cpp", "src\core\FeatureIdentity.cpp") `
        -Libs @("-lQt5Core", "-lQt5Gui") `
        -NeedsGui

    Invoke-TestBuild `
        -Name "compound_hole_recognizer_test" `
        -Sources @("tests\compound_hole_recognizer_test.cpp", "src\core\CompoundHoleFeature.cpp", "src\import\CompoundHoleRecognizer.cpp", "src\core\FeatureIdentity.cpp") `
        -Libs @("-lQt5Core", "-lQt5Gui") `
        -NeedsGui

    Invoke-TestBuild `
        -Name "slot_frame_recognizer_test" `
        -Sources @("tests\slot_frame_recognizer_test.cpp", "src\import\SlotFrameRecognizer.cpp", "src\core\FeatureIdentity.cpp", "src\core\ProcessContext.cpp", "src\core\SetupOrigin.cpp") `
        -Libs @("-lQt5Core", "-lQt5Gui") `
        -NeedsGui

    Invoke-TestBuild `
        -Name "slot_frame_planning_service_test" `
        -Sources @("tests\slot_frame_planning_service_test.cpp", "src\services\SlotFramePlanningService.cpp", "src\services\OperationFactory.cpp", "src\strategies\OperationProposal.cpp", "src\core\FeatureIdentity.cpp", "src\core\ProcessParameterSchema.cpp", "src\core\ProcessContext.cpp", "src\core\SetupOrigin.cpp") `
        -Libs @("-lQt5Core", "-lQt5Gui") `
        -NeedsGui

    Invoke-TestBuild `
        -Name "auto_hole_candidate_dialog_test" `
        -Sources @("tests\auto_hole_candidate_dialog_test.cpp", "src\ui\AutoHoleCandidateDialog.cpp") `
        -Libs @("-lQt5Core", "-lQt5Gui", "-lQt5Widgets") `
        -NeedsWidgets

    Invoke-TestBuild `
        -Name "strategy_parameter_contract_test" `
        -Sources @("tests\strategy_parameter_contract_test.cpp", "src\core\ProcessParameterSchema.cpp", "src\strategies\StrategyBase.cpp", "src\strategies\hole\SpotDrillingStrategy.cpp") `
        -Libs @("-lQt5Core", "-lQt5Gui") `
        -NeedsGui

    Invoke-TestBuild `
        -Name "hole_cycle_strategy_test" `
        -Sources @("tests\hole_cycle_strategy_test.cpp", "src\strategies\hole\HighSpeedPeckDrillingStrategy.cpp", "src\strategies\hole\BoringG86Strategy.cpp") `
        -Libs @("-lQt5Core", "-lQt5Gui") `
        -NeedsGui

    Invoke-TestBuild `
        -Name "thread_milling_strategy_test" `
        -Sources @("tests\thread_milling_strategy_test.cpp", "src\strategies\hole\ThreadMillingStrategy.cpp") `
        -Libs @("-lQt5Core", "-lQt5Gui") `
        -NeedsGui

    Invoke-TestBuild `
        -Name "fixed_hole_cycle_post_test" `
        -Sources @("tests\fixed_hole_cycle_post_test.cpp", "src\postprocessor\PostProcessorBase.cpp", "src\postprocessor\FanucPostProcessor.cpp", "src\postprocessor\Cq8PostProcessor.cpp", "src\postprocessor\SiemensPostProcessor.cpp") `
        -Libs @("-lQt5Core")

    $ParameterEditorDialogMoc = Join-Path $DebugDir "moc_ParameterEditorDialog_test.cpp"
    & moc src\ui\ParameterEditorDialog.h -o $ParameterEditorDialogMoc
    if ($LASTEXITCODE -ne 0) {
        throw "ParameterEditorDialog moc failed"
    }

    Invoke-TestBuild `
        -Name "parameter_editor_schema_test" `
        -Sources @("tests\parameter_editor_schema_test.cpp", "src\ui\ParameterEditorDialog.cpp", "src\core\ProcessParameterSchema.cpp", $ParameterEditorDialogMoc) `
        -Libs @("-lQt5Core", "-lQt5Gui", "-lQt5Widgets") `
        -NeedsWidgets

    Invoke-TestBuild `
        -Name "standalone_ui_contract_test" `
        -Sources @("tests\standalone_ui_contract_test.cpp") `
        -Libs @("-lQt5Core")

    Invoke-TestBuild `
        -Name "architecture_boundary_contract_test" `
        -Sources @("tests\architecture_boundary_contract_test.cpp") `
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
        -Sources @("tests\operation_workflow_model_test.cpp", "src\core\FeatureIdentity.cpp", "src\core\HoleSelectionSession.cpp") `
        -Libs @("-lQt5Core", "-lQt5Gui") `
        -NeedsGui

    Invoke-TestBuild `
        -Name "selection_chain_controller_test" `
        -Sources @("tests\selection_chain_controller_test.cpp", "src\core\SelectionChainController.cpp") `
        -Libs @("-lQt5Core", "-lQt5Gui") `
        -NeedsGui

    Invoke-TestBuild `
        -Name "selection_chain_operation_contract_test" `
        -Sources @("tests\selection_chain_operation_contract_test.cpp", "src\services\OperationFactory.cpp", "src\strategies\OperationProposal.cpp", "src\core\FeatureIdentity.cpp", "src\core\ProcessContext.cpp", "src\core\SelectionChainController.cpp") `
        -Libs @("-lQt5Core", "-lQt5Gui") `
        -NeedsGui

    Invoke-TestBuild `
        -Name "hole_selection_dialog_test" `
        -Sources @("tests\hole_selection_dialog_test.cpp", "src\ui\HoleSelectionDialog.cpp", "src\core\HoleSelectionSession.cpp") `
        -Libs @("-lQt5Core", "-lQt5Gui", "-lQt5Widgets") `
        -NeedsWidgets

    Invoke-TestBuild `
        -Name "hole_selection_dialog_wiring_test" `
        -Sources @("tests\hole_selection_dialog_wiring_test.cpp") `
        -Libs @("-lQt5Core")

    Invoke-TestBuild `
        -Name "feature_list_filter_test" `
        -Sources @("tests\feature_list_filter_test.cpp", "src\ui\FeatureDisplayFilter.cpp") `
        -Libs @("-lQt5Core", "-lQt5Gui") `
        -NeedsGui

    Invoke-TestBuild `
        -Name "hole_feature_grouping_test" `
        -Sources @("tests\hole_feature_grouping_test.cpp", "src\ui\HoleFeatureGrouping.cpp") `
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
        -Sources @("tests\gcode_safety_validator_test.cpp", "src\gcode\GCodeSafetyValidator.cpp") `
        -Libs @("-lQt5Core")

    Invoke-TestBuild `
        -Name "cutter_compensation_strategy_test" `
        -Sources @("tests\cutter_compensation_strategy_test.cpp", "src\gcode\GCodeSafetyValidator.cpp", "src\strategies\mill\CircleMillingStrategy.cpp", "src\strategies\mill\ContourFinishStrategy.cpp", "src\strategies\mill\ContourMillingContract.cpp", "src\strategies\mill\OpenContourMillingStrategy.cpp", "src\strategies\mill\ClosedContourMillingStrategy.cpp", "src\strategies\mill\SlotMillingStrategy.cpp", "src\strategies\mill\SlotMachiningGeometry.cpp", "src\strategies\mill\BlindSlotMillingStrategy.cpp") `
        -Libs @("-lQt5Core", "-lQt5Gui") `
        -NeedsGui

    Invoke-TestBuild `
        -Name "contour_feature_grouping_test" `
        -Sources @("tests\contour_feature_grouping_test.cpp", "src\ui\ContourFeatureGrouping.cpp") `
        -Libs @("-lQt5Core", "-lQt5Gui") `
        -NeedsGui

    Invoke-TestBuild `
        -Name "contour_machining_choice_test" `
        -Sources @("tests\contour_machining_choice_test.cpp", "src\ui\ContourMachiningChoice.cpp", "src\core\SelectionChainController.cpp") `
        -Libs @("-lQt5Core", "-lQt5Gui") `
        -NeedsGui

    Invoke-TestBuild `
        -Name "annular_milling_strategy_test" `
        -Sources @("tests\annular_milling_strategy_test.cpp", "src\strategies\mill\AnnularMillingStrategy.cpp", "src\core\geometry2d\ToolpathGeometry2D.cpp") `
        -Libs @("-lQt5Core", "-lQt5Gui") `
        -NeedsGui

    Invoke-TestBuild `
        -Name "outer_contour_chamfer_strategy_test" `
        -Sources @("tests\outer_contour_chamfer_strategy_test.cpp", "src\strategies\mill\OuterContourChamferStrategy.cpp") `
        -Libs @("-lQt5Core", "-lQt5Gui") `
        -NeedsGui

    Invoke-TestBuild `
        -Name "inner_corner_cleanup_strategy_test" `
        -Sources @("tests\inner_corner_cleanup_strategy_test.cpp", "src\strategies\mill\InnerCornerCleanupStrategy.cpp") `
        -Libs @("-lQt5Core", "-lQt5Gui") `
        -NeedsGui

    Invoke-TestBuild `
        -Name "planar_slope_milling_strategy_test" `
        -Sources @("tests\planar_slope_milling_strategy_test.cpp", "src\strategies\mill\PlanarSlopeMillingStrategy.cpp") `
        -Libs @("-lQt5Core", "-lQt5Gui") `
        -NeedsGui

    Invoke-TestBuild `
        -Name "surface_finish_contract_test" `
        -Sources @("tests\surface_finish_contract_test.cpp", "src\strategies\mill\SurfaceFinishStrategy.cpp") `
        -Libs @("-lQt5Core", "-lQt5Gui") `
        -NeedsGui

    $ToolLibraryMetadataMoc = Join-Path $DebugDir "moc_ToolLibrary_metadata_test.cpp"
    & moc src\tool\ToolLibrary.h -o $ToolLibraryMetadataMoc
    if ($LASTEXITCODE -ne 0) {
        throw "ToolLibrary metadata moc failed"
    }
    Invoke-TestBuild `
        -Name "tool_library_metadata_test" `
        -Sources @("tests\tool_library_metadata_test.cpp", "src\tool\ToolLibrary.cpp", "src\tool\ToolEntry.cpp", $ToolLibraryMetadataMoc) `
        -Libs @("-lQt5Core")

    Invoke-TestBuild `
        -Name "island_milling_strategy_test" `
        -Sources @("tests\island_milling_strategy_test.cpp", "src\strategies\mill\IslandMillingStrategy.cpp", "src\strategies\mill\AnnularMillingStrategy.cpp", "src\core\geometry2d\ToolpathGeometry2D.cpp") `
        -Libs @("-lQt5Core", "-lQt5Gui") `
        -NeedsGui

    Invoke-TestBuild `
        -Name "tool_operation_compatibility_test" `
        -Sources @("tests\tool_operation_compatibility_test.cpp", "src\ui\ToolOperationCompatibility.cpp") `
        -Libs @("-lQt5Core", "-lQt5Gui") `
        -NeedsGui

    Invoke-TestBuild `
        -Name "pocket_roughing_strategy_test" `
        -Sources @("tests\pocket_roughing_strategy_test.cpp", "src\strategies\mill\PocketRoughingStrategy.cpp", "src\core\geometry2d\ToolpathGeometry2D.cpp") `
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
        -Sources @("tests\program_snapshot_fingerprint_test.cpp", "src\gcode\ProgramSnapshotFingerprint.cpp", "src\import\StepImporter.cpp") `
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
        -Sources @("tests\operation_proposal_confirmation_test.cpp", "src\strategies\OperationProposal.cpp", "src\core\FeatureIdentity.cpp") `
        -Libs @("-lQt5Core", "-lQt5Gui") `
        -NeedsGui

    Invoke-TestBuild `
        -Name "program_generation_service_test" `
        -Sources @("tests\program_generation_service_test.cpp", "src\services\ProgramGenerationService.cpp", "src\strategies\hole\PeckDrillingStrategy.cpp", "src\gcode\GCodeSafetyValidator.cpp", "src\gcode\GCodeModalOptimizer.cpp", "src\gcode\Cq8MacroProgramBuilder.cpp", "src\gcode\ProgramSnapshotFingerprint.cpp", "src\gcode\SiemensProgramPackage.cpp", "src\postprocessor\PostProcessorBase.cpp", "src\postprocessor\SiemensPostProcessor.cpp", "src\postprocessor\FanucPostProcessor.cpp", "src\postprocessor\Cq8PostProcessor.cpp") `
        -Libs @("-lQt5Core", "-lQt5Gui") `
        -NeedsGui

    $SimulationControllerMoc = Join-Path $DebugDir "moc_SimulationController_test.cpp"
    & moc src\simulation\SimulationController.h -o $SimulationControllerMoc
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
        -Sources @("tests\simulation_gcode_test.cpp", "src\simulation\SimulationController.cpp", "src\tool\ToolLibrary.cpp", "src\tool\ToolEntry.cpp", $SimulationControllerMoc, $ToolLibraryMoc) `
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
        -Name "annular_milling_cq8_integration_test" `
        -Sources @("tests\annular_milling_cq8_integration_test.cpp", "src\services\ProgramGenerationService.cpp", "src\strategies\mill\AnnularMillingStrategy.cpp", "src\strategies\mill\IslandMillingStrategy.cpp", "src\core\geometry2d\ToolpathGeometry2D.cpp", "src\simulation\SimulationController.cpp", "src\tool\ToolLibrary.cpp", "src\tool\ToolEntry.cpp", "src\gcode\GCodeSafetyValidator.cpp", "src\gcode\GCodeModalOptimizer.cpp", "src\gcode\Cq8MacroProgramBuilder.cpp", "src\gcode\ProgramSnapshotFingerprint.cpp", "src\gcode\SiemensProgramPackage.cpp", "src\postprocessor\PostProcessorBase.cpp", "src\postprocessor\FanucPostProcessor.cpp", "src\postprocessor\SiemensPostProcessor.cpp", "src\postprocessor\Cq8PostProcessor.cpp", $SimulationControllerMoc, $ToolLibraryMoc) `
        -Libs @("-lQt5Core", "-lQt5Gui") `
        -NeedsGui

    Invoke-TestBuild `
        -Name "outer_contour_chamfer_cq8_integration_test" `
        -Sources @("tests\outer_contour_chamfer_cq8_integration_test.cpp", "src\services\OperationFactory.cpp", "src\strategies\OperationProposal.cpp", "src\core\FeatureIdentity.cpp", "src\core\ProcessContext.cpp", "src\core\SelectionChainController.cpp", "src\services\ProgramGenerationService.cpp", "src\strategies\mill\OuterContourChamferStrategy.cpp", "src\simulation\SimulationController.cpp", "src\tool\ToolLibrary.cpp", "src\tool\ToolEntry.cpp", "src\gcode\GCodeSafetyValidator.cpp", "src\gcode\GCodeModalOptimizer.cpp", "src\gcode\Cq8MacroProgramBuilder.cpp", "src\gcode\ProgramSnapshotFingerprint.cpp", "src\gcode\SiemensProgramPackage.cpp", "src\postprocessor\PostProcessorBase.cpp", "src\postprocessor\FanucPostProcessor.cpp", "src\postprocessor\Cq8PostProcessor.cpp", $SimulationControllerMoc, $ToolLibraryMoc) `
        -Libs @("-lQt5Core", "-lQt5Gui") `
        -NeedsGui

    Invoke-TestBuild `
        -Name "inner_corner_cleanup_cq8_integration_test" `
        -Sources @("tests\inner_corner_cleanup_cq8_integration_test.cpp", "src\services\ProgramGenerationService.cpp", "src\strategies\mill\InnerCornerCleanupStrategy.cpp", "src\simulation\SimulationController.cpp", "src\tool\ToolLibrary.cpp", "src\tool\ToolEntry.cpp", "src\gcode\GCodeSafetyValidator.cpp", "src\gcode\GCodeModalOptimizer.cpp", "src\gcode\Cq8MacroProgramBuilder.cpp", "src\gcode\ProgramSnapshotFingerprint.cpp", "src\gcode\SiemensProgramPackage.cpp", "src\postprocessor\PostProcessorBase.cpp", "src\postprocessor\FanucPostProcessor.cpp", "src\postprocessor\SiemensPostProcessor.cpp", "src\postprocessor\Cq8PostProcessor.cpp", $SimulationControllerMoc, $ToolLibraryMoc) `
        -Libs @("-lQt5Core", "-lQt5Gui") `
        -NeedsGui

    Invoke-TestBuild `
        -Name "planar_slope_cq8_integration_test" `
        -Sources @("tests\planar_slope_cq8_integration_test.cpp", "src\services\OperationFactory.cpp", "src\strategies\OperationProposal.cpp", "src\core\FeatureIdentity.cpp", "src\core\ProcessContext.cpp", "src\core\SelectionChainController.cpp", "src\services\ProgramGenerationService.cpp", "src\strategies\mill\PlanarSlopeMillingStrategy.cpp", "src\simulation\SimulationController.cpp", "src\tool\ToolLibrary.cpp", "src\tool\ToolEntry.cpp", "src\gcode\GCodeSafetyValidator.cpp", "src\gcode\GCodeModalOptimizer.cpp", "src\gcode\Cq8MacroProgramBuilder.cpp", "src\gcode\ProgramSnapshotFingerprint.cpp", "src\gcode\SiemensProgramPackage.cpp", "src\postprocessor\PostProcessorBase.cpp", "src\postprocessor\FanucPostProcessor.cpp", "src\postprocessor\Cq8PostProcessor.cpp", $SimulationControllerMoc, $ToolLibraryMoc) `
        -Libs @("-lQt5Core", "-lQt5Gui") `
        -NeedsGui

    Invoke-TestBuild `
        -Name "tapered_slot_cq8_integration_test" `
        -Sources @("tests\tapered_slot_cq8_integration_test.cpp", "src\services\ProgramGenerationService.cpp", "src\strategies\mill\TaperedSlotMillingStrategy.cpp", "src\simulation\SimulationController.cpp", "src\tool\ToolLibrary.cpp", "src\tool\ToolEntry.cpp", "src\gcode\GCodeSafetyValidator.cpp", "src\gcode\GCodeModalOptimizer.cpp", "src\gcode\Cq8MacroProgramBuilder.cpp", "src\gcode\ProgramSnapshotFingerprint.cpp", "src\gcode\SiemensProgramPackage.cpp", "src\postprocessor\PostProcessorBase.cpp", "src\postprocessor\FanucPostProcessor.cpp", "src\postprocessor\Cq8PostProcessor.cpp", $SimulationControllerMoc, $ToolLibraryMoc) `
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
        -Name "hole_g73_g86_cq8_integration_test" `
        -Sources @("tests\hole_g73_g86_cq8_integration_test.cpp", "src\services\ProgramGenerationService.cpp", "src\strategies\hole\HighSpeedPeckDrillingStrategy.cpp", "src\strategies\hole\BoringG86Strategy.cpp", "src\simulation\SimulationController.cpp", "src\tool\ToolLibrary.cpp", "src\tool\ToolEntry.cpp", "src\gcode\GCodeSafetyValidator.cpp", "src\gcode\GCodeModalOptimizer.cpp", "src\gcode\Cq8MacroProgramBuilder.cpp", "src\gcode\ProgramSnapshotFingerprint.cpp", "src\gcode\SiemensProgramPackage.cpp", "src\postprocessor\PostProcessorBase.cpp", "src\postprocessor\FanucPostProcessor.cpp", "src\postprocessor\Cq8PostProcessor.cpp", $SimulationControllerMoc, $ToolLibraryMoc) `
        -Libs @("-lQt5Core", "-lQt5Gui") `
        -NeedsGui

    Invoke-TestBuild `
        -Name "auto_hole_cq8_integration_test" `
        -Sources @("tests\auto_hole_cq8_integration_test.cpp", "src\core\CompoundHoleFeature.cpp", "src\import\CompoundHoleRecognizer.cpp", "src\services\AutoHolePlanningService.cpp", "src\services\ProcessTemplateService.cpp", "src\services\RestrictedFormulaEvaluator.cpp", "src\services\OperationFactory.cpp", "src\services\ProgramGenerationService.cpp", "src\core\ProcessTemplateLibrary.cpp", "src\core\ProcessContext.cpp", "src\core\ProcessParameterSchema.cpp", "src\core\FeatureIdentity.cpp", "src\strategies\OperationProposal.cpp", "src\strategies\hole\PeckDrillingStrategy.cpp", "src\simulation\SimulationController.cpp", "src\tool\ToolLibrary.cpp", "src\tool\ToolEntry.cpp", "src\gcode\GCodeSafetyValidator.cpp", "src\gcode\GCodeModalOptimizer.cpp", "src\gcode\Cq8MacroProgramBuilder.cpp", "src\gcode\ProgramSnapshotFingerprint.cpp", "src\gcode\SiemensProgramPackage.cpp", "src\postprocessor\PostProcessorBase.cpp", "src\postprocessor\FanucPostProcessor.cpp", "src\postprocessor\Cq8PostProcessor.cpp", $SimulationControllerMoc, $ToolLibraryMoc) `
        -Libs @("-lQt5Core", "-lQt5Gui") `
        -NeedsGui

    Invoke-TestBuild `
        -Name "compound_auto_hole_cq8_integration_test" `
        -Sources @("tests\compound_auto_hole_cq8_integration_test.cpp", "src\core\CompoundHoleFeature.cpp", "src\import\CompoundHoleRecognizer.cpp", "src\services\AutoHolePlanningService.cpp", "src\services\ProcessTemplateService.cpp", "src\services\RestrictedFormulaEvaluator.cpp", "src\services\OperationFactory.cpp", "src\core\ProcessTemplateLibrary.cpp", "src\core\ProcessContext.cpp", "src\core\ProcessParameterSchema.cpp", "src\core\FeatureIdentity.cpp", "src\strategies\OperationProposal.cpp", "src\strategies\hole\PeckDrillingStrategy.cpp", "src\strategies\hole\ChamferStrategy.cpp", "src\simulation\SimulationController.cpp", "src\tool\ToolLibrary.cpp", "src\tool\ToolEntry.cpp", "src\gcode\GCodeSafetyValidator.cpp", "src\gcode\GCodeModalOptimizer.cpp", "src\gcode\Cq8MacroProgramBuilder.cpp", "src\gcode\ProgramSnapshotFingerprint.cpp", "src\gcode\SiemensProgramPackage.cpp", "src\services\ProgramGenerationService.cpp", "src\postprocessor\PostProcessorBase.cpp", "src\postprocessor\FanucPostProcessor.cpp", "src\postprocessor\SiemensPostProcessor.cpp", "src\postprocessor\Cq8PostProcessor.cpp", $SimulationControllerMoc, $ToolLibraryMoc) `
        -Libs @("-lQt5Core", "-lQt5Gui") `
        -NeedsGui

    Invoke-TestBuild `
        -Name "slot_frame_cq8_integration_test" `
        -Sources @("tests\slot_frame_cq8_integration_test.cpp", "src\services\SlotFramePlanningService.cpp", "src\services\OperationFactory.cpp", "src\services\ProgramGenerationService.cpp", "src\strategies\OperationProposal.cpp", "src\strategies\mill\PocketRoughingStrategy.cpp", "src\strategies\mill\SlotMillingStrategy.cpp", "src\strategies\mill\SlotMachiningGeometry.cpp", "src\core\geometry2d\ToolpathGeometry2D.cpp", "src\simulation\SimulationController.cpp", "src\tool\ToolLibrary.cpp", "src\tool\ToolEntry.cpp", "src\gcode\GCodeSafetyValidator.cpp", "src\gcode\GCodeModalOptimizer.cpp", "src\gcode\Cq8MacroProgramBuilder.cpp", "src\gcode\ProgramSnapshotFingerprint.cpp", "src\gcode\SiemensProgramPackage.cpp", "src\postprocessor\PostProcessorBase.cpp", "src\postprocessor\FanucPostProcessor.cpp", "src\postprocessor\SiemensPostProcessor.cpp", "src\postprocessor\Cq8PostProcessor.cpp", "src\core\FeatureIdentity.cpp", "src\core\ProcessParameterSchema.cpp", "src\core\ProcessContext.cpp", "src\core\SetupOrigin.cpp", $SimulationControllerMoc, $ToolLibraryMoc) `
        -Libs @("-lQt5Core", "-lQt5Gui") `
        -NeedsGui

    Invoke-TestBuild `
        -Name "thread_milling_cq8_integration_test" `
        -Sources @("tests\thread_milling_cq8_integration_test.cpp", "src\services\ProgramGenerationService.cpp", "src\strategies\hole\ThreadMillingStrategy.cpp", "src\simulation\SimulationController.cpp", "src\tool\ToolLibrary.cpp", "src\tool\ToolEntry.cpp", "src\gcode\GCodeSafetyValidator.cpp", "src\gcode\GCodeModalOptimizer.cpp", "src\gcode\Cq8MacroProgramBuilder.cpp", "src\gcode\ProgramSnapshotFingerprint.cpp", "src\gcode\SiemensProgramPackage.cpp", "src\postprocessor\PostProcessorBase.cpp", "src\postprocessor\FanucPostProcessor.cpp", "src\postprocessor\Cq8PostProcessor.cpp", $SimulationControllerMoc, $ToolLibraryMoc) `
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
        -Name "slot_frame_occ_adjacency_test" `
        -TestSource "tests\slot_frame_occ_adjacency_test.cpp" `
        -ExtraSources @(
            "src\import\SlotFrameRecognizer.cpp",
            "src\core\FeatureIdentity.cpp",
            "src\core\ProcessContext.cpp",
            "src\core\SetupOrigin.cpp"
        )

    Invoke-OccFixtureTest `
        -Name "acceptance_step_model_test" `
        -TestSource "tests\acceptance_step_model_test.cpp" `
        -ExtraSources @(
            "src\services\ProgramGenerationService.cpp",
            "src\strategies\StrategyBase.cpp",
            "src\core\ProcessParameterSchema.cpp",
            "src\strategies\hole\PeckDrillingStrategy.cpp",
            "src\strategies\mill\PocketRoughingStrategy.cpp",
            "src\core\geometry2d\ToolpathGeometry2D.cpp",
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
        -Sources @("tests\machine_profile_validator_test.cpp", "src\core\MachineProfileValidator.cpp") `
        -Libs @("-lQt5Core")

    $ProjectManagerMoc = Join-Path $DebugDir "moc_ProjectManager_test.cpp"
    & moc src\core\ProjectManager.h -o $ProjectManagerMoc
    if ($LASTEXITCODE -ne 0) {
        throw "ProjectManager moc failed"
    }

    Invoke-TestBuild `
        -Name "project_manager_serialization_test" `
        -Sources @("tests\project_manager_serialization_test.cpp", "src\core\ProjectManager.cpp", "src\core\ProcessTemplateLibrary.cpp", "src\core\ProcessParameterSchema.cpp", "src\core\SelectionChainController.cpp", "src\core\SetupOrigin.cpp", "src\core\StockDefinition.cpp", "src\gcode\ProgramSnapshotFingerprint.cpp", "src\import\StepImporter.cpp", $ProjectManagerMoc) `
        -Libs @("-lQt5Core", "-lQt5Gui") `
        -NeedsGui

    Invoke-TestBuild `
        -Name "slot_frame_persistence_test" `
        -Sources @("tests\slot_frame_persistence_test.cpp", "src\core\ProjectManager.cpp", "src\core\ProcessTemplateLibrary.cpp", "src\core\ProcessParameterSchema.cpp", "src\core\SelectionChainController.cpp", "src\core\SetupOrigin.cpp", "src\core\StockDefinition.cpp", "src\gcode\ProgramSnapshotFingerprint.cpp", "src\import\StepImporter.cpp", $ProjectManagerMoc) `
        -Libs @("-lQt5Core", "-lQt5Gui") `
        -NeedsGui

    Invoke-TestBuild `
        -Name "system_acceptance_roundtrip_test" `
        -Sources @("tests\system_acceptance_roundtrip_test.cpp", "src\core\ProjectManager.cpp", "src\core\ProcessTemplateLibrary.cpp", "src\core\ProcessContext.cpp", "src\core\ProcessParameterSchema.cpp", "src\core\FeatureIdentity.cpp", "src\core\SetupOrigin.cpp", "src\core\StockDefinition.cpp", "src\core\SelectionChainController.cpp", "src\services\OperationFactory.cpp", "src\services\ProgramGenerationService.cpp", "src\strategies\OperationProposal.cpp", "src\strategies\hole\PeckDrillingStrategy.cpp", "src\simulation\SimulationController.cpp", "src\tool\ToolLibrary.cpp", "src\tool\ToolEntry.cpp", "src\gcode\GCodeSafetyValidator.cpp", "src\gcode\GCodeModalOptimizer.cpp", "src\gcode\Cq8MacroProgramBuilder.cpp", "src\gcode\ProgramSnapshotFingerprint.cpp", "src\gcode\SiemensProgramPackage.cpp", "src\postprocessor\PostProcessorBase.cpp", "src\postprocessor\FanucPostProcessor.cpp", "src\postprocessor\Cq8PostProcessor.cpp", "src\import\StepImporter.cpp", $ProjectManagerMoc, $SimulationControllerMoc, $ToolLibraryMoc) `
        -Libs @("-lQt5Core", "-lQt5Gui") `
        -NeedsGui

    Invoke-TestBuild `
        -Name "project_operation_invalidation_test" `
        -Sources @("tests\project_operation_invalidation_test.cpp", "src\core\ProjectManager.cpp", "src\core\ProcessTemplateLibrary.cpp", "src\core\ProcessParameterSchema.cpp", "src\core\SetupOrigin.cpp", "src\core\StockDefinition.cpp", "src\gcode\ProgramSnapshotFingerprint.cpp", "src\import\StepImporter.cpp", $ProjectManagerMoc) `
        -Libs @("-lQt5Core", "-lQt5Gui") `
        -NeedsGui

    $AppControllerMoc = Join-Path $DebugDir "moc_AppController_test.cpp"
    & moc src\core\AppController.h -o $AppControllerMoc
    if ($LASTEXITCODE -ne 0) {
        throw "AppController moc failed"
    }

    Invoke-TestBuild `
        -Name "app_controller_project_load_test" `
        -Sources @("tests\app_controller_project_load_test.cpp", "src\core\AppController.cpp", "src\core\ProjectManager.cpp", "src\core\ProcessTemplateLibrary.cpp", "src\core\ProcessParameterSchema.cpp", "src\core\SetupOrigin.cpp", "src\core\StockDefinition.cpp", "src\core\SourceFileFingerprint.cpp", "src\import\StepImporter.cpp", $ProjectManagerMoc, $AppControllerMoc) `
        -Libs @("-lQt5Core", "-lQt5Gui") `
        -NeedsGui
}
finally {
    Pop-Location
}
