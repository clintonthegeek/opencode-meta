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
    ${CMAKE_SOURCE_DIR}/src/ui/ConfigInspector.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/ConfirmApplyDialog.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/ConfirmApplyDialog.h
    ${CMAKE_SOURCE_DIR}/src/ui/EditSpecialistDialog.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/LabOverviewWidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/ModelsBrowserWidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/ProviderSubscriptionDialog.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/ProjectsWidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/PromptPreview.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/RoleEditorDialog.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/RolesWidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/TeamsDialog.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/TeamsWidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/TeamEditorWidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/TrialsWidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/TrialCompareDialog.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/TrialCompareDialog.h
)
add_executable(test_cross_view_smoke test_cross_view_smoke.cpp ${F5_UI_SOURCES})
target_include_directories(test_cross_view_smoke PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_cross_view_smoke PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network Qt6::Test opencode-meta-lib)
add_test(NAME test_cross_view_smoke COMMAND test_cross_view_smoke)

   add_qt_test(test_team_renderer           test_team_renderer.cpp)
   add_qt_test(test_apply_team              test_apply_team.cpp)
   add_qt_test(test_starter_team_apply      test_starter_team_apply.cpp)
   add_qt_test(test_teams_storage           test_teams_storage.cpp)
   add_qt_test(test_roles_storage           test_roles_storage.cpp)
   add_qt_test(test_trials_storage          test_trials_storage.cpp)
   add_qt_test(test_contract_checker        test_contract_checker.cpp)

# RoleEditorDialog pulls in the QDialog UI surface; the test target needs
# the matching .cpp to satisfy the linker (the static lib deliberately
# excludes UI sources so non-UI tests don't pay for them).
add_executable(test_role_editor_dialog
    test_role_editor_dialog.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/RoleEditorDialog.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/RoleEditorDialog.h
)
target_include_directories(test_role_editor_dialog PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_role_editor_dialog PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test opencode-meta-lib
)
add_test(NAME test_role_editor_dialog COMMAND test_role_editor_dialog)

# EditSpecialistDialog (P1-4) also needs its QDialog UI source linked
# alongside the test target, plus ModelsBrowserWidget + PromptPreview
# because the dialog constructor connects signals from the embedded
# picker and instantiates PromptPreview as a child widget. Mirrors the
# role-editor pattern but pulls in the broader UI graph.
add_executable(test_edit_specialist_dialog
    test_edit_specialist_dialog.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/EditSpecialistDialog.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/EditSpecialistDialog.h
    ${CMAKE_SOURCE_DIR}/src/ui/ModelsBrowserWidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/ModelsBrowserWidget.h
    ${CMAKE_SOURCE_DIR}/src/ui/ProviderSubscriptionDialog.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/ProviderSubscriptionDialog.h
    ${CMAKE_SOURCE_DIR}/src/ui/PromptPreview.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/PromptPreview.h
)
target_include_directories(test_edit_specialist_dialog PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_edit_specialist_dialog PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network Qt6::Test opencode-meta-lib
)
add_test(NAME test_edit_specialist_dialog COMMAND test_edit_specialist_dialog)

# ConfirmApplyDialog (P1-5) is a self-contained QDialog — it depends
# only on the lib + its own UI source. Mirrors the role-editor test
# pattern.
add_executable(test_confirm_apply_dialog
    test_confirm_apply_dialog.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/ConfirmApplyDialog.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/ConfirmApplyDialog.h
)
target_include_directories(test_confirm_apply_dialog PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_confirm_apply_dialog PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test opencode-meta-lib
)
add_test(NAME test_confirm_apply_dialog COMMAND test_confirm_apply_dialog)

# TrialCompareDialog (P2-1) follows the same self-contained QDialog
# pattern. It depends on TeamRenderer for the fallback re-render
# path, which is already in the lib, so the only extra source we need
# is the dialog itself.
add_executable(test_trial_compare_dialog
    test_trial_compare_dialog.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/TrialCompareDialog.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/TrialCompareDialog.h
)
target_include_directories(test_trial_compare_dialog PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_trial_compare_dialog PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test opencode-meta-lib
)
add_test(NAME test_trial_compare_dialog COMMAND test_trial_compare_dialog)
