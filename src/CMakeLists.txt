cmake_minimum_required(VERSION 3.27)

file(GLOB SOURCE_FILES *.cpp *.h)

add_library(objlib OBJECT ${SOURCE_FILES})

set_property(TARGET objlib PROPERTY CMAKE_POSITION_INDEPENDENT_CODE 1)

add_library(PS2ImageMaker SHARED $<TARGET_OBJECTS:objlib>)
add_library(PS2ImageMakerStatic STATIC $<TARGET_OBJECTS:objlib>)

target_sources(PS2ImageMaker PUBLIC FILE_SET HEADERS BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR} FILES ps2imagemaker.h)
target_sources(PS2ImageMakerStatic PUBLIC FILE_SET HEADERS BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR} FILES ps2imagemaker.h)

install(TARGETS PS2ImageMaker DESTINATION ${PROJECT_SOURCE_DIR}/lib/)
install(TARGETS PS2ImageMakerStatic DESTINATION ${PROJECT_SOURCE_DIR}/lib/)