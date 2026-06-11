find_package(Qt6 REQUIRED COMPONENTS Test)

function(add_qt_test name source)
    add_executable(${name} ${source})
    target_include_directories(${name} PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(${name} PRIVATE Qt6::Core Qt6::Test opencode-meta-lib)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

add_qt_test(test_models    test_models.cpp)
add_qt_test(test_generation test_generation.cpp)
