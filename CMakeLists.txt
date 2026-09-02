# -*- mode: cmake; tab-width: 2; indent-tabs-mode: nil; truncate-lines: t; compile-command: "cmake -Wdev" -*-
# vim: set filetype=cmake autoindent tabstop=2 shiftwidth=2 expandtab softtabstop=2 nowrap:

###########################################################################
#                                                                         #
# Note: The bulk of the build system is located in the cmake/ directory.  #
#       This file only contains the specializations for this particular   #
#       project. Most likely you are interested in editing one of these   #
#       files instead:                                                    #
#                                                                         #
#       dune.module                              Name and version number  #
#       CMakeLists_files.cmake                   Path of source files     #
#       cmake/Modules/${project}-prereqs.cmake   Dependencies             #
#                                                                         #
###########################################################################

cmake_minimum_required (VERSION 3.23)

# Mandatory call to project
# opm-grid has no Fortran sources; Fortran is enabled only so that
# FortranCInterface can detect the BLAS/LAPACK name mangling and write
# FCMacros.h, which opm-common's opm/common/utility/numeric/blas_lapack.h
# hard-requires (it #errors when the header is missing) and which
# VelocityInterpolation.cpp and trans_tpfa.c pull in through it.
#
# Ask for the language only when a compiler for it actually exists, rather
# than keying on the host OS: a Windows machine with Intel Fortran should get
# the real header, and a machine without any Fortran compiler should still
# configure. opm-grid_language_hook writes a fallback in that case.
# Note: project() has not run yet, so use check_language(), not CMAKE_Fortran_COMPILER.
include(CheckLanguage)
check_language(Fortran)
if(CMAKE_Fortran_COMPILER)
  project(opm-grid C CXX Fortran)
else()
  project(opm-grid C CXX)
endif()

# project information is in dune.module. Read this file and set variables.
# we cannot generate dune.module since it is read by dunecontrol before
# the build starts, so it makes sense to keep the data there then.

option(SIBLING_SEARCH "Search for other modules in sibling directories?" ON)
option(REQUIRE_ZOLTAN "Require Zoltan to be found (needed for productive run" ON)
option(USE_OPM_COMMON "Use data structures from opm-common?" ON)

macro(opm-grid_prereqs_hook)
  if(USE_OPM_COMMON)
    target_link_libraries(opmgrid PUBLIC opmcommon)
  endif()

  if(NOT ZOLTAN_FOUND AND MPI_C_FOUND AND REQUIRE_ZOLTAN)
    message(SEND_ERROR
      "opm-grid with MPI support requires the package ZOLTAN. "
      "Please install it (e.g. from http://www.cs.sandia.gov/zoltan/.)"
    )
  endif()

  if(MPI_FOUND)
    target_link_libraries(opmgrid PUBLIC MPI::MPI_C MPI::MPI_CXX)
  endif()

  target_link_libraries(opmgrid PUBLIC dunecommon dunegrid)

  if(TARGET Dune::ISTL)
    target_link_libraries(opmgrid PUBLIC Dune::ISTL)
  endif()

  if(TARGET duneuggrid)
    target_link_libraries(opmgrid PUBLIC duneuggrid)
  endif()

  if(TARGET Zoltan::Zoltan)
    target_link_libraries(opmgrid PUBLIC Zoltan::Zoltan)
  endif()

  if(TARGET METIS::METIS)
    target_link_libraries(opmgrid PUBLIC METIS::METIS)
  endif()
endmacro()

# Write a fallback FCMacros.h when FortranCInterface could not produce one.
# It assumes the de-facto standard mangling -- lowercase with a trailing
# underscore -- used by gfortran-built BLAS/LAPACK (OpenBLAS, reference LAPACK)
# and also exported by MKL. Rewrites the file only when the contents differ, so
# a reconfigure does not needlessly invalidate everything that includes it.
function(opm_write_fallback_fcmacros path)
  set(content
"#ifndef FC_HEADER_INCLUDED
#define FC_HEADER_INCLUDED

/* Fallback Fortran name mangling, written without a working FortranCInterface
   detection: lowercase with trailing underscore (gfortran/OpenBLAS/LAPACK). */
#define FC_GLOBAL(name,NAME) name##_
#define FC_GLOBAL_(name,NAME) name##_

#endif
")
  set(existing "")
  if(EXISTS "${path}")
    file(READ "${path}" existing)
  endif()
  if(NOT existing STREQUAL content)
    message(STATUS "No usable FortranCInterface result - writing fallback FCMacros.h "
                   "assuming lowercase-with-underscore BLAS/LAPACK name mangling "
                   "(gfortran/OpenBLAS/LAPACK/MKL convention). If linking BLAS/LAPACK "
                   "fails with unresolved symbols like 'dgemm_', your library uses a "
                   "different convention; configure with -DCMAKE_Fortran_COMPILER=... "
                   "so the real mangling can be detected.")
    file(WRITE "${path}" "${content}")
  endif()
endfunction()

macro(opm-grid_language_hook)
  if(CMAKE_Fortran_COMPILER)
    include(FortranCInterface)
    fortrancinterface_header(${PROJECT_BINARY_DIR}/FCMacros.h MACRO_NAMESPACE FC_)
  endif()
  # Two ways to end up without usable macros: no Fortran compiler at all, or
  # a compiler FortranCInterface could not link against the C toolchain (MinGW
  # gfortran on PATH with an MSVC/Ninja toolchain, say). The latter is reported
  # as a status message rather than an error and leaves behind a header with no
  # macros in it, which __has_include() is happy to find, so test the result
  # rather than the compiler.
  if(NOT FortranCInterface_GLOBAL_FOUND)
    opm_write_fallback_fcmacros(${PROJECT_BINARY_DIR}/FCMacros.h)
  endif()
endmacro()

macro(opm-grid_config_hook)
  opm_need_version_of ("dune-grid")
endmacro()

macro(opm-grid_targets_hook)
  target_include_directories(opmgrid PRIVATE ${PROJECT_BINARY_DIR})
endmacro()

macro(opm-grid_tests_hook)
  # Additional parallel tests
  if(MPI_FOUND)
    set(tests_4proc
      addLgrsOnDistributedGrid_test
      autoRefine_test
      distribution_test
      level_and_grid_cartesianIndexMappers_test
      logicalCartesianSize_and_refinement_test
      test_communication_utils
      id_entity_entityrep_test
    )
    if(USE_OPM_COMMON)
      list(APPEND tests_4proc
        communicate_distributed_grid_with_lgrs_test
        distribute_level_zero_from_grid_with_lgrs_test
        distribute_level_zero_from_grid_with_lgrs_and_wells_test
        grid_global_id_set_test
        lgr_cell_id_sync_test
        mapLevelIndicesToCartesianOutputOrder_test
      )
    endif()

    list(APPEND test_4proc lgr_with_inactive_parent_cells_test)
    foreach(procs 3 4)
      add_test(
        NAME
          graphofgrid_parallel${procs}
        COMMAND
          ${MPIEXEC_EXECUTABLE}
          ${MPIEXEC_NUMPROC_FLAG}
          ${procs}
          $<TARGET_FILE:test_graphofgrid_parallel>
      )
      set_tests_properties(graphofgrid_parallel${procs} PROPERTIES PROCESSORS ${procs})
    endforeach()

    foreach(test4 ${tests_4proc})
      add_test(
        NAME
          ${test4}_parallel
        COMMAND
          ${MPIEXEC_EXECUTABLE}
          ${MPIEXEC_NUMPROC_FLAG}
          4
          $<TARGET_FILE:${test4}>
      )
      set_tests_properties(${test4}_parallel PROPERTIES PROCESSORS 4)
    endforeach()

    if(HAVE_OPM_TESTS AND USE_OPM_COMMON)
      opm_add_executable(
        TARGET
          cpgrid_aquifer_test
        SOURCES
          tests/cpgrid/cpgrid_aquifer_test.cpp
        LIBRARIES
          opmcommon
          opmgrid
          Boost::unit_test_framework
      )
      add_test(
        NAME
          cpgrid_aquifer_parallel
        COMMAND
          ${MPIEXEC_EXECUTABLE}
          ${MPIEXEC_NUMPROC_FLAG}
          4
          $<TARGET_FILE:cpgrid_aquifer_test>
          -- ${OPM_TESTS_ROOT}/aquifer-num/3D_2AQU_NUM.DATA
      )
      set_tests_properties(cpgrid_aquifer_parallel PROPERTIES PROCESSORS 4)
    endif()
  endif()
endmacro()

macro(opm-grid_install_hook)
  install(
    DIRECTORY
      doc/man1
    DESTINATION
      ${CMAKE_INSTALL_MANDIR}
    FILES_MATCHING PATTERN
      "*.1"
  )
endmacro()

if(SIBLING_SEARCH AND NOT opm-common_DIR)
  # guess the sibling dir
  get_filename_component(_leaf_dir_name ${PROJECT_BINARY_DIR} NAME)
  get_filename_component(_parent_full_dir ${PROJECT_BINARY_DIR} DIRECTORY)
  get_filename_component(_parent_dir_name ${_parent_full_dir} NAME)
  #Try if <module-name>/<build-dir> is used
  get_filename_component(_modules_dir ${_parent_full_dir} DIRECTORY)
  if(IS_DIRECTORY ${_modules_dir}/opm-common/${_leaf_dir_name})
    set(opm-common_DIR ${_modules_dir}/opm-common/${_leaf_dir_name})
  else()
    string(REPLACE ${PROJECT_NAME} opm-common _opm_common_leaf ${_leaf_dir_name})
    if(NOT _leaf_dir_name STREQUAL _opm_common_leaf
        AND IS_DIRECTORY ${_parent_full_dir}/${_opm_common_leaf})
      # We are using build directories named <prefix><module-name><postfix>
      set(opm-common_DIR ${_parent_full_dir}/${_opm_common_leaf})
    elseif(IS_DIRECTORY ${_parent_full_dir}/opm-common)
      # All modules are in a common build dir
      set(opm-common_DIR "${_parent_full_dir}/opm-common")
    endif()
  endif()
endif()
if(opm-common_DIR AND NOT IS_DIRECTORY ${opm-common_DIR})
  message(WARNING "Value ${opm-common_DIR} passed to variable"
    " opm-common_DIR is not a directory")
endif()

find_package(opm-common REQUIRED)

# project information is in dune.module. Read this file and set variables.
# we cannot generate dune.module since it is read by dunecontrol before
# the build starts, so it makes sense to keep the data there then.
include (OpmInit)

# Search opm-tests to use some of the data sets for testing
include(Findopm-tests)

# all setup common to the OPM library modules is done here
include (OpmLibMain)

