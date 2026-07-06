cmake_minimum_required(VERSION 3.27)

file(GLOB SOURCE_FILES ../src/*.cpp ../src/*.h)

include_directories(../src)

add_executable(PS2ImageMakerTest ${SOURCE_FILES} Test.cpp)