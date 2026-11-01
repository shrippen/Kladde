# The demo never ships: an app built without KLADDE_DEMO must not contain the demo world
# or its backend. Run as: cmake -DAPP=<path to kladde> -P no_demo.cmake
file(STRINGS "${APP}" hits REGEX "studio-weber|DemoBackend|demo/world\\.json|DEMO_|SHOT_PLAN|[Dd]emo build")
if(hits)
    message(FATAL_ERROR "demo code or data in ${APP}: ${hits}")
endif()
