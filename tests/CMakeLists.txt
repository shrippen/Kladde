find_package(Qt6 REQUIRED COMPONENTS Test)

foreach(name test_mapper test_batch test_client test_api test_session)
    qt_add_executable(${name} ${name}.cpp fakeserver.h)
    target_link_libraries(${name} PRIVATE kladde-core Qt6::Test)
    target_compile_definitions(${name} PRIVATE
        KLADDE_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
    add_test(NAME ${name} COMMAND ${name})
endforeach()

# The app's account (sign-in, errors, the sync's token) without QML, with a fake sync.
if(KLADDE_APP)
    set(app ${PROJECT_SOURCE_DIR}/app)
    qt_add_executable(test_account test_account.cpp syncbridge_fake.h syncbridge_fake.cpp
        ${app}/account.cpp ${app}/errors.cpp ${app}/syncbridge.cpp)
    target_include_directories(test_account PRIVATE ${app})
    target_link_libraries(test_account PRIVATE kladde-core Qt6::Test)
    add_test(NAME test_account COMMAND test_account)
endif()

if(KLADDE_APP AND NOT KLADDE_DEMO)
    add_test(NAME no_demo_in_app COMMAND ${CMAKE_COMMAND} -DAPP=$<TARGET_FILE:kladde> -P ${CMAKE_CURRENT_SOURCE_DIR}/no_demo.cmake)
endif()

# Resources the app loads at run time: QML files in the qrc, icons bundled for Android.
find_package(Python3 COMPONENTS Interpreter)
if(KLADDE_APP AND Python3_FOUND)
    add_test(NAME app_resources COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/check_resources.py)
endif()
