cmake_minimum_required(VERSION 3.20)
project(kladde VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# The core (mapping, batches, HealthLog client) needs only Qt Core and Network, so it runs
# headless: in the tests, in kladde-push and later in the Android background job.
find_package(Qt6 REQUIRED COMPONENTS Core Network)

option(KLADDE_APP "Build the Kirigami app" ON)
option(KLADDE_TESTS "Build the tests" ON)

add_subdirectory(core)
add_subdirectory(tools)
if(KLADDE_APP)
    add_subdirectory(app)
endif()
if(KLADDE_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
