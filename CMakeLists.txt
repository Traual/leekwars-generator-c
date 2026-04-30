cmake_minimum_required(VERSION 3.20)
project(leekwars_generator_c
        VERSION 0.1.0
        LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(MSVC)
  add_compile_options(/W4 /O2)
else()
  add_compile_options(-Wall -Wextra -O2 -fPIC)
endif()

# Library --------------------------------------------------------------

file(GLOB LW_SRC CONFIGURE_DEPENDS "src/*.c")

add_library(lwengine STATIC ${LW_SRC})
target_include_directories(lwengine PUBLIC include)

# math (libm) needed for pow / floor on Linux+gcc.
if(NOT MSVC)
  target_link_libraries(lwengine PUBLIC m)
endif()

# Tests ----------------------------------------------------------------

enable_testing()
file(GLOB LW_TESTS CONFIGURE_DEPENDS "tests/test_*.c")
foreach(test_src ${LW_TESTS})
  get_filename_component(test_name ${test_src} NAME_WE)
  add_executable(${test_name} ${test_src})
  target_link_libraries(${test_name} PRIVATE lwengine)
  add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()
