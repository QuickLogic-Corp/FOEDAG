cmake_minimum_required(VERSION 3.15)

# GUI test framework: replay runner, gui_* Tcl commands, milestone hub and
# recorder. Depends only on Qt and Tcl, so foedagcore can link it without a
# cycle. Active only when aurora runs with --replay or --record.
project(guitestframework LANGUAGES CXX)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (MSVC)
else()
  set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -Werror")
  set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -Werror")
endif()

include (../../cmake/cmake_qt.txt)
include (../../cmake/cmake_tcl.txt)

include_directories(${PROJECT_SOURCE_DIR}/../../src ${CMAKE_CURRENT_BINARY_DIR}/../../include/)

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/../../lib)

set (SRC_CPP_LIST
  GuiRecorder.cpp
  GuiScreenshot.cpp
  GuiTestCommands.cpp
  GuiTestHub.cpp
  GuiTestMode.cpp
  GuiTestRunner.cpp
)

set (SRC_H_LIST
  GuiMilestone.h
  GuiRecorder.h
  GuiScreenshot.h
  GuiTestCommands.h
  GuiTestHub.h
  GuiTestMode.h
  GuiTestRunner.h
)

add_library(guitestframework STATIC
  ${SRC_CPP_LIST}
  ${SRC_H_LIST}
)

target_link_libraries(guitestframework PUBLIC Qt6::Widgets Qt6::Core Qt6::Gui)

install (
  FILES ${CMAKE_CURRENT_BINARY_DIR}/../../lib/$<TARGET_FILE_NAME:guitestframework>
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/foedag)
