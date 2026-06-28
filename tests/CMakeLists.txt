find_package(Qt6 REQUIRED COMPONENTS Test Network)

function(add_qt_test name source)
    add_executable(${name} ${source})
    target_include_directories(${name} PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(${name} PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test opencode-meta-lib)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

# F5: this smoke test drives the entire Role -> Teams -> Trials pipeline
# including MainWindow, TeamsWidget, TeamEditorWidget, AddSpecialistDialog,
# TeamsDialog, and TrialsWidget. It pulls the application's UI sources
# directly into its own executable so the test target has full access to
# the UI symbols (the application's main.cpp is excluded — the test brings
# its own QTEST_MAIN entry point).
set(F5_UI_SOURCES
    ${CMAKE_SOURCE_DIR}/src/MainWindow.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/AddSpecialistDialog.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/LabOverviewWidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/ModelsBrowserWidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/ProviderSubscriptionDialog.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/ProjectsWidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/RoleEditorDialog.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/RolesWidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/TeamsDialog.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/TeamsWidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/TeamEditorWidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/TrialsWidget.cpp
)
add_executable(test_cross_view_smoke test_cross_view_smoke.cpp ${F5_UI_SOURCES})
target_include_directories(test_cross_view_smoke PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_cross_view_smoke PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network Qt6::Test opencode-meta-lib)
add_test(NAME test_cross_view_smoke COMMAND test_cross_view_smoke)

   add_qt_test(test_team_renderer           test_team_renderer.cpp)
   add_qt_test(test_apply_team              test_apply_team.cpp)
   add_qt_test(test_starter_team_apply      test_starter_team_apply.cpp)
   add_qt_test(test_teams_storage           test_teams_storage.cpp)
   add_qt_test(test_contract_checker        test_contract_checker.cpp)
