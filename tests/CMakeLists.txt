find_package(Qt6 REQUIRED COMPONENTS Test)

foreach(name test_mapper test_batch test_client test_api test_session)
    qt_add_executable(${name} ${name}.cpp fakeserver.h)
    target_link_libraries(${name} PRIVATE kladde-core Qt6::Test)
    target_compile_definitions(${name} PRIVATE
        KLADDE_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
    add_test(NAME ${name} COMMAND ${name})
endforeach()

if(KLADDE_APP AND NOT KLADDE_DEMO)
    add_test(NAME no_demo_in_app COMMAND ${CMAKE_COMMAND} -DAPP=$<TARGET_FILE:kladde> -P ${CMAKE_CURRENT_SOURCE_DIR}/no_demo.cmake)
endif()
