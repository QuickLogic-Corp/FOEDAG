# Install script for directory: C:/testFOEDAGWin/FOEDAG/src/Main

# Set the install prefix
if(NOT DEFINED CMAKE_INSTALL_PREFIX)
  set(CMAKE_INSTALL_PREFIX "C:/Program Files (x86)/FOEDAG")
endif()
string(REGEX REPLACE "/$" "" CMAKE_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")

# Set the install configuration name.
if(NOT DEFINED CMAKE_INSTALL_CONFIG_NAME)
  if(BUILD_TYPE)
    string(REGEX REPLACE "^[^A-Za-z0-9_]+" ""
           CMAKE_INSTALL_CONFIG_NAME "${BUILD_TYPE}")
  else()
    set(CMAKE_INSTALL_CONFIG_NAME "Release")
  endif()
  message(STATUS "Install configuration: \"${CMAKE_INSTALL_CONFIG_NAME}\"")
endif()

# Set the component getting installed.
if(NOT CMAKE_INSTALL_COMPONENT)
  if(COMPONENT)
    message(STATUS "Install component: \"${COMPONENT}\"")
    set(CMAKE_INSTALL_COMPONENT "${COMPONENT}")
  else()
    set(CMAKE_INSTALL_COMPONENT)
  endif()
endif()

# Is this installation the result of a crosscompile?
if(NOT DEFINED CMAKE_CROSSCOMPILING)
  set(CMAKE_CROSSCOMPILING "FALSE")
endif()

# Set default install directory permissions.
if(NOT DEFINED CMAKE_OBJDUMP)
  set(CMAKE_OBJDUMP "C:/msys64/mingw64/bin/objdump.exe")
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/foedag" TYPE FILE FILES "C:/testFOEDAGWin/FOEDAG/src/Main/../../lib/libfoedagcore.a")
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/foedag/MainWindow" TYPE FILE FILES
    "C:/testFOEDAGWin/FOEDAG/src/Main/../MainWindow/main_window.h"
    "C:/testFOEDAGWin/FOEDAG/src/Main/../MainWindow/Session.h"
    "C:/testFOEDAGWin/FOEDAG/src/Main/../MainWindow/mainwindowmodel.h"
    "C:/testFOEDAGWin/FOEDAG/src/Main/../MainWindow/TopLevelInterface.h"
    "C:/testFOEDAGWin/FOEDAG/src/Main/../NewProject/newprojectmodel.h"
    "C:/testFOEDAGWin/FOEDAG/src/Main/../NewFile/newfilemodel.h"
    )
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/foedag/Tcl" TYPE FILE FILES "C:/testFOEDAGWin/FOEDAG/src/Main/../Tcl/TclInterpreter.h")
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/foedag/Main" TYPE FILE FILES
    "C:/testFOEDAGWin/FOEDAG/src/Main/../Main/CommandLine.h"
    "C:/testFOEDAGWin/FOEDAG/src/Main/../Main/Foedag.h"
    "C:/testFOEDAGWin/FOEDAG/src/Main/../Main/ToolContext.h"
    "C:/testFOEDAGWin/FOEDAG/src/Main/../Main/Settings.h"
    "C:/testFOEDAGWin/FOEDAG/src/Main/../Main/Tasks.h"
    "C:/testFOEDAGWin/FOEDAG/src/Main/../Main/WidgetFactory.h"
    )
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/foedag/Command" TYPE FILE FILES
    "C:/testFOEDAGWin/FOEDAG/src/Main/../Command/Command.h"
    "C:/testFOEDAGWin/FOEDAG/src/Main/../Command/Logger.h"
    "C:/testFOEDAGWin/FOEDAG/src/Main/../Command/CommandStack.h"
    )
endif()

