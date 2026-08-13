param(
    [string]$QtDir = "D:\qt5\5.14.2\mingw73_64",
    [string]$OccPrefix = "D:\msys64\mingw64"
)

$ErrorActionPreference = "Stop"

$ProjectDir = Split-Path -Parent $PSScriptRoot
$DebugDir = Join-Path $ProjectDir "debug"
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
        [switch]$NeedsGui
    )

    $Output = Join-Path $DebugDir "$Name.exe"
    $Includes = @(
        "-I.",
        "-I$QtInclude",
        "-I$(Join-Path $QtInclude 'QtCore')",
        "-I$Mkspecs"
    )
    if ($NeedsGui) {
        $Includes += "-I$(Join-Path $QtInclude 'QtGui')"
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
        -Name "standalone_ui_contract_test" `
        -Sources @("tests\standalone_ui_contract_test.cpp") `
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
        -Sources @("tests\cutter_compensation_strategy_test.cpp", "src\gcode\GCodeSafetyValidator.cpp", "src\strategies\mill\CircleMillingStrategy.cpp", "src\strategies\mill\ContourFinishStrategy.cpp", "src\strategies\mill\OpenContourMillingStrategy.cpp", "src\strategies\mill\ClosedContourMillingStrategy.cpp", "src\strategies\mill\SlotMillingStrategy.cpp", "src\strategies\mill\SlotMachiningGeometry.cpp", "src\strategies\mill\BlindSlotMillingStrategy.cpp") `
        -Libs @("-lQt5Core", "-lQt5Gui") `
        -NeedsGui

    Invoke-TestBuild `
        -Name "contour_feature_grouping_test" `
        -Sources @("tests\contour_feature_grouping_test.cpp", "src\ui\ContourFeatureGrouping.cpp") `
        -Libs @("-lQt5Core", "-lQt5Gui") `
        -NeedsGui

    Invoke-TestBuild `
        -Name "contour_machining_choice_test" `
        -Sources @("tests\contour_machining_choice_test.cpp", "src\ui\ContourMachiningChoice.cpp") `
        -Libs @("-lQt5Core", "-lQt5Gui") `
        -NeedsGui

    Invoke-TestBuild `
        -Name "tool_operation_compatibility_test" `
        -Sources @("tests\tool_operation_compatibility_test.cpp", "src\ui\ToolOperationCompatibility.cpp") `
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
        -Sources @("tests\operation_proposal_confirmation_test.cpp", "src\strategies\OperationProposal.cpp") `
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

    Invoke-OccFixtureTest `
        -Name "hole_step_fixture_test" `
        -TestSource "tests\hole_step_fixture_test.cpp"

    Invoke-OccFixtureTest `
        -Name "acceptance_step_model_test" `
        -TestSource "tests\acceptance_step_model_test.cpp" `
        -ExtraSources @(
            "src\services\ProgramGenerationService.cpp",
            "src\strategies\StrategyBase.cpp",
            "src\strategies\hole\PeckDrillingStrategy.cpp",
            "src\gcode\GCodeSafetyValidator.cpp",
            "src\gcode\GCodeModalOptimizer.cpp",
            "src\gcode\Cq8MacroProgramBuilder.cpp",
            "src\gcode\ProgramSnapshotFingerprint.cpp",
            "src\gcode\SiemensProgramPackage.cpp",
            "src\postprocessor\PostProcessorBase.cpp",
            "src\postprocessor\FanucPostProcessor.cpp",
            "src\postprocessor\Cq8PostProcessor.cpp"
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
        -Sources @("tests\project_manager_serialization_test.cpp", "src\core\ProjectManager.cpp", "src\core\SetupOrigin.cpp", "src\core\StockDefinition.cpp", "src\gcode\ProgramSnapshotFingerprint.cpp", "src\import\StepImporter.cpp", $ProjectManagerMoc) `
        -Libs @("-lQt5Core", "-lQt5Gui") `
        -NeedsGui

    $AppControllerMoc = Join-Path $DebugDir "moc_AppController_test.cpp"
    & moc src\core\AppController.h -o $AppControllerMoc
    if ($LASTEXITCODE -ne 0) {
        throw "AppController moc failed"
    }

    Invoke-TestBuild `
        -Name "app_controller_project_load_test" `
        -Sources @("tests\app_controller_project_load_test.cpp", "src\core\AppController.cpp", "src\core\ProjectManager.cpp", "src\core\SetupOrigin.cpp", "src\core\StockDefinition.cpp", "src\core\SourceFileFingerprint.cpp", "src\import\StepImporter.cpp", $ProjectManagerMoc, $AppControllerMoc) `
        -Libs @("-lQt5Core", "-lQt5Gui") `
        -NeedsGui
}
finally {
    Pop-Location
}
