# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# phosphor_share_test_pch()
#
# One precompiled header, built once, that every test target in the tree
# reuses (target_precompile_headers REUSE_FROM). The tree has around six
# hundred test executables of one or two sources each. A per-target PCH would
# cost more than it saves on a target that small, so without a shared one each
# test parsed QtCore and the standard library from scratch, which was most of
# its compile.
#
# Call it from the top-level CMakeLists after every tier has been added: it
# walks the whole directory tree for the targets to attach, so a test declared
# after the call is not covered.
#
# What the header may hold is set by how GCC validates a PCH. Every macro the
# PCH was built with must be defined, identically, when it is used, and a
# consumer's extra macros are fine as long as no header in the PCH tests them.
# Test targets differ in exactly two macro families:
#   * QT_TESTCASE_BUILDDIR / QT_TESTCASE_SOURCEDIR, which Qt6::Test defines per
#     directory, and
#   * the QT_<MODULE>_LIB set (QT_GUI_LIB, QT_QUICK_LIB, ...), which follows the
#     modules each test links.
# QtTest's own headers test both, so no QtTest header can be in the PCH, and
# the PCH target links Qt6::Core alone so that QT_CORE_LIB, which every test
# has, is its only module macro. It is declared in the top-level directory, so
# its other definitions are the top-level ones every target inherits. The
# header list is QtCore and standard headers the test sources include most.
#
# A target the PCH does not fit is not broken by it, because GCC then ignores
# the PCH and parses the header as text. GCC also takes one PCH across -fPIC,
# -fPIE and non-PIC compiles, so the library directories that turn on
# CMAKE_POSITION_INDEPENDENT_CODE share it too. A target with a precompiled
# header of its own keeps it.
#
# GCC only. Clang validates a PCH strictly and fails the compile on the
# mismatches GCC tolerates (a PIC level, a macro the PCH did not have), so a
# Clang build keeps compiling each test without one.
#
# A test can compile against a header it never included, because the PCH
# included it. The non-unity, ENABLE_PCH=OFF build is the gate for that, as it
# already is for the same hole unity builds open.

include_guard(GLOBAL)

# Appends every non-imported target declared in <dir> and its subdirectories to
# the list named <out>.
function(_phosphor_targets_below dir out)
    get_property(_targets DIRECTORY "${dir}" PROPERTY BUILDSYSTEM_TARGETS)
    get_property(_subdirs DIRECTORY "${dir}" PROPERTY SUBDIRECTORIES)
    foreach(_sub IN LISTS _subdirs)
        _phosphor_targets_below("${_sub}" _below)
        list(APPEND _targets ${_below})
    endforeach()
    set(${out} ${_targets} PARENT_SCOPE)
endfunction()

function(phosphor_share_test_pch)
    if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        return()
    endif()
    # The PCH target needs one source to compile. Written with file(CONFIGURE)
    # so a reconfigure leaves it untouched and does not rebuild the PCH.
    set(_src "${CMAKE_CURRENT_BINARY_DIR}/phosphor_test_pch.cpp")
    file(CONFIGURE OUTPUT "${_src}" CONTENT "// Host for the shared test PCH (cmake/PhosphorTestPch.cmake).\n")
    add_library(phosphor_test_pch OBJECT "${_src}")
    target_link_libraries(phosphor_test_pch PRIVATE Qt6::Core)
    set_target_properties(phosphor_test_pch PROPERTIES
        AUTOMOC OFF
        AUTOUIC OFF
        AUTORCC OFF
        UNITY_BUILD OFF)
    target_precompile_headers(phosphor_test_pch PRIVATE
        <QCoreApplication>
        <QDebug>
        <QDir>
        <QFile>
        <QFileInfo>
        <QHash>
        <QJsonArray>
        <QJsonDocument>
        <QJsonObject>
        <QJsonValue>
        <QList>
        <QMap>
        <QObject>
        <QPoint>
        <QPointer>
        <QRect>
        <QRectF>
        <QRegularExpression>
        <QScopeGuard>
        <QSet>
        <QSize>
        <QStandardPaths>
        <QString>
        <QStringList>
        <QTemporaryDir>
        <QTimer>
        <QUrl>
        <QUuid>
        <QVariant>
        <QVariantMap>
        <algorithm>
        <functional>
        <limits>
        <memory>
        <optional>
        <vector>
    )

    _phosphor_targets_below("${CMAKE_SOURCE_DIR}" _all)
    set(_attached 0)
    foreach(_target IN LISTS _all)
        if(_target STREQUAL "phosphor_test_pch")
            continue()
        endif()
        get_target_property(_type ${_target} TYPE)
        if(NOT _type STREQUAL "EXECUTABLE" AND NOT _type STREQUAL "OBJECT_LIBRARY")
            continue()
        endif()
        # A test target is one declared in a tests/ directory at any depth.
        get_target_property(_dir ${_target} SOURCE_DIR)
        if(NOT _dir MATCHES "/tests(/|$)")
            continue()
        endif()
        get_target_property(_own ${_target} PRECOMPILE_HEADERS)
        get_target_property(_reuse ${_target} PRECOMPILE_HEADERS_REUSE_FROM)
        if(_own OR _reuse)
            continue()
        endif()
        # A target that does not compile C++ has nothing to precompile for.
        get_target_property(_sources ${_target} SOURCES)
        if(NOT _sources MATCHES "\\.(cpp|cxx|cc)(;|$)")
            continue()
        endif()
        target_precompile_headers(${_target} REUSE_FROM phosphor_test_pch)
        math(EXPR _attached "${_attached} + 1")
    endforeach()
    message(STATUS "Shared test PCH: attached to ${_attached} test targets")
endfunction()
