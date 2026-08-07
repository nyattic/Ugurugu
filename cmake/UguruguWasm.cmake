find_package(Qt6 6.11 REQUIRED COMPONENTS Core Gui)

qt_standard_project_setup()

add_library(ugurugu_engine STATIC ${UGURUGU_ENGINE_SOURCES})
target_include_directories(ugurugu_engine PUBLIC src)
target_link_libraries(
    ugurugu_engine
    PUBLIC
    Qt6::Core
    Qt6::Gui
)
ugurugu_target_defaults(ugurugu_engine)

# Plain add_executable on purpose: the engine runs headless, so it must not
# pull in the wasm QPA plugin or the qtloader HTML shell that
# qt_add_executable's finalization generates.
add_executable(ugurugu_engine_spike src/wasm/EngineBridge.cpp)
target_link_libraries(ugurugu_engine_spike PRIVATE ugurugu_engine)
ugurugu_target_defaults(ugurugu_engine_spike)
target_link_options(
    ugurugu_engine_spike
    PRIVATE
    --no-entry
    # Qt Core and Gui use emscripten::val internally, which needs embind's
    # JS-side runtime even in a headless engine.
    -lembind
    -sMODULARIZE=1
    -sEXPORT_NAME=createUguruguEngine
    -sENVIRONMENT=web,worker,node
    -sALLOW_MEMORY_GROWTH=1
    -sEXPORTED_FUNCTIONS=_malloc,_free
    -sEXPORTED_RUNTIME_METHODS=ccall,cwrap,HEAPU8,UTF8ToString
)
