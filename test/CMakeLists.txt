cmake_minimum_required(VERSION 3.27)

file(GLOB SOURCE_FILES ../src/*.cpp ../src/*.h)

include_directories(../src)

add_executable(PS2ImageMakerTest ${SOURCE_FILES} Test.cpp)

# The image's end, run by ctest in a folder of the build
find_package(Threads REQUIRED)
add_executable(PS2ImageMakerRegressionTest RegressionTest.cpp)
target_link_libraries(PS2ImageMakerRegressionTest PRIVATE PS2ImageMakerStatic Threads::Threads)
add_test(NAME regression COMMAND PS2ImageMakerRegressionTest ${CMAKE_CURRENT_BINARY_DIR}/regression)
