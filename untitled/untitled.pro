QT += core gui widgets concurrent opengl
qtHaveModule(serialport) {
    QT += serialport
    DEFINES += CNEXT_ENABLE_SERIALPORT
}

greaterThan(QT_MAJOR_VERSION, 5): QT += openglwidgets

CONFIG += c++17
CONFIG -= app_bundle

TEMPLATE = app
TARGET   = CNEXT-CAM

DEFINES += QT_DEPRECATED_WARNINGS

TRANSLATIONS += \
    translations/zh_CN.ts \
    translations/en_US.ts

SOURCES += \
    main.cpp \
    src/app/AppController.cpp \
    src/core/ProjectManager.cpp \
    src/core/SourceFileFingerprint.cpp \
    src/workspaces/process_design/selection/FeatureIdentity.cpp \
    src/workspaces/process_design/selection/HoleSelectionSession.cpp \
    src/workspaces/process_design/selection/SelectionChainController.cpp \
    src/workspaces/process_design/ui/SelectionChainDialog.cpp \
    src/core/SetupOrientation.cpp \
    src/core/SetupOrigin.cpp \
    src/core/StockDefinition.cpp \
    src/workspaces/machine_operation/model/MachineProfileValidator.cpp \
    src/core/Settings.cpp \
    src/app/MainWindow.cpp \
    src/app/MillingBatchCommit.cpp \
    src/app/controllers/AppWorkspaceRouter.cpp \
    src/app/controllers/ProjectCommandController.cpp \
    src/app/controllers/SlotOperationController.cpp \
    src/workspaces/shared/ui/ViewportWidget.cpp \
    src/workspaces/process_design/ui/StrategyPanel.cpp \
    src/workspaces/process_design/ui/ParameterEditorDialog.cpp \
    src/workspaces/machine_operation/ui/MachineProfileDialog.cpp \
    src/workspaces/process_design/ui/CircleMillDialog.cpp \
    src/workspaces/process_design/ui/MillingOperationDialog.cpp \
    src/workspaces/process_design/ui/ToolLibraryPanel.cpp \
    src/workspaces/process_design/ui/FeatureDisplayFilter.cpp \
    src/workspaces/process_design/ui/ContourMachiningChoice.cpp \
    src/workspaces/process_design/ui/ContourMachiningChoiceDialog.cpp \
    src/workspaces/process_design/ui/ToolOperationCompatibility.cpp \
    src/workspaces/process_design/ui/ContourFeatureGrouping.cpp \
    src/workspaces/process_design/ui/HoleFeatureGrouping.cpp \
    src/workspaces/process_design/ui/HoleSelectionDialog.cpp \
    src/workspaces/process_design/ui/SetupOriginDialog.cpp \
    src/workspaces/process_design/ui/StockDefinitionDialog.cpp \
    src/workspaces/process_design/ui/FeatureListPanel.cpp \
    src/workspaces/shared/ui/BottomBar.cpp \
    src/workspaces/program_verification/ui/GCodeEditor.cpp \
    src/workspaces/program_verification/ui/GCodeHighlighter.cpp \
    src/workspaces/machine_operation/ui/CncSendDialog.cpp \
    src/workspaces/process_design/ui/OperationListPanel.cpp \
    src/import/StepImporter.cpp \
    src/import/TopoAnalyzer.cpp \
    src/import/FeatureRecognizer.cpp \
    src/import/FeatureClassifier.cpp \
    src/strategies/hole/SpotDrillingStrategy.cpp \
    src/strategies/hole/PeckDrillingStrategy.cpp \
    src/strategies/hole/DeepHoleDrillingStrategy.cpp \
    src/strategies/hole/TappingStrategy.cpp \
    src/strategies/hole/ReamingStrategy.cpp \
    src/strategies/hole/ChamferStrategy.cpp \
    src/strategies/hole/HoleCircularMillingStrategy.cpp \
    src/strategies/mill/CircleMillingStrategy.cpp \
    src/strategies/mill/FaceMillingStrategy.cpp \
    src/strategies/mill/PocketRoughingStrategy.cpp \
    src/strategies/mill/PocketFinishStrategy.cpp \
    src/strategies/mill/PocketFloorFinishStrategy.cpp \
    src/strategies/mill/ContourFinishStrategy.cpp \
    src/strategies/mill/SurfaceFinishStrategy.cpp \
    src/strategies/mill/ClosedContourMillingStrategy.cpp \
    src/strategies/mill/OpenContourMillingStrategy.cpp \
    src/strategies/mill/SlotMachiningGeometry.cpp \
    src/strategies/mill/SlotMillingStrategy.cpp \
    src/strategies/mill/BlindSlotMillingStrategy.cpp \
    src/strategies/mill/TaperedSlotMillingStrategy.cpp \
    src/strategies/StrategyBase.cpp \
    src/strategies/StrategyFactory.cpp \
    src/workspaces/process_design/operations/OperationProposal.cpp \
    src/postprocessor/PostProcessorBase.cpp \
    src/postprocessor/SiemensPostProcessor.cpp \
    src/postprocessor/FanucPostProcessor.cpp \
    src/postprocessor/Cq8PostProcessor.cpp \
    src/postprocessor/PostProcessorRegistry.cpp \
    src/workspaces/program_verification/safety/GCodeSafetyValidator.cpp \
    src/gcode/GCodeModalOptimizer.cpp \
    src/gcode/Cq8MacroProgramBuilder.cpp \
    src/gcode/ProgramPackageExporter.cpp \
    src/gcode/SiemensProgramPackage.cpp \
    src/workspaces/program_verification/fingerprint/ProgramSnapshotFingerprint.cpp \
    src/gcode/ProgramSnapshotStatus.cpp \
    src/workspaces/program_verification/generation/ProgramGenerationService.cpp \
    src/workspaces/program_verification/simulation/SimulationController.cpp \
    src/workspaces/machine_operation/communication/CncCommInterface.cpp \
    src/tool/ToolLibrary.cpp \
    src/tool/ToolEntry.cpp

HEADERS += \
    src/app/AppController.h \
    src/workspaces/machine_operation/model/MachineProfile.h \
    src/workspaces/machine_operation/model/MachineProfileValidator.h \
    src/core/ProjectManager.h \
    src/core/SourceFileFingerprint.h \
    src/workspaces/process_design/selection/FeatureIdentity.h \
    src/workspaces/process_design/selection/HoleSelectionSession.h \
    src/workspaces/process_design/operations/OperationWorkflow.h \
    src/workspaces/process_design/selection/SelectionChainController.h \
    src/workspaces/process_design/selection/SelectionCandidate.h \
    src/workspaces/process_design/ui/SelectionChainDialog.h \
    src/workspaces/shared/WorkspaceModule.h \
    src/workspaces/shared/WorkspaceContracts.h \
    src/workspaces/process_design/ProcessDesignWorkspaceFacade.h \
    src/workspaces/program_verification/ProgramVerificationWorkspaceFacade.h \
    src/workspaces/machine_operation/MachineOperationWorkspaceFacade.h \
    src/core/StrategyParams.h \
    src/core/SetupOrientation.h \
    src/core/SetupOrigin.h \
    src/core/StockDefinition.h \
    src/core/Settings.h \
    src/app/MainWindow.h \
    src/app/MillingBatchCommit.h \
    src/app/controllers/AppWorkspaceRouter.h \
    src/app/controllers/ProjectCommandController.h \
    src/app/controllers/SlotOperationController.h \
    src/workspaces/process_design/operations/SlotOperationState.h \
    src/workspaces/shared/ui/ViewportWidget.h \
    src/workspaces/process_design/ui/StrategyPanel.h \
    src/workspaces/process_design/ui/ParameterEditorDialog.h \
    src/workspaces/machine_operation/ui/MachineProfileDialog.h \
    src/workspaces/process_design/ui/CircleMillDialog.h \
    src/workspaces/process_design/ui/MillingOperationDialog.h \
    src/workspaces/process_design/ui/ToolLibraryPanel.h \
    src/workspaces/process_design/ui/FeatureDisplayFilter.h \
    src/workspaces/process_design/ui/ContourMachiningChoice.h \
    src/workspaces/process_design/ui/ContourMachiningChoiceDialog.h \
    src/workspaces/process_design/ui/ToolOperationCompatibility.h \
    src/workspaces/process_design/ui/ContourFeatureGrouping.h \
    src/workspaces/process_design/ui/HoleFeatureGrouping.h \
    src/workspaces/process_design/ui/HoleSelectionDialog.h \
    src/workspaces/process_design/ui/SetupOriginDialog.h \
    src/workspaces/process_design/ui/StockDefinitionDialog.h \
    src/workspaces/process_design/ui/FeatureListPanel.h \
    src/workspaces/shared/ui/BottomBar.h \
    src/workspaces/program_verification/ui/GCodeEditor.h \
    src/workspaces/program_verification/ui/GCodeHighlighter.h \
    src/workspaces/machine_operation/ui/CncSendDialog.h \
    src/workspaces/process_design/ui/OperationListPanel.h \
    src/import/StepImporter.h \
    src/import/TopoAnalyzer.h \
    src/import/FeatureRecognizer.h \
    src/import/FeatureClassifier.h \
    src/strategies/hole/SpotDrillingStrategy.h \
    src/strategies/hole/PeckDrillingStrategy.h \
    src/strategies/hole/DeepHoleDrillingStrategy.h \
    src/strategies/hole/TappingStrategy.h \
    src/strategies/hole/ReamingStrategy.h \
    src/strategies/hole/ChamferStrategy.h \
    src/strategies/hole/HoleCircularMillingStrategy.h \
    src/strategies/mill/CircleMillingStrategy.h \
    src/strategies/mill/FaceMillingStrategy.h \
    src/strategies/mill/PocketRoughingStrategy.h \
    src/strategies/mill/PocketFinishStrategy.h \
    src/strategies/mill/PocketFloorFinishStrategy.h \
    src/strategies/mill/ContourFinishStrategy.h \
    src/strategies/mill/SurfaceFinishStrategy.h \
    src/strategies/mill/ClosedContourMillingStrategy.h \
    src/strategies/mill/OpenContourMillingStrategy.h \
    src/strategies/mill/SlotMachiningGeometry.h \
    src/strategies/mill/SlotMillingStrategy.h \
    src/strategies/mill/BlindSlotMillingStrategy.h \
    src/strategies/mill/TaperedSlotMillingStrategy.h \
    src/strategies/StrategyBase.h \
    src/strategies/StrategyFactory.h \
    src/workspaces/process_design/operations/MachiningOperation.h \
    src/workspaces/process_design/operations/OperationProposal.h \
    src/postprocessor/PostProcessorBase.h \
    src/postprocessor/SiemensPostProcessor.h \
    src/postprocessor/FanucPostProcessor.h \
    src/postprocessor/Cq8PostProcessor.h \
    src/postprocessor/PostProcessorRegistry.h \
    src/workspaces/program_verification/safety/GCodeSafetyValidator.h \
    src/gcode/GCodeModalOptimizer.h \
    src/gcode/Cq8MacroProgramBuilder.h \
    src/gcode/ParametricToolpathProgram.h \
    src/gcode/ProgramPackageExporter.h \
    src/gcode/SiemensProgramPackage.h \
    src/workspaces/program_verification/fingerprint/ProgramSnapshotFingerprint.h \
    src/gcode/ProgramSnapshotStatus.h \
    src/workspaces/program_verification/generation/ProgramGenerationService.h \
    src/workspaces/program_verification/simulation/SimulationController.h \
    src/workspaces/machine_operation/communication/CncCommInterface.h \
    src/tool/ToolLibrary.h \
    src/tool/ToolEntry.h

# ------------------------------------------------------------------
# OpenCAMLib (vendored source)
# ------------------------------------------------------------------
CNEXT_OCL_SRC = $$PWD/third_party/opencamlib/src
exists($$CNEXT_OCL_SRC/ocl.hpp) {
    DEFINES += CNEXT_ENABLE_OCL

    INCLUDEPATH += \
        $$CNEXT_OCL_SRC \
        $$CNEXT_OCL_SRC/algo \
        $$CNEXT_OCL_SRC/common \
        $$CNEXT_OCL_SRC/cutters \
        $$CNEXT_OCL_SRC/dropcutter \
        $$CNEXT_OCL_SRC/geo

    CNEXT_BOOST_PREFIX = $$(CNEXT_BOOST_PREFIX)
    isEmpty(CNEXT_BOOST_PREFIX) {
        exists(D:/boost/boost_1_88_0/boost/version.hpp): CNEXT_BOOST_PREFIX = D:/boost/boost_1_88_0
    }
    isEmpty(CNEXT_BOOST_PREFIX) {
        exists(C:/boost/boost_1_88_0/boost/version.hpp): CNEXT_BOOST_PREFIX = C:/boost/boost_1_88_0
    }
    !isEmpty(CNEXT_BOOST_PREFIX) {
        INCLUDEPATH += $$CNEXT_BOOST_PREFIX
    } else {
        warning("OpenCAMLib: Boost not detected. Set CNEXT_BOOST_PREFIX if build fails.")
    }

    OCL_SOURCES = \
        $$files($$CNEXT_OCL_SRC/*.cpp, false) \
        $$files($$CNEXT_OCL_SRC/algo/*.cpp, false) \
        $$files($$CNEXT_OCL_SRC/common/*.cpp, false) \
        $$files($$CNEXT_OCL_SRC/cutters/*.cpp, false) \
        $$files($$CNEXT_OCL_SRC/dropcutter/*.cpp, false) \
        $$files($$CNEXT_OCL_SRC/geo/*.cpp, false)
    SOURCES += $$OCL_SOURCES
} else {
    warning("OpenCAMLib: vendored sources not found at third_party/opencamlib/src. OCL support disabled.")
}

# ------------------------------------------------------------------
# OpenCASCADE (STEP parsing / geometry)
# ------------------------------------------------------------------
CNEXT_OCC_PREFIX = $$(CNEXT_OCC_PREFIX)
contains(QT_ARCH, i386) {
    isEmpty(CNEXT_OCC_PREFIX) {
        exists(D:/msys64/mingw32/include/opencascade): CNEXT_OCC_PREFIX = D:/msys64/mingw32
    }
    isEmpty(CNEXT_OCC_PREFIX) {
        exists(C:/msys64/mingw32/include/opencascade): CNEXT_OCC_PREFIX = C:/msys64/mingw32
    }
} else {
    isEmpty(CNEXT_OCC_PREFIX) {
        exists(D:/msys64/mingw64/include/opencascade): CNEXT_OCC_PREFIX = D:/msys64/mingw64
    }
    isEmpty(CNEXT_OCC_PREFIX) {
        exists(D:/msys64/ucrt64/include/opencascade): CNEXT_OCC_PREFIX = D:/msys64/ucrt64
    }
    isEmpty(CNEXT_OCC_PREFIX) {
        exists(C:/msys64/mingw64/include/opencascade): CNEXT_OCC_PREFIX = C:/msys64/mingw64
    }
    isEmpty(CNEXT_OCC_PREFIX) {
        exists(C:/msys64/ucrt64/include/opencascade): CNEXT_OCC_PREFIX = C:/msys64/ucrt64
    }
}

!isEmpty(CNEXT_OCC_PREFIX) {
    OCC_STEP_LIB = $$CNEXT_OCC_PREFIX/lib/libTKDESTEP.dll.a
    exists($$OCC_STEP_LIB) {
        DEFINES += CNEXT_ENABLE_OCC
        INCLUDEPATH += $$CNEXT_OCC_PREFIX/include/opencascade
        QMAKE_LIBDIR += $$CNEXT_OCC_PREFIX/lib

        OCC_TK_LIBS = TKDESTEP TKDE TKXSBase TKXml TKXmlL TKCDF TKMesh TKTopAlgo TKGeomAlgo TKShHealing TKBool TKPrim TKBO TKBRep TKGeomBase TKG3d TKG2d TKMath TKernel
        for (tk, OCC_TK_LIBS) {
            LIBS += $$shell_path($$CNEXT_OCC_PREFIX/lib/lib$${tk}.dll.a)
        }
        LIBS += -lfreetype -ltbb12 -ltbbmalloc -lz

        win32 {
            OCC_RUNTIME_DIR = $$CNEXT_OCC_PREFIX/bin
            QT_RUNTIME_DIR  = $$[QT_INSTALL_BINS]
            QT_PLUGIN_DIR   = $$[QT_INSTALL_PLUGINS]
            defineReplace(copyFileCmd) {
                src = $$1
                dst = $$2
                return(copy /y \"$$src\" \"$$dst\")
            }
            CONFIG(debug, debug|release) {
                CNEXT_OUT = $$OUT_PWD/debug
            } else {
                CNEXT_OUT = $$OUT_PWD/release
            }
            CNEXT_DEPLOY_COMMANDS += if not exist $$shell_path($$CNEXT_OUT) mkdir $$shell_path($$CNEXT_OUT) $$escape_expand(\n\t)
            CNEXT_DEPLOY_COMMANDS += if not exist $$shell_path($$CNEXT_OUT/platforms) mkdir $$shell_path($$CNEXT_OUT/platforms) $$escape_expand(\n\t)

            OCC_RUNTIME_DLLS = \
                libTKDESTEP.dll libTKDE.dll libTKXSBase.dll \
                libTKLCAF.dll libTKXCAF.dll libTKCAF.dll \
                libTKXml.dll libTKXmlL.dll libTKCDF.dll \
                libTKMesh.dll libTKTopAlgo.dll libTKGeomAlgo.dll \
                libTKShHealing.dll libTKBool.dll libTKPrim.dll \
                libTKBO.dll libTKBRep.dll libTKGeomBase.dll \
                libTKG3d.dll libTKG2d.dll libTKMath.dll libTKernel.dll \
                libTKService.dll libTKV3d.dll libTKVCAF.dll \
                libfreetype-6.dll libtbb12.dll libtbbmalloc.dll \
                libgcc_s_seh-1.dll libstdc++-6.dll libwinpthread-1.dll \
                libbz2-1.dll libbrotlicommon.dll libbrotlidec.dll \
                libbrotlienc.dll libdouble-conversion.dll libexpat-1.dll \
                libfreeimage-3.dll libgraphite2.dll libharfbuzz-0.dll \
                libgmp-10.dll libhogweed-6.dll \
                libiconv-2.dll libintl-8.dll liblzma-5.dll \
                libidn2-0.dll libnettle-8.dll \
                libpcre2-8-0.dll libpcre2-16-0.dll \
                libpng16-16.dll libzstd.dll zlib1.dll \
                avcodec-62.dll avformat-62.dll avutil-60.dll \
                swresample-6.dll swscale-9.dll \
                libIex-3_4.dll libImath-3_2.dll libOpenEXR-3_4.dll \
                libaom.dll libdav1d-7.dll libglib-2.0-0.dll \
                libgsm.dll libjpeg-8.dll libjxl.dll libjxl_threads.dll \
                libmp3lame-0.dll libopenjp2-7.dll libopus-0.dll \
                libraw-24.dll libspeex-1.dll libtiff-6.dll \
                libvorbis-0.dll libvorbisenc-2.dll libvpx-1.dll \
                libwebp-7.dll libwebpmux-3.dll libx264-165.dll libx265-215.dll \
                libxml2-16.dll libzvbi-0.dll xvidcore.dll \
                libbluray-3.dll libcairo-2.dll libcairo-gobject-2.dll \
                libcrypto-3-x64.dll libdatrie-1.dll libdeflate.dll libffi-8.dll \
                libfontconfig-1.dll libfribidi-0.dll libgdk_pixbuf-2.0-0.dll \
                libgio-2.0-0.dll libgme.dll libgmodule-2.0-0.dll \
                libgnutls-30.dll libgobject-2.0-0.dll libgomp-1.dll libhwy.dll \
                libIlmThread-3_4.dll libjbig-0.dll libjpegxr.dll libjxl_cms.dll \
                libjxrglue.dll liblc3-1.dll liblcms2-2.dll libLerc.dll \
                libmodplug-1.dll libogg-0.dll libopencore-amrnb-0.dll \
                libopencore-amrwb-0.dll libOpenEXRCore-3_4.dll \
                libopenjph-0.26.dll libopenvr_api.dll libp11-kit-0.dll \
                libpango-1.0-0.dll libpangocairo-1.0-0.dll \
                libpangoft2-1.0-0.dll libpangowin32-1.0-0.dll libpixman-1-0.dll \
                librav1e.dll librsvg-2-2.dll librtmp-1.dll \
                libshaderc_shared.dll libsharpyuv-0.dll libsoxr.dll libsrt.dll \
                libssh.dll libSvtAv1Enc-4.dll libtasn1-6.dll libthai-0.dll \
                libtheoradec-2.dll libtheoraenc-2.dll libTKHLR.dll \
                libunistring-5.dll libva_win32.dll libva.dll libvpl-2.dll

            for (dll, OCC_RUNTIME_DLLS) {
                exists($$OCC_RUNTIME_DIR/$${dll}) {
                    CNEXT_DEPLOY_COMMANDS += $$copyFileCmd($$shell_path($$OCC_RUNTIME_DIR/$${dll}), $$shell_path($$CNEXT_OUT/$${dll})) $$escape_expand(\n\t)
                }
            }

            QT_RUNTIME_DLLS = Qt5Core.dll Qt5Gui.dll Qt5Widgets.dll Qt5Concurrent.dll Qt5OpenGL.dll
            qtHaveModule(serialport): QT_RUNTIME_DLLS += Qt5SerialPort.dll
            for (dll, QT_RUNTIME_DLLS) {
                exists($$QT_RUNTIME_DIR/$${dll}) {
                    CNEXT_DEPLOY_COMMANDS += $$copyFileCmd($$shell_path($$QT_RUNTIME_DIR/$${dll}), $$shell_path($$CNEXT_OUT/$${dll})) $$escape_expand(\n\t)
                }
            }
            CNEXT_DEPLOY_COMMANDS += if not exist $$shell_path($$CNEXT_OUT/translations) mkdir $$shell_path($$CNEXT_OUT/translations) $$escape_expand(\n\t)
            for (qm, TRANSLATIONS) {
                qmfile = $$replace(qm, \\.ts$, .qm)
                exists($$PWD/$${qmfile}) {
                    CNEXT_DEPLOY_COMMANDS += $$copyFileCmd($$shell_path($$PWD/$${qmfile}), $$shell_path($$CNEXT_OUT/$${qmfile})) $$escape_expand(\n\t)
                }
            }
            exists($$QT_PLUGIN_DIR/platforms/qwindows.dll) {
                CNEXT_DEPLOY_COMMANDS += $$copyFileCmd($$shell_path($$QT_PLUGIN_DIR/platforms/qwindows.dll), $$shell_path($$CNEXT_OUT/platforms/qwindows.dll)) $$escape_expand(\n\t)
            }
            QMAKE_POST_LINK += $$CNEXT_DEPLOY_COMMANDS
            copy_runtime.target = copy_runtime
            copy_runtime.commands = $$CNEXT_DEPLOY_COMMANDS
            QMAKE_EXTRA_TARGETS += copy_runtime
        }
    } else {
        warning("OpenCascade: headers found but libTKDESTEP.dll.a missing. OCC support disabled.")
    }
} else {
    contains(QT_ARCH, i386) {
        warning("OpenCascade: 32-bit MSYS2 OpenCascade not detected. Set CNEXT_OCC_PREFIX=D:/msys64/mingw32 or use a 64-bit Qt kit.")
    } else {
        warning("OpenCascade: MSYS2 OpenCascade not detected. Set CNEXT_OCC_PREFIX=D:/msys64/mingw64 in Qt Creator.")
    }
}

# ------------------------------------------------------------------
# Stub: License/auth placeholder (enable when implemented)
# ------------------------------------------------------------------
# DEFINES += CNEXT_ENABLE_LICENSE

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target
