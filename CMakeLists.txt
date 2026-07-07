cmake_minimum_required(VERSION 3.16)

project(HyprInk VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(PkgConfig REQUIRED)
pkg_check_modules(HYPRINK_DEPS REQUIRED
  gtkmm-3.0
  gtk-layer-shell-0
  jsoncpp
)

add_executable(hyprink
  src/main.cpp
)

target_include_directories(hyprink PRIVATE ${HYPRINK_DEPS_INCLUDE_DIRS})
target_compile_options(hyprink PRIVATE ${HYPRINK_DEPS_CFLAGS_OTHER} -Wall -Wextra -Wpedantic)
target_link_directories(hyprink PRIVATE ${HYPRINK_DEPS_LIBRARY_DIRS})
target_link_libraries(hyprink PRIVATE ${HYPRINK_DEPS_LIBRARIES})

install(TARGETS hyprink RUNTIME DESTINATION bin)
install(FILES Project.conf DESTINATION share/hyprink)
install(FILES README.md LICENSE DESTINATION share/doc/hyprink)
install(FILES examples/hyprland.lua examples/hyprland.conf DESTINATION share/doc/hyprink/examples)
