# Install script for directory: C:/testFOEDAGWin/FOEDAG/third_party/QScintilla-2.13.1

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
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/foedag" TYPE FILE FILES "C:/testFOEDAGWin/FOEDAG/third_party/QScintilla-2.13.1/../../lib/libqscintilla2_qt.a")
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/foedag/Qsci" TYPE FILE FILES "C:/testFOEDAGWin/FOEDAG/third_party/QScintilla-2.13.1/src/Qsci/qsciglobal.h")
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/foedag/Qsci" TYPE FILE FILES "C:/testFOEDAGWin/FOEDAG/third_party/QScintilla-2.13.1/src/Qsci/qsciscintilla.h")
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/foedag/Qsci" TYPE FILE FILES "C:/testFOEDAGWin/FOEDAG/third_party/QScintilla-2.13.1/src/Qsci/qsciscintillabase.h")
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/foedag/Qsci" TYPE FILE FILES "C:/testFOEDAGWin/FOEDAG/third_party/QScintilla-2.13.1/src/Qsci/qsciabstractapis.h")
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/foedag/Qsci" TYPE FILE FILES "C:/testFOEDAGWin/FOEDAG/third_party/QScintilla-2.13.1/src/Qsci/qsciapis.h")
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/foedag/Qsci" TYPE FILE FILES "C:/testFOEDAGWin/FOEDAG/third_party/QScintilla-2.13.1/src/Qsci/qscicommand.h")
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/foedag/Qsci" TYPE FILE FILES "C:/testFOEDAGWin/FOEDAG/third_party/QScintilla-2.13.1/src/Qsci/qscicommandset.h")
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/foedag/Qsci" TYPE FILE FILES "C:/testFOEDAGWin/FOEDAG/third_party/QScintilla-2.13.1/src/Qsci/qscidocument.h")
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/foedag/Qsci" TYPE FILE FILES "C:/testFOEDAGWin/FOEDAG/third_party/QScintilla-2.13.1/src/Qsci/qscilexer.h")
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/foedag/Qsci" TYPE FILE FILES "C:/testFOEDAGWin/FOEDAG/third_party/QScintilla-2.13.1/src/Qsci/qscilexertcl.h")
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/foedag/Qsci" TYPE FILE FILES "C:/testFOEDAGWin/FOEDAG/third_party/QScintilla-2.13.1/src/Qsci/qscilexerverilog.h")
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/foedag/Qsci" TYPE FILE FILES "C:/testFOEDAGWin/FOEDAG/third_party/QScintilla-2.13.1/src/Qsci/qscilexervhdl.h")
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/foedag/Qsci" TYPE FILE FILES "C:/testFOEDAGWin/FOEDAG/third_party/QScintilla-2.13.1/src/Qsci/qscimacro.h")
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/foedag/Qsci" TYPE FILE FILES "C:/testFOEDAGWin/FOEDAG/third_party/QScintilla-2.13.1/src/Qsci/qscistyle.h")
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/foedag/Qsci" TYPE FILE FILES "C:/testFOEDAGWin/FOEDAG/third_party/QScintilla-2.13.1/src/Qsci/qscistyledtext.h")
endif()

