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

set(CMAKE_AUTORCC ON)

include_directories(${PROJECT_SOURCE_DIR}/../../src ${CMAKE_CURRENT_BINARY_DIR}/../../include/)

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/../../lib)

# [aurora2#1725] Two targets, not one. The rules half carries no QWidget and no
# Q_OBJECT, so anything that only needs to read a .qdc or run the headless check --
# the compiler does both -- links it without dragging in the panel. The widgets
# depend on the rules; nothing depends the other way.
set (RULES_SRC_LIST
  DeviceGridDescriptor.cpp
  AtomSets.cpp
  FloorplanChecker.cpp
  QdcSerializer.cpp
  RtlInstanceModel.cpp
  DeviceGrid.cpp
  Tile.cpp
  Partition.cpp
  Region.cpp
)

set (RULES_HDR_LIST
  DeviceGridDescriptor.h
  AtomSets.h
  FloorplanChecker.h
  FloorplanningPaths.h
  TileDescriptor.h
  QdcSerializer.h
  RtlInstanceModel.h
  DeviceGrid.h
  Tile.h
  Partition.h
  Region.h
  HierarhyElement.h
  NaturalSort.h
)

set (SRC_LIST
  FloorPlanningWidget.cpp
  DeviceGridWidget.cpp
  SynthResourceHierarchyWidget.cpp
  PartitionsListWidget.cpp
  NewUniqueNameDialog.cpp
  IssuesListWidget.cpp
  SelectedResourcesWidget.cpp
  CheckableButton.cpp
  PointAnimation.cpp
)

set (HDR_LIST
  FloorPlanningWidget.h
  DeviceGridWidget.h
  SynthResourceHierarchyWidget.h
  PartitionsListWidget.h
  NewUniqueNameDialog.h
  IssuesListWidget.h
  SelectedResourcesWidget.h
  CheckableButton.h
  PointAnimation.h
)

set(QRC_LIST
    data/floorplanning_resource.qrc
)

add_library(floorplan_rules STATIC
  ${RULES_SRC_LIST}
  ${RULES_HDR_LIST}
)

# No Qt6::Widgets: QColor pulls in Gui and the .qdc parser needs Xml, but nothing
# here draws. Keep it that way -- a QWidget include here silently re-links the
# panel into every consumer.
target_link_libraries(floorplan_rules PUBLIC Qt6::Core Qt6::Gui Qt6::Xml)

add_library(floorplanning STATIC
  ${SRC_LIST}
  ${HDR_LIST}
  ${QRC_LIST}
)

target_link_libraries(floorplanning PUBLIC floorplan_rules Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Xml)

target_compile_definitions(floorplanning PRIVATE FP_UI_MARGIN=0)

install (
  FILES ${CMAKE_CURRENT_BINARY_DIR}/../../lib/$<TARGET_FILE_NAME:floorplan_rules>
        ${CMAKE_CURRENT_BINARY_DIR}/../../lib/$<TARGET_FILE_NAME:floorplanning>
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/foedag)

set (HEADER_INSTALL_LIST "")
foreach(HEADER ${SRC_H_LIST})
  list(APPEND HEADER_INSTALL_LIST ${PROJECT_SOURCE_DIR}/../FloorPlanning/${HEADER})
endforeach()

install(
 FILES ${HEADER_INSTALL_LIST}
 DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/foedag/FloorPlanning)


if (STANDALONE_FP_APP)
    add_executable(floorplanning_bin
        Test/FloorPlanning_main.cpp
        ../Utils/StringUtils.cpp
        ../Utils/FileUtils.cpp
        ../Widgets/RoundProgressWidget.cpp
        ../Utils/StringUtils.h
        ../Utils/FileUtils.h
        ../Widgets/RoundProgressWidget.h
    )
    target_link_libraries(floorplanning_bin PUBLIC floorplanning Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Xml)

    target_compile_definitions(floorplanning_bin PRIVATE FP_UI_MARGIN=0)
endif()
