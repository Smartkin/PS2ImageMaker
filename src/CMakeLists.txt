cmake_minimum_required(VERSION 3.27)

file(GLOB SOURCE_FILES *.cpp *.h)

add_library(objlib OBJECT ${SOURCE_FILES})

set_property(TARGET objlib PROPERTY CMAKE_POSITION_INDEPENDENT_CODE 1)

add_library(PS2ImageMaker SHARED $<TARGET_OBJECTS:objlib>)
add_library(PS2ImageMakerStatic STATIC $<TARGET_OBJECTS:objlib>)

set_target_properties(PS2ImageMaker PROPERTIES PUBLIC_HEADER ps2imagemaker.h VERSION 0.5 SOVERSION 50)
set_target_properties(PS2ImageMakerStatic PROPERTIES PUBLIC_HEADER ps2imagemaker.h VERSION 0.5 SOVERSION 50)

install(TARGETS PS2ImageMaker DESTINATION ${PROJECT_SOURCE_DIR}/lib/)
install(TARGETS PS2ImageMakerStatic DESTINATION ${PROJECT_SOURCE_DIR}/lib/)