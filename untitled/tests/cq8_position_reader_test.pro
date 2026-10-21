QT = core
CONFIG += console c++17
CONFIG -= app_bundle
TEMPLATE = app
TARGET = cq8_position_reader_test
INCLUDEPATH += ../src/workspaces/machine_operation/communication
SOURCES += cq8_position_reader_test.cpp \
    ../src/workspaces/machine_operation/communication/Cq8PositionReader.cpp
HEADERS += ../src/workspaces/machine_operation/communication/Cq8PositionReader.h
