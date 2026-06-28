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
 	src/generation/ContractChecker.cpp
 	src/generation/ProviderCatalog.cpp
 	src/generation/TeamRenderer.cpp
 	src/apply_helpers.cpp
 	src/models/Role.cpp
 	src/models/Specialist.cpp
 	src/models/Team.cpp
 	src/models/Trial.cpp
 	src/storage/StorageManager.cpp
 )

 set(LIB_HEADERS
 	src/generation/ContractChecker.h
 	src/generation/ProviderCatalog.h
 	src/generation/TeamRenderer.h
 	src/apply_helpers.h
 	src/models/ModelInfo.h
 	src/models/ProjectRecord.h
 	src/models/Role.h
 	src/models/Specialist.h
 	src/models/Team.h
 	src/models/Trial.h
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
	src/ui/AddSpecialistDialog.cpp
	src/ui/ConfigInspector.cpp
	src/ui/ConfirmApplyDialog.cpp
 	src/ui/EditSpecialistDialog.cpp
 	src/ui/FilterBar.cpp
 	src/ui/LabOverviewWidget.cpp
	src/ui/ModelsBrowserWidget.cpp
	src/ui/ProviderSubscriptionDialog.cpp
	src/ui/ProjectsWidget.cpp
	src/ui/PromptPreview.cpp
	src/ui/RoleEditorDialog.cpp
	src/ui/RolesWidget.cpp
	src/ui/TrialsWidget.cpp
	src/ui/TeamsWidget.cpp
	src/ui/TeamEditorWidget.cpp
	src/ui/TeamsDialog.cpp
	src/ui/TrialCompareDialog.cpp
)

 set(APP_HEADERS
	src/MainWindow.h
	src/ui/AddSpecialistDialog.h
	src/ui/ConfigInspector.h
	src/ui/ConfirmApplyDialog.h
 	src/ui/EditSpecialistDialog.h
 	src/ui/FilterBar.h
 	src/ui/LabOverviewWidget.h
	src/ui/ModelsBrowserWidget.h
	src/ui/ProviderSubscriptionDialog.h
	src/ui/ProjectsWidget.h
	src/ui/PromptPreview.h
	src/ui/RoleEditorDialog.h
	src/ui/RolesWidget.h
	src/ui/TrialsWidget.h
	src/ui/TeamsWidget.h
	src/ui/TeamEditorWidget.h
	src/ui/TeamsDialog.h
	src/ui/TrialCompareDialog.h
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
