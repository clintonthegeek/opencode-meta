cmake_minimum_required(VERSION 3.19)

project(opencode-meta-qt VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets)

set(SOURCES
	src/main.cpp
	src/MainWindow.cpp
	src/models/Template.cpp
	src/models/Profile.cpp
	src/storage/StorageManager.cpp
)

set(HEADERS
	src/MainWindow.h
	src/models/Template.h
	src/models/Profile.h
	src/models/ModelInfo.h
	src/models/ProjectRecord.h
	src/storage/StorageManager.h
)

qt_add_executable(opencode-meta-qt
    MANUAL_FINALIZATION
    ${SOURCES}
    ${HEADERS}
)

target_link_libraries(opencode-meta-qt PRIVATE
    Qt6::Core
    Qt6::Widgets
)

qt_finalize_executable(opencode-meta-qt)
