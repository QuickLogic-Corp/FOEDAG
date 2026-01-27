cmake_minimum_required(VERSION 3.15)

project(floorplanning LANGUAGES CXX)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)


if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    set(STANDALONE_FP_APP TRUE)
else()
    set(STANDALONE_FP_APP FALSE)
endif()

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
  DeviceGridWidget.cpp
  SynthResourceHierarchyWidget.cpp
  PartitionsListWidget.cpp
  NewUniqueNameDialog.cpp
  ErrorsListWidget.cpp
  # widgets
  DeviceGridDescriptor.cpp
  SynthResourceExtractor.cpp
  QdcSerializer.cpp
  DeviceGrid.cpp
  Tile.cpp
  Partition.cpp
  Region.cpp
  PointAnimation.cpp
)

set (HDR_LIST
  # widgets
  FloorPlanningWidget.h
  DeviceGridWidget.h
  SynthResourceHierarchyWidget.h
  PartitionsListWidget.h
  NewUniqueNameDialog.h
  ErrorsListWidget.h
  # widgets
  DeviceGridDescriptor.h
  TileDescriptor.h
  SynthResourceExtractor.h
  QdcSerializer.h
  DeviceGrid.h
  Tile.h
  Partition.h
  Region.h
  HierarhyElement.h
  PointAnimation.h
)

add_library(floorplanning STATIC
  ${SRC_LIST}
  ${HDR_LIST}
)

target_link_libraries(floorplanning PUBLIC Qt5::Core Qt5::Gui Qt5::Widgets Qt5::Xml)

target_compile_definitions(floorplanning PRIVATE FP_UI_MARGIN=0)

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


if (STANDALONE_FP_APP)
    set(CMAKE_AUTORCC ON)
    set(QRC
        ../MainWindow/main_window_resource.qrc
        )

    add_executable(floorplanning_bin
        Test/FloorPlanning_main.cpp
        ../Utils/StringUtils.cpp
        ../Utils/FileUtils.cpp
        ../Utils/StringUtils.h
        ../Utils/FileUtils.h
        ${QRC}
    )
    target_link_libraries(floorplanning_bin PUBLIC floorplanning Qt5::Core Qt5::Gui Qt5::Widgets Qt5::Xml)

    target_compile_definitions(floorplanning_bin PRIVATE FP_UI_MARGIN=0)
endif()
