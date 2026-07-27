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

# WIN32 = Windows GUI subsystem (no console window). Ignored on Linux/macOS.
add_executable(radiacode-monitor WIN32
  src/main.cpp
  src/mainwindow.h
  src/mainwindow.cpp
  src/spectrumwidget.h
  src/spectrumwidget.cpp
  src/spectrumexport.h
  src/spectrumexport.cpp
  src/roitimeseries/roimath.h
  src/roitimeseries/roimath.cpp
  src/roitimeseries/roirecorder.h
  src/roitimeseries/roirecorder.cpp
  src/roitimeseries/roiexport.h
  src/roitimeseries/roiexport.cpp
  src/roitimeseries/timeserieswidget.h
  src/roitimeseries/timeserieswidget.cpp
  src/roitimeseries/roitimeseriespanel.h
  src/roitimeseries/roitimeseriespanel.cpp
)

target_include_directories(radiacode-monitor PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

set_target_properties(radiacode-monitor PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
  WIN32_EXECUTABLE TRUE
  MACOSX_BUNDLE TRUE
)

target_link_libraries(radiacode-monitor PRIVATE
  QtRadiacode::QtRadiacode
  Qt6::Core
  Qt6::Widgets
)

# Deploy QtRadiacode.dll + libusb-1.0.dll next to the .exe (Windows).
qtradiacode_deploy_runtime(radiacode-monitor)

install(TARGETS radiacode-monitor RUNTIME DESTINATION bin)
