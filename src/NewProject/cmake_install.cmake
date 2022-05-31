# Install script for directory: C:/testFOEDAGWin/FOEDAG/src/NewProject

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
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/foedag" TYPE FILE FILES "C:/testFOEDAGWin/FOEDAG/src/NewProject/../../lib/libnewproject.a")
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/foedag/NewProject" TYPE FILE FILES
    "C:/testFOEDAGWin/FOEDAG/src/NewProject/../NewProject/new_project_dialog.h"
    "C:/testFOEDAGWin/FOEDAG/src/NewProject/../NewProject/newprojectmodel.h"
    "C:/testFOEDAGWin/FOEDAG/src/NewProject/../NewProject/add_constraints_form.h"
    "C:/testFOEDAGWin/FOEDAG/src/NewProject/../NewProject/source_grid.h"
    "C:/testFOEDAGWin/FOEDAG/src/NewProject/../NewProject/add_source_form.h"
    "C:/testFOEDAGWin/FOEDAG/src/NewProject/../NewProject/device_planner_form.h"
    "C:/testFOEDAGWin/FOEDAG/src/NewProject/../NewProject/location_form.h"
    "C:/testFOEDAGWin/FOEDAG/src/NewProject/../NewProject/project_type_form.h"
    "C:/testFOEDAGWin/FOEDAG/src/NewProject/../NewProject/summary_form.h"
    )
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/foedag/NewProject/Main" TYPE FILE FILES "C:/testFOEDAGWin/FOEDAG/src/NewProject/../NewProject/Main/registerNewProjectCommands.h")
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/foedag/NewProject/ProjectManager" TYPE FILE FILES
    "C:/testFOEDAGWin/FOEDAG/src/NewProject/../NewProject/ProjectManager/project_manager.h"
    "C:/testFOEDAGWin/FOEDAG/src/NewProject/../NewProject/ProjectManager/project.h"
    "C:/testFOEDAGWin/FOEDAG/src/NewProject/../NewProject/ProjectManager/project_configuration.h"
    "C:/testFOEDAGWin/FOEDAG/src/NewProject/../NewProject/ProjectManager/project_option.h"
    "C:/testFOEDAGWin/FOEDAG/src/NewProject/../NewProject/ProjectManager/project_fileset.h"
    "C:/testFOEDAGWin/FOEDAG/src/NewProject/../NewProject/ProjectManager/project_run.h"
    )
endif()

