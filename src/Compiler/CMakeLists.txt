# -*- mode:cmake -*-

# Copyright 2021 The Foedag team

# GPL License

# Copyright (c) 2021 The Open-Source FPGA Foundation

# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.

# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.

# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.

cmake_minimum_required(VERSION 3.15)

project(compiler LANGUAGES CXX)

if (PRODUCTION_BUILD)
  message("Production Build type set to ON")
  set (PRODUCTION_BUILD_FLAG "-DPRODUCTION_BUILD=1")
endif(PRODUCTION_BUILD)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (MSVC)
else()
  set(CMAKE_CXX_FLAGS_DEBUG
  "${CMAKE_CXX_FLAGS_DEBUG} ${TCMALLOC_COMPILE_OPTIONS} -Werror -Wall -O0 -g ${MSYS_COMPILE_OPTIONS} ${MY_CXX_WARNING_FLAGS} ${MEM_SANITIZER_FLAGS} ${PRODUCTION_BUILD_FLAG} -Wno-deprecated-declarations")
  set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} ${PRODUCTION_BUILD_FLAG} -Werror -Wall -Wno-deprecated-declarations")
endif()

include (../../cmake/cmake_qt.txt)
include (../../cmake/cmake_tcl.txt)

include_directories(${PROJECT_SOURCE_DIR}/../../src ${PROJECT_SOURCE_DIR}/.. ${CMAKE_CURRENT_BINARY_DIR}/../../include/)

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/../../lib)

set (SRC_CPP_LIST
  Compiler.cpp
  #foedag_version_number.cpp
  foedag_version_number_ql.cpp
  Constraints.cpp
  CompilerOpenFPGA.cpp
  CompilerOpenFPGA_ql.cpp
  WorkerThread.cpp
  TaskTableView.cpp
  #TaskModel.cpp
  TaskModel_ql.cpp
  Task.cpp
  TaskCompilationStateManager.cpp
  CommandWrapper.cpp
  TilesCfgParser.cpp
  #TaskManager.cpp
  TaskManager_ql.cpp
  CompilerDefines.cpp
  Log.cpp
  Reports/AbstractReportManager.cpp
  Reports/DefaultTaskReport.cpp
  # Reports/RoutingReportManager.cpp
  # Reports/PackingReportManager.cpp
  # Reports/PlacementReportManager.cpp
  Reports/RoutingReportManager_ql.cpp
  Reports/PackingReportManager_ql.cpp
  Reports/PlacementReportManager_ql.cpp
  Reports/TableReport.cpp
  Reports/TaskReportManagerRegistry.cpp
  # Reports/SynthesisReportManager.cpp
  # Reports/TimingAnalysisReportManager.cpp
  Reports/SynthesisReportManager_ql.cpp
  Reports/TimingAnalysisReportManager_ql.cpp
  Reports/PowerAnalysisReportManager_ql.cpp
  QLDeviceManager.cpp
  QLSettingsManager.cpp
  QLMetricsManager.cpp
  BlifParser.cpp
  WildcardFileFinder.cpp
  DialogUtils.cpp
)

set (SRC_H_INSTALL_LIST
  Compiler.h
  Constraints.cpp
  CompilerOpenFPGA.h
  CompilerOpenFPGA_ql.h
  WorkerThread.h
  TaskTableView.h
  TaskModel.h
  Task.h
  TaskCompilationStateManager.h
  CommandWrapper.h
  TilesCfgParser.h
  TaskManager.h
  CompilerDefines.h
  Log.h
  Reports/AbstractReportManager.h
  Reports/DefaultTaskReport.h
  Reports/IDataReport.h
  Reports/ITaskReport.h
  Reports/ITaskReportManager.h
  Reports/RoutingReportManager.h
  Reports/PackingReportManager.h
  Reports/PlacementReportManager.h
  Reports/TableReport.h
  Reports/TaskReportManagerRegistry.h
  Reports/SynthesisReportManager.h
  Reports/TimingAnalysisReportManager.h
  Reports/PowerAnalysisReportManager_ql.h
  Reports/DataProfiles.h
  TaskGlobal.h
  QLDeviceManager.h
  QLSettingsManager.h
  QLMetricsManager.h
  BlifParser.h
  WildcardFileFinder.h
  DialogUtils.h
)

set (SRC_H_LIST
  ${SRC_H_INSTALL_LIST}
  TclInterpreterHandler.h
)

set (res_LIST
  compiler_resources.qrc
)

# Conditionally embed power calculator xlsx when built from Aurora2.
# Aurora2 sets POWER_CALCULATOR_XLSX to the path of the built xlsx file.
if(POWER_CALCULATOR_XLSX AND EXISTS "${POWER_CALCULATOR_XLSX}")
  file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/compiler_power_calc_resources.qrc"
    "<RCC>\n  <qresource prefix=\"/\">\n    <file alias=\"build/power_calculator/power_calculator.xlsx\">${POWER_CALCULATOR_XLSX}</file>\n  </qresource>\n</RCC>\n"
  )
  list(APPEND res_LIST "${CMAKE_CURRENT_BINARY_DIR}/compiler_power_calc_resources.qrc")
  set(HAS_POWER_CALC_RESOURCE TRUE)
endif()

add_library(compiler STATIC
  ${SRC_CPP_LIST}
  ${SRC_H_LIST}
  ${res_LIST}
)

# [aurora2#1725 REQ-004] The compiler runs the headless floorplan check before
# generate_floorplanning.py, so it genuinely depends on floorplanning now. Declared
# rather than left to link order: floorplanning's other symbols happened to resolve
# because libfoedag pulls them in first, which would not have covered this one.
target_link_libraries(compiler PUBLIC Qt6::Widgets Qt6::Core Qt6::Gui Qt6::Xml qlcrypt::qlcrypt floorplanning)
target_compile_definitions(compiler PRIVATE COMPILER_LIBRARY)
if(HAS_POWER_CALC_RESOURCE)
  target_compile_definitions(compiler PUBLIC HAS_POWER_CALC_RESOURCE)
endif()

install (
  FILES ${CMAKE_CURRENT_BINARY_DIR}/../../lib/$<TARGET_FILE_NAME:compiler>
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/foedag)

foreach(file ${SRC_H_INSTALL_LIST})
  install(
      FILES ${PROJECT_SOURCE_DIR}/../Compiler/${file}
      DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/foedag/Compiler)
endforeach()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/../../bin)

add_executable(compiler_bin
	${PROJECT_SOURCE_DIR}/../Compiler/Test/compiler_main.cpp)
# Removed designquery 
target_link_libraries(compiler_bin foedag tcl_stubb tcl_static zlib compiler ipgenerate simulation)
set_target_properties(compiler_bin PROPERTIES OUTPUT_NAME compiler_test)

