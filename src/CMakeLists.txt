cmake_minimum_required(VERSION 4.0)

FILE(GLOB SOURCE_FILES *.cpp *.h)

add_library(PS2ImageMaker SHARED ${SOURCE_FILES})

install(TARGETS PS2ImageMaker DESTINATION ${PROJECT_SOURCE_DIR}/lib/)
