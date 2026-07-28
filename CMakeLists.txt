cmake_minimum_required(VERSION 3.21)
project(radiacode-monitor VERSION 0.2.0 LANGUAGES CXX)

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
set(_radiacode_monitor_sources
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
  resources/app.qrc
)

# macOS Dock / Finder icon
set(_radiacode_app_icon_mac "${CMAKE_CURRENT_SOURCE_DIR}/macos/radiacode-monitor.icns")
if(APPLE AND EXISTS "${_radiacode_app_icon_mac}")
  list(APPEND _radiacode_monitor_sources "${_radiacode_app_icon_mac}")
  set_source_files_properties("${_radiacode_app_icon_mac}" PROPERTIES
    MACOSX_PACKAGE_LOCATION "Resources"
  )
endif()

# Windows: embed multi-size .ico into the .exe
if(WIN32)
  set(RADIACODE_WIN_ICON "${CMAKE_CURRENT_SOURCE_DIR}/icons/radiacode-monitor.ico")
  # RC compiler accepts forward slashes
  file(TO_CMAKE_PATH "${RADIACODE_WIN_ICON}" RADIACODE_WIN_ICON)
  configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/windows/radiacode-monitor.rc.in"
    "${CMAKE_CURRENT_BINARY_DIR}/radiacode-monitor.rc"
    @ONLY
  )
  list(APPEND _radiacode_monitor_sources "${CMAKE_CURRENT_BINARY_DIR}/radiacode-monitor.rc")
endif()

add_executable(radiacode-monitor WIN32 ${_radiacode_monitor_sources})
unset(_radiacode_monitor_sources)

target_include_directories(radiacode-monitor PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

# Single source of truth for app version (About dialog, packaging scripts).
target_compile_definitions(radiacode-monitor PRIVATE
  "RADIACODE_MONITOR_VERSION=\"${PROJECT_VERSION}\""
)

set_target_properties(radiacode-monitor PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
  WIN32_EXECUTABLE TRUE
  MACOSX_BUNDLE TRUE
  MACOSX_BUNDLE_GUI_IDENTIFIER "io.qtradiacode.radiacode-monitor"
  MACOSX_BUNDLE_BUNDLE_NAME "Radiacode Monitor"
  MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
  MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
  MACOSX_BUNDLE_COPYRIGHT "Copyright © QtRadiacode contributors"
  MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/macos/Info.plist.in"
  MACOSX_BUNDLE_ICON_FILE "radiacode-monitor"
)
unset(_radiacode_app_icon_mac)

# Linux: install desktop entry + hicolor icons (optional packaging / `cmake --install`)
if(UNIX AND NOT APPLE)
  include(GNUInstallDirs)
  set(_desktop_in "${CMAKE_CURRENT_SOURCE_DIR}/linux/radiacode-monitor.desktop.in")
  set(_desktop_out "${CMAKE_CURRENT_BINARY_DIR}/radiacode-monitor.desktop")
  configure_file("${_desktop_in}" "${_desktop_out}" @ONLY)
  install(FILES "${_desktop_out}"
    DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/applications")
  foreach(_sz IN ITEMS 16 24 32 48 64 128 256 512)
    set(_icon "${CMAKE_CURRENT_SOURCE_DIR}/icons/radiacode-monitor-${_sz}.png")
    if(EXISTS "${_icon}")
      install(FILES "${_icon}"
        DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/${_sz}x${_sz}/apps"
        RENAME radiacode-monitor.png)
    endif()
  endforeach()
  unset(_desktop_in)
  unset(_desktop_out)
  unset(_sz)
  unset(_icon)
endif()

target_link_libraries(radiacode-monitor PRIVATE
  QtRadiacode::QtRadiacode
  Qt6::Core
  Qt6::Widgets
)

# macOS/iOS: static Qt permission plugin required for QBluetoothPermission.
# Without it: "Could not find permission plugin for QBluetoothPermission".
if(APPLE)
  target_link_libraries(radiacode-monitor PRIVATE Qt6::QDarwinBluetoothPermissionPlugin)
endif()

# Deploy QtRadiacode.dll + libusb-1.0.dll next to the .exe (Windows).
qtradiacode_deploy_runtime(radiacode-monitor)

install(TARGETS radiacode-monitor
  BUNDLE DESTINATION .
  RUNTIME DESTINATION bin
)
