cmake_minimum_required(VERSION 3.21)
project(radiacode-monitor VERSION 0.3.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets)

# QtRadiacode library: sibling checkout, explicit -DQTRADIACODE_DIR=..., or FetchContent.
# Default pin for automatic download (override with -DQTRADIACODE_GIT_TAG=...).
set(QTRADIACODE_GIT_TAG "v0.1.3" CACHE STRING
  "Git tag/branch for FetchContent when no local qtradiacode tree is found")

set(QTRADIACODE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../qtradiacode"
    CACHE PATH "Path to qtradiacode library source (optional if FetchContent is used)")

set(_qtradiacode_src "")
if(EXISTS "${QTRADIACODE_DIR}/CMakeLists.txt")
  set(_qtradiacode_src "${QTRADIACODE_DIR}")
  message(STATUS "QtRadiacode: local tree ${QTRADIACODE_DIR}")
else()
  # GitHub source ZIPs extract as qtradiacode-<tag> (with or without leading v).
  file(GLOB _qtr_zip_dirs
    "${CMAKE_CURRENT_SOURCE_DIR}/../qtradiacode-*"
    "${CMAKE_CURRENT_SOURCE_DIR}/../qtradiacode-v*"
  )
  foreach(_cand IN LISTS _qtr_zip_dirs)
    if(EXISTS "${_cand}/CMakeLists.txt")
      set(_qtradiacode_src "${_cand}")
      message(STATUS "QtRadiacode: local tree ${_cand} (release ZIP layout)")
      break()
    endif()
  endforeach()
endif()

if(_qtradiacode_src STREQUAL "")
  message(STATUS
    "QtRadiacode: no local tree — FetchContent "
    "https://github.com/petriska/qtradiacode.git (${QTRADIACODE_GIT_TAG})")
  include(FetchContent)
  FetchContent_Declare(qtradiacode
    GIT_REPOSITORY https://github.com/petriska/qtradiacode.git
    GIT_TAG        ${QTRADIACODE_GIT_TAG}
  )
  set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(qtradiacode)
else()
  set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
  add_subdirectory("${_qtradiacode_src}" "${CMAKE_BINARY_DIR}/qtradiacode")
endif()
unset(_qtradiacode_src)
unset(_qtr_zip_dirs)
unset(_cand)

# WIN32 = Windows GUI subsystem (no console window). Ignored on Linux/macOS.
set(_radiacode_monitor_sources
  src/main.cpp
  src/mainwindow.h
  src/mainwindow.cpp
  src/acquisition/acquisitioncontroller.h
  src/acquisition/acquisitioncontroller.cpp
  src/spectrumwidget.h
  src/spectrumwidget.cpp
  src/spectrumwaterfall.h
  src/spectrumwaterfall.cpp
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

include(GNUInstallDirs)

# Linux: install desktop entry + hicolor icons (optional packaging / `cmake --install`)
if(UNIX AND NOT APPLE)
  set(_desktop_in "${CMAKE_CURRENT_SOURCE_DIR}/linux/radiacode-monitor.desktop.in")
  set(_desktop_out "${CMAKE_BINARY_DIR}/radiacode-monitor.desktop")
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

# Installed binary: no build-tree RUNPATH; libQtRadiacode lives in multiarch libdir
# (/usr/lib/…) and is found via ldconfig after package install.
if(UNIX AND NOT APPLE)
  set_target_properties(radiacode-monitor PROPERTIES
    INSTALL_RPATH ""
    BUILD_WITH_INSTALL_RPATH FALSE
  )
endif()

install(TARGETS radiacode-monitor
  BUNDLE DESTINATION .
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)

# Docs inside the package (/usr/share/doc/radiacode-monitor/…)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
  install(FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE"
    DESTINATION "${CMAKE_INSTALL_DOCDIR}"
    RENAME copyright
  )
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/THIRD_PARTY.md")
  install(FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/THIRD_PARTY.md"
    DESTINATION "${CMAKE_INSTALL_DOCDIR}"
  )
endif()

# ---------------------------------------------------------------------------
# CPack: Debian package (Linux). Configure with CMAKE_INSTALL_PREFIX=/usr for
# a normal system layout, then: cpack -G DEB   or   ./scripts/package-deb.sh
# ---------------------------------------------------------------------------
if(UNIX AND NOT APPLE)
  set(CPACK_GENERATOR "DEB")
  set(CPACK_PACKAGE_NAME "radiacode-monitor")
  set(CPACK_PACKAGE_VENDOR "QtRadiacode contributors")
  set(CPACK_PACKAGE_CONTACT "Martin Petriska <https://github.com/petriska>")
  set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
    "USB/BLE desktop monitor for RadiaCode radiation detectors")
  set(CPACK_PACKAGE_DESCRIPTION
"Radiacode Monitor is an unofficial Qt 6 desktop app for RadiaCode detectors
over USB and Bluetooth LE (spectrum, count/dose rate, ROI time series).

Includes the QtRadiacode shared library and udev rules for USB access
(VID 0483 / PID f123). Not affiliated with the RadiaCode manufacturer.")
  set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/petriska/radiacode-monitor")
  set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
  set(CPACK_PACKAGE_INSTALL_DIRECTORY "${CPACK_PACKAGE_NAME}")
  set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
  set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
  set(CPACK_STRIP_FILES TRUE)

  set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Martin Petriska <https://github.com/petriska>")
  set(CPACK_DEBIAN_PACKAGE_SECTION "science")
  set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
  set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
  set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
  set(CPACK_DEBIAN_PACKAGE_DESCRIPTION "${CPACK_PACKAGE_DESCRIPTION}")

  set(_deb_postinst "${CMAKE_CURRENT_SOURCE_DIR}/linux/debian/postinst")
  set(_deb_postrm "${CMAKE_CURRENT_SOURCE_DIR}/linux/debian/postrm")
  if(EXISTS "${_deb_postinst}" AND EXISTS "${_deb_postrm}")
    set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA "${_deb_postinst};${_deb_postrm}")
  endif()
  unset(_deb_postinst)
  unset(_deb_postrm)

  include(CPack)
endif()
