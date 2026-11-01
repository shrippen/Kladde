add_library(kladde-core STATIC
    sample.h
    hcjson.h hcjson.cpp
    mapper.h mapper.cpp
    batch.h batch.cpp
    healthlogclient.h healthlogclient.cpp
    secretstore.h
    session.h session.cpp
    pkce.h pkce.cpp
    api.h api.cpp
)
target_include_directories(kladde-core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(kladde-core PUBLIC Qt6::Core Qt6::Network)
