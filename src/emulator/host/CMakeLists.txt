add_library(
host
STATIC
include/host/app.h
include/host/config.h
include/host/screen_render.h
include/host/functions.h
include/host/imports.h
include/host/sfo.h
include/host/state.h
include/host/version.h
include/host/app_util.h
src/app.cpp
src/screen_render.cpp
src/config.cpp
src/host.cpp
src/sfo.cpp
version.cpp
)

configure_file(src/version.cpp.in version.cpp)

target_include_directories(host PUBLIC include)
target_link_libraries(host PUBLIC audio cpu ctrl gxm io kernel mem net nids np renderer rtc util gui)
target_link_libraries(host PRIVATE ${Boost_LIBRARIES} glbinding-aux glutil microprofile sdl2 yaml-cpp)
