cmake_minimum_required(VERSION 3.20)

project(xPad)

set(CMAKE_CXX_STANDARD 20)
add_subdirectory(win)
add_subdirectory(scintilla)

set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT xpad-win64)

