cmake_minimum_required(VERSION 3.21)
project(radiacode-monitor VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets)

# Prefer sibling checkout of the library (local development).
# Later: FetchContent / git submodule from private GitHub.
set(QTRADIACODE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../qtradiacode"
    CACHE PATH "Path to qtradiacode library source")
if(NOT EXISTS "${QTRADIACODE_DIR}/CMakeLists.txt")
  message(FATAL_ERROR
    "QtRadiacode not found at QTRADIACODE_DIR=${QTRADIACODE_DIR}. "
    "Clone qtradiacode next to this project or pass -DQTRADIACODE_DIR=...")
endif()

set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory("${QTRADIACODE_DIR}" "${CMAKE_BINARY_DIR}/qtradiacode")

add_executable(radiacode-monitor
  src/main.cpp
  src/mainwindow.h
  src/mainwindow.cpp
  src/spectrumwidget.h
  src/spectrumwidget.cpp
  src/spectrumexport.h
  src/spectrumexport.cpp
)

target_link_libraries(radiacode-monitor PRIVATE
  QtRadiacode::QtRadiacode
  Qt6::Core
  Qt6::Widgets
)

install(TARGETS radiacode-monitor RUNTIME DESTINATION bin)
