cmake_minimum_required(VERSION 3.16)

project(HyprInk VERSION 1.0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)
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
target_link_libraries(hyprink PRIVATE ${HYPRINK_DEPS_LIBRARIES} Threads::Threads)
target_compile_definitions(hyprink PRIVATE HYPRINK_VERSION="${PROJECT_VERSION}")

install(TARGETS hyprink RUNTIME DESTINATION bin)
install(FILES Project.conf DESTINATION share/hyprink)
install(FILES README.md LICENSE DESTINATION share/doc/hyprink)
install(FILES examples/hyprland.lua examples/hyprland.conf DESTINATION share/doc/hyprink/examples)


include(CTest)
if(BUILD_TESTING)
  add_executable(hyprink-core-tests tests/core_test.cpp)
  target_include_directories(hyprink-core-tests PRIVATE src ${HYPRINK_DEPS_INCLUDE_DIRS})
  target_compile_options(hyprink-core-tests PRIVATE ${HYPRINK_DEPS_CFLAGS_OTHER} -Wall -Wextra -Wpedantic)
  target_link_libraries(hyprink-core-tests PRIVATE ${HYPRINK_DEPS_LIBRARIES})
  add_test(NAME core COMMAND hyprink-core-tests)
  add_executable(hyprink-window-tests tests/window_test.cpp)
  target_include_directories(hyprink-window-tests PRIVATE src ${HYPRINK_DEPS_INCLUDE_DIRS})
  target_compile_options(hyprink-window-tests PRIVATE ${HYPRINK_DEPS_CFLAGS_OTHER} -Wall -Wextra -Wpedantic)
  target_link_libraries(hyprink-window-tests PRIVATE ${HYPRINK_DEPS_LIBRARIES} Threads::Threads)
  target_compile_definitions(hyprink-window-tests PRIVATE HYPRINK_VERSION="${PROJECT_VERSION}")
  add_test(NAME window COMMAND hyprink-window-tests)
  set_tests_properties(window PROPERTIES SKIP_RETURN_CODE 77)
endif()
