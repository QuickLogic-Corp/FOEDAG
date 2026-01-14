cmake_minimum_required(VERSION 3.15)

project(floorplanning LANGUAGES CXX)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (MSVC)
else()
  set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -Werror")
  set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -Werror")
endif()

include (../../cmake/cmake_qt.txt)

include_directories(${PROJECT_SOURCE_DIR}/../../src ${CMAKE_CURRENT_BINARY_DIR}/../../include/)

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/../../lib)

set (SRC_LIST
  # widgets
  FloorPlanningWidget.cpp
  DeviceWidget.cpp
  SynthResourceHierarchyWidget.cpp
  # widgets
  SynthResourceExtractor.cpp
  QdcSerializer.cpp
  Device.cpp
  Tile.cpp
  Region.cpp
)

set (HDR_LIST
  # widgets
  FloorPlanningWidget.h
  DeviceWidget.h
  SynthResourceHierarchyWidget.h
  # widgets
  DeviceDescriptor.h
  TileDescriptor.h
  SynthResourceExtractor.h
  QdcSerializer.h
  Device.h
  Tile.h
  Region.h
  HierarhyElement.h
)

add_library(floorplanning STATIC
  ${SRC_LIST}
  ${HDR_LIST}
)

#find_package(Qt5 COMPONENTS Network REQUIRED)
#if (NOT Qt5Network_FOUND)
#    message(FATAL_ERROR "Failed to find Qt5Network")
#endif()

target_link_libraries(floorplanning PUBLIC Qt5::Core Qt5::Gui Qt5::Widgets Qt5::Xml)

install (
  FILES ${CMAKE_CURRENT_BINARY_DIR}/../../lib/$<TARGET_FILE_NAME:floorplanning>
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/foedag)

set (HEADER_INSTALL_LIST "")
foreach(HEADER ${SRC_H_LIST})
  list(APPEND HEADER_INSTALL_LIST ${PROJECT_SOURCE_DIR}/../FloorPlanning/${HEADER})
endforeach()

install(
 FILES ${HEADER_INSTALL_LIST}
 DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/foedag/FloorPlanning)
