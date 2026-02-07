cmake_minimum_required(VERSION 4.0)

file(GLOB SOURCE_FILES *.cpp *.h)

add_library(objlib OBJECT ${SOURCE_FILES})

set_property(TARGET objlib PROPERTY CMAKE_POSITION_INDEPENDENT_CODE 1)

add_library(PS2ImageMaker SHARED $<TARGET_OBJECTS:objlib>)
add_library(PS2ImageMakerStatic STATIC $<TARGET_OBJECTS:objlib>)

install(TARGETS PS2ImageMaker DESTINATION ${PROJECT_SOURCE_DIR}/lib/)
install(TARGETS PS2ImageMakerStatic DESTINATION ${PROJECT_SOURCE_DIR}/lib/)