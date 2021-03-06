## ---------------------------------------------------------------------
##
## Copyright (C) 2013 - 2015 by the deal.II authors
##
## This file is part of the deal.II library.
##
## The deal.II library is free software; you can use it, redistribute
## it, and/or modify it under the terms of the GNU Lesser General
## Public License as published by the Free Software Foundation; either
## version 2.1 of the License, or (at your option) any later version.
## The full text of the license can be found in the file LICENSE at
## the top level of the deal.II distribution.
##
## ---------------------------------------------------------------------

#
# Set up the testsuite.
#
# We define toplevel targets:
#    setup_tests    - set up testsuite subprojects
#    regen_tests    - rerun configure stage in every testsuite subproject
#    clean_tests    - run the 'clean' target in every testsuite subproject
#    prune_tests    - remove all testsuite subprojects
#

CMAKE_MINIMUM_REQUIRED(VERSION 2.8.8)

MACRO(SET_IF_EMPTY _variable)
  IF("${${_variable}}" STREQUAL "")
    SET(${_variable} ${ARGN})
  ENDIF()
ENDMACRO()

SET_IF_EMPTY(MAKEOPTS $ENV{MAKEOPTS})

IF(DEFINED DEAL_II_HAVE_TESTS_DIRECTORY)

  IF(POLICY CMP0037)
    # allow to override "test" target for quick tests
    CMAKE_POLICY(SET CMP0037 OLD)
  ENDIF()

  #
  # If this CMakeLists.txt file is called from within the deal.II build
  # system, set up quick tests as well:
  #
  ADD_SUBDIRECTORY(quick_tests)

  MESSAGE(STATUS "Setting up testsuite")

  #
  # Write minimalistic CTestTestfile.cmake files to CMAKE_BINARY_DIR and
  # CMAKE_BINARY_DIR/tests:
  #
  FILE(WRITE ${CMAKE_BINARY_DIR}/CTestTestfile.cmake "SUBDIRS(tests)")

  SET(_options "-DDEAL_II_DIR=${CMAKE_BINARY_DIR}")

ELSE()

  MESSAGE(STATUS "This is CMake ${CMAKE_VERSION}")
  MESSAGE(STATUS "")

  IF("${CMAKE_SOURCE_DIR}" STREQUAL "${CMAKE_BINARY_DIR}")
    MESSAGE(FATAL_ERROR "The testsuite cannot be configured in-source. "
      "Please create a separate build directory"
      )
  ENDIF()

  FIND_PACKAGE(deal.II 8.3 REQUIRED HINTS ${DEAL_II_DIR} $ENV{DEAL_II_DIR})
  PROJECT(testsuite NONE)
  SET(_options "-DDEAL_II_DIR=${DEAL_II_PATH}")
ENDIF()

FILE(WRITE ${CMAKE_CURRENT_BINARY_DIR}/CTestTestfile.cmake "")

#
# Always undefine the following variables in the setup_tests target:
#
FOREACH(_var DIFF_DIR NUMDIFF_DIR TEST_PICKUP_REGEX TEST_TIME_LIMIT)
  LIST(APPEND _options "-U${_var}")
  IF(NOT "${${_var}}" STREQUAL "")
    LIST(APPEND _options "-D${_var}=${${_var}}")
  ENDIF()
ENDFOREACH()

SET(_categories
  a-framework algorithms all-headers aniso arpack base bits build_tests
  codim_one deal.II distributed_grids fe gla grid hp integrators lac lapack
  manifold matrix_free metis mpi multigrid opencascade petsc serialization
  slepc trilinos umfpack
  )
IF(DEFINED DEAL_II_HAVE_TESTS_DIRECTORY)
  # Only set up mesh_converter tests if the testsuite is set up as a
  # dependent project:
  LIST(APPEND _categories mesh_converter)
ENDIF()

#
# Custom targets for the testsuite:
#

# Setup tests:
ADD_CUSTOM_TARGET(setup_tests)

# Remove all tests:
ADD_CUSTOM_TARGET(prune_tests)

# Regenerate tests (run "make rebuild_cache" in subprojects):
ADD_CUSTOM_TARGET(regen_tests)

# Regenerate tests (run "make clean" in subprojects):
ADD_CUSTOM_TARGET(clean_tests)

FOREACH(_category ${_categories})
  SET(_category_dir ${CMAKE_CURRENT_SOURCE_DIR}/${_category})

  ADD_CUSTOM_TARGET(setup_tests_${_category}
    COMMAND ${CMAKE_COMMAND} -E make_directory
      ${CMAKE_CURRENT_BINARY_DIR}/${_category}
      COMMAND cd ${CMAKE_CURRENT_BINARY_DIR}/${_category} &&
      ${CMAKE_COMMAND} -G${CMAKE_GENERATOR} ${_options} ${_category_dir}
      > /dev/null
    COMMENT "Processing tests/${_category}"
    )
  ADD_DEPENDENCIES(setup_tests setup_tests_${_category})

  ADD_CUSTOM_TARGET(prune_tests_${_category}
    COMMAND ${CMAKE_COMMAND} -E remove_directory
      ${CMAKE_CURRENT_BINARY_DIR}/${_category}
    COMMENT "Processing tests/${_category}"
    )
  ADD_DEPENDENCIES(prune_tests prune_tests_${_category})

  ADD_CUSTOM_TARGET(regen_tests_${_category}
    COMMAND
      test ! -d ${CMAKE_CURRENT_BINARY_DIR}/${_category} || ${CMAKE_COMMAND}
      --build ${CMAKE_CURRENT_BINARY_DIR}/${_category} --target regenerate
      > /dev/null
    COMMENT "Processing tests/${_category}"
    )
  ADD_DEPENDENCIES(regen_tests regen_tests_${_category})

  ADD_CUSTOM_TARGET(clean_tests_${_category}
    COMMAND
      test ! -d ${CMAKE_CURRENT_BINARY_DIR}/${_category} || ${CMAKE_COMMAND}
      --build ${CMAKE_CURRENT_BINARY_DIR}/${_category} --target clean
    COMMENT "Processing tests/${_category}"
    )
  ADD_DEPENDENCIES(clean_tests clean_tests_${_category})

  FILE(APPEND ${CMAKE_CURRENT_BINARY_DIR}/CTestTestfile.cmake
    "SUBDIRS(${_category})\n"
    )
ENDFOREACH()


IF(DEFINED DEAL_II_HAVE_TESTS_DIRECTORY)
  MESSAGE(STATUS "Setting up testsuite - Done")
ENDIF()
