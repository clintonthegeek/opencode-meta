cmake_minimum_required(VERSION 3.19)

project(opencode-meta-qt VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets Network Test)

# Core library: models, storage, and generation — no UI, testable independently
set(LIB_SOURCES
	src/generation.cpp
	src/models/Template.cpp
	src/models/Profile.cpp
	src/storage/StorageManager.cpp
)

set(LIB_HEADERS
	src/generation.h
	src/models/Template.h
	src/models/Profile.h
	src/models/ModelInfo.h
	src/models/ProjectRecord.h
	src/storage/StorageManager.h
)

add_library(opencode-meta-lib STATIC ${LIB_SOURCES} ${LIB_HEADERS})

target_include_directories(opencode-meta-lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(opencode-meta-lib PUBLIC Qt6::Core)

# Main application
set(APP_SOURCES
	src/main.cpp
	src/MainWindow.cpp
	src/ui/ModelsBrowserWidget.cpp
	src/ui/ProviderSubscriptionDialog.cpp
	src/ui/ProjectsWidget.cpp
	src/ui/TemplatesWidget.cpp
	src/ui/TemplateEditorDialog.cpp
	src/ui/ProfilesWidget.cpp
	src/ui/ProfileEditorDialog.cpp
)

set(APP_HEADERS
	src/MainWindow.h
	src/ui/ModelsBrowserWidget.h
	src/ui/ProviderSubscriptionDialog.h
	src/ui/ProjectsWidget.h
	src/ui/TemplatesWidget.h
	src/ui/TemplateEditorDialog.h
	src/ui/ProfilesWidget.h
	src/ui/ProfileEditorDialog.h
)

qt_add_executable(opencode-meta-qt
    MANUAL_FINALIZATION
    ${APP_SOURCES}
    ${APP_HEADERS}
)

target_include_directories(opencode-meta-qt PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(opencode-meta-qt PRIVATE
    opencode-meta-lib
    Qt6::Widgets
    Qt6::Network
)

qt_finalize_executable(opencode-meta-qt)

# Tests
enable_testing()
add_subdirectory(tests)
