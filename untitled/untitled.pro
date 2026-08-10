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
    src/core/AppController.cpp \
    src/core/ProjectManager.cpp \
    src/core/SourceFileFingerprint.cpp \
    src/core/SetupOrientation.cpp \
    src/core/SetupOrigin.cpp \
    src/core/StockDefinition.cpp \
    src/core/MachineProfileValidator.cpp \
    src/core/Settings.cpp \
    src/ui/MainWindow.cpp \
    src/ui/ViewportWidget.cpp \
    src/ui/StrategyPanel.cpp \
    src/ui/ParameterEditorDialog.cpp \
    src/ui/MachineProfileDialog.cpp \
    src/ui/CircleMillDialog.cpp \
    src/ui/MillingOperationDialog.cpp \
    src/ui/ToolLibraryPanel.cpp \
    src/ui/FeatureDisplayFilter.cpp \
    src/ui/ContourMachiningChoice.cpp \
    src/ui/ContourMachiningChoiceDialog.cpp \
    src/ui/ToolOperationCompatibility.cpp \
    src/ui/ContourFeatureGrouping.cpp \
    src/ui/HoleFeatureGrouping.cpp \
    src/ui/SetupOriginDialog.cpp \
    src/ui/StockDefinitionDialog.cpp \
    src/ui/FeatureListPanel.cpp \
    src/ui/BottomBar.cpp \
    src/ui/GCodeEditor.cpp \
    src/ui/GCodeHighlighter.cpp \
    src/ui/CncSendDialog.cpp \
    src/ui/OperationListPanel.cpp \
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
    src/strategies/OperationProposal.cpp \
    src/postprocessor/PostProcessorBase.cpp \
    src/postprocessor/SiemensPostProcessor.cpp \
    src/postprocessor/FanucPostProcessor.cpp \
    src/postprocessor/PostProcessorRegistry.cpp \
    src/gcode/GCodeSafetyValidator.cpp \
    src/gcode/ProgramPackageExporter.cpp \
    src/gcode/SiemensProgramPackage.cpp \
    src/gcode/ProgramSnapshotFingerprint.cpp \
    src/gcode/ProgramSnapshotStatus.cpp \
    src/services/ProgramGenerationService.cpp \
    src/simulation/SimulationController.cpp \
    src/communication/CncCommInterface.cpp \
    src/tool/ToolLibrary.cpp \
    src/tool/ToolEntry.cpp

HEADERS += \
    src/core/AppController.h \
    src/core/MachineProfile.h \
    src/core/MachineProfileValidator.h \
    src/core/ProjectManager.h \
    src/core/SourceFileFingerprint.h \
    src/core/SetupOrientation.h \
    src/core/SetupOrigin.h \
    src/core/StockDefinition.h \
    src/core/Settings.h \
    src/ui/MainWindow.h \
    src/ui/ViewportWidget.h \
    src/ui/StrategyPanel.h \
    src/ui/ParameterEditorDialog.h \
    src/ui/MachineProfileDialog.h \
    src/ui/CircleMillDialog.h \
    src/ui/MillingOperationDialog.h \
    src/ui/ToolLibraryPanel.h \
    src/ui/FeatureDisplayFilter.h \
    src/ui/ContourMachiningChoice.h \
    src/ui/ContourMachiningChoiceDialog.h \
    src/ui/ToolOperationCompatibility.h \
    src/ui/ContourFeatureGrouping.h \
    src/ui/HoleFeatureGrouping.h \
    src/ui/SetupOriginDialog.h \
    src/ui/StockDefinitionDialog.h \
    src/ui/FeatureListPanel.h \
    src/ui/BottomBar.h \
    src/ui/GCodeEditor.h \
    src/ui/GCodeHighlighter.h \
    src/ui/CncSendDialog.h \
    src/ui/OperationListPanel.h \
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
    src/strategies/MachiningOperation.h \
    src/strategies/OperationProposal.h \
    src/postprocessor/PostProcessorBase.h \
    src/postprocessor/SiemensPostProcessor.h \
    src/postprocessor/FanucPostProcessor.h \
    src/postprocessor/PostProcessorRegistry.h \
    src/gcode/GCodeSafetyValidator.h \
    src/gcode/ProgramPackageExporter.h \
    src/gcode/SiemensProgramPackage.h \
    src/gcode/ProgramSnapshotFingerprint.h \
    src/gcode/ProgramSnapshotStatus.h \
    src/services/ProgramGenerationService.h \
    src/simulation/SimulationController.h \
    src/communication/CncCommInterface.h \
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
                libxml2-16.dll libzvbi-0.dll xvidcore.dll

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
