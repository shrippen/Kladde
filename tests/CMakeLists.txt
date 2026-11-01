find_package(Qt6 REQUIRED COMPONENTS Test)

foreach(name test_mapper test_batch test_client)
    qt_add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE kladde-core Qt6::Test)
    target_compile_definitions(${name} PRIVATE
        KLADDE_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
    add_test(NAME ${name} COMMAND ${name})
endforeach()
