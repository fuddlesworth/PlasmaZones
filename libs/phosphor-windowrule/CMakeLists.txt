# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# PhosphorWindowRule — the unified window/context rule engine.
#
# A single composable match-expression language, pluggable slot-based action
# set, one serialization format, and one evaluation pipeline + match cache.
# Both the GPL KWin effect and the GPL daemon link this LGPL library, so there
# is exactly one match-code implementation (GPL->LGPL linking is permitted).
#
# This library does NOT own any QObject / QML / D-Bus surface — those belong
# to the higher-level GPL settings app and adaptors.
#
# Dependency surface: Qt6::Core only (NO Qt6::Gui) + PhosphorProtocol::Types
# (PUBLIC — WindowQuery surfaces PhosphorProtocol::WindowType) + PhosphorIdentity
# (PRIVATE — WindowId::appIdMatches backs the AppIdMatches operator; no public
# header leaks the type).

cmake_minimum_required(VERSION 3.16)

set(PHOSPHORWINDOWRULE_VERSION "0.1.0")

if(NOT PROJECT_VERSION)
    project(PhosphorWindowRule VERSION ${PHOSPHORWINDOWRULE_VERSION} LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_AUTOMOC ON)
endif()

include(GenerateExportHeader)

# ═══════════════════════════════════════════════════════════════════════════════
# Dependencies
# ═══════════════════════════════════════════════════════════════════════════════

# Core only — no Gui. Keeping the effect's transitive link cost low is a
# deliberate constraint (see the design doc).
find_package(Qt6 6.6 REQUIRED COMPONENTS Core)

# PhosphorProtocol::Types provides WindowTypeEnum.h, which WindowQuery includes
# and surfaces through its public API. PUBLIC because the type leaks through
# the header. Found via add_subdirectory in PZ in-tree builds; falls back to
# find_package when this library is consumed standalone.
if(NOT TARGET PhosphorProtocol::Types)
    find_package(PhosphorProtocol CONFIG REQUIRED)
endif()

# PhosphorIdentity supplies WindowId::appIdMatches(), which backs the
# AppIdMatches match operator. PRIVATE — no public PhosphorWindowRule header
# surfaces the type (the include lives in matchexpression.cpp).
if(NOT TARGET PhosphorIdentity::PhosphorIdentity)
    find_package(PhosphorIdentity CONFIG REQUIRED)
endif()

# ═══════════════════════════════════════════════════════════════════════════════
# Library
# ═══════════════════════════════════════════════════════════════════════════════

set(phosphorwindowrule_public_HDRS
    include/PhosphorWindowRule/PhosphorWindowRule.h
    include/PhosphorWindowRule/MatchTypes.h
    include/PhosphorWindowRule/WindowQuery.h
    include/PhosphorWindowRule/MatchExpression.h
    include/PhosphorWindowRule/RuleAction.h
    include/PhosphorWindowRule/WindowRule.h
    include/PhosphorWindowRule/WindowRuleSet.h
    include/PhosphorWindowRule/RuleEvaluator.h
    include/PhosphorWindowRule/WindowRuleStore.h
    include/PhosphorWindowRule/WindowRuleLogging.h
    # Header-only in-memory bridges — public API, installed via the
    # install(DIRECTORY) glob below. Listed here so the target carries them
    # as sources (IDE visibility, install-intent parity with the rest of
    # the public headers above).
    include/PhosphorWindowRule/ContextRuleBridge.h
    include/PhosphorWindowRule/ExclusionListBridge.h
    include/PhosphorWindowRule/IdentityKey.h
)

set(phosphorwindowrule_SRCS
    src/windowrulelogging.h
    src/windowrulelogging.cpp
    src/matchexpression.cpp
    src/ruleaction.cpp
    src/windowrule.cpp
    src/windowruleset.cpp
    src/ruleevaluator.cpp
    src/windowrulestore.cpp
)

add_library(PhosphorWindowRule SHARED
    ${phosphorwindowrule_public_HDRS}
    ${phosphorwindowrule_SRCS}
)

add_library(PhosphorWindowRule::PhosphorWindowRule ALIAS PhosphorWindowRule)

generate_export_header(PhosphorWindowRule
    EXPORT_FILE_NAME ${CMAKE_CURRENT_BINARY_DIR}/phosphorwindowrule_export.h
    EXPORT_MACRO_NAME PHOSPHORWINDOWRULE_EXPORT
)

if(NOT DEFINED KDE_INSTALL_INCLUDEDIR)
    include(GNUInstallDirs)
    set(KDE_INSTALL_INCLUDEDIR ${CMAKE_INSTALL_INCLUDEDIR})
endif()

target_include_directories(PhosphorWindowRule
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
        $<INSTALL_INTERFACE:${KDE_INSTALL_INCLUDEDIR}>
)

target_compile_features(PhosphorWindowRule PUBLIC cxx_std_20)

# Suppress GCC 14's `-Wsfinae-incomplete` for moc-generated TUs. Qt6's MOC
# emits boilerplate that probes Q_OBJECT subclasses in SFINAE expressions
# before their full definition is visible — the de-facto Qt6 build shape.
# Scoped PRIVATE so downstream consumers still see the diagnostic on their
# own code.
include(CheckCXXCompilerFlag)
check_cxx_compiler_flag("-Wno-sfinae-incomplete" PWR_HAS_NO_SFINAE_INCOMPLETE)
if(PWR_HAS_NO_SFINAE_INCOMPLETE)
    target_compile_options(PhosphorWindowRule PRIVATE -Wno-sfinae-incomplete)
endif()

target_link_libraries(PhosphorWindowRule
    PUBLIC
        Qt6::Core
        # WindowQuery surfaces PhosphorProtocol::WindowType — type must be
        # visible to callers that include WindowQuery.h.
        PhosphorProtocol::Types
    PRIVATE
        # WindowId::appIdMatches backs the AppIdMatches operator; PRIVATE
        # because no public PhosphorWindowRule header surfaces the type.
        PhosphorIdentity::PhosphorIdentity
)

set_target_properties(PhosphorWindowRule PROPERTIES
    VERSION ${PHOSPHORWINDOWRULE_VERSION}
    SOVERSION 0
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    UNITY_BUILD OFF
)

# ═══════════════════════════════════════════════════════════════════════════════
# Tests
# ═══════════════════════════════════════════════════════════════════════════════

if(CMAKE_PROJECT_NAME STREQUAL "PhosphorWindowRule" OR BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()

# ═══════════════════════════════════════════════════════════════════════════════
# Install
# ═══════════════════════════════════════════════════════════════════════════════

if(NOT DEFINED KDE_INSTALL_LIBDIR)
    include(GNUInstallDirs)
    set(KDE_INSTALL_LIBDIR ${CMAKE_INSTALL_LIBDIR})
    set(KDE_INSTALL_INCLUDEDIR ${CMAKE_INSTALL_INCLUDEDIR})
endif()

if(NOT DEFINED KDE_INSTALL_BINDIR)
    set(KDE_INSTALL_BINDIR ${CMAKE_INSTALL_BINDIR})
endif()

install(TARGETS PhosphorWindowRule
    EXPORT PhosphorWindowRuleTargets
    LIBRARY DESTINATION ${KDE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${KDE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${KDE_INSTALL_BINDIR}
)

install(EXPORT PhosphorWindowRuleTargets
    FILE PhosphorWindowRuleTargets.cmake
    NAMESPACE PhosphorWindowRule::
    DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorWindowRule
)

include(CMakePackageConfigHelpers)
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/PhosphorWindowRuleConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorWindowRuleConfig.cmake"
    INSTALL_DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorWindowRule
)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorWindowRuleConfigVersion.cmake"
    VERSION ${PHOSPHORWINDOWRULE_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorWindowRuleConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorWindowRuleConfigVersion.cmake"
    DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorWindowRule
)

install(DIRECTORY include/PhosphorWindowRule/
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/PhosphorWindowRule
    FILES_MATCHING PATTERN "*.h"
)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/phosphorwindowrule_export.h
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/PhosphorWindowRule
)
