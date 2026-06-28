# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# PhosphorRules — the unified window/context rule engine.
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

set(PHOSPHORRULES_VERSION "0.1.0")

if(NOT PROJECT_VERSION)
    project(PhosphorRules VERSION ${PHOSPHORRULES_VERSION} LANGUAGES CXX)
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
# the header. Found via add_subdirectory in Phosphor in-tree builds; falls back to
# find_package when this library is consumed standalone.
if(NOT TARGET PhosphorProtocol::Types)
    find_package(PhosphorProtocol CONFIG REQUIRED)
endif()

# PhosphorIdentity supplies WindowId::appIdMatches(), which backs the
# AppIdMatches match operator. PRIVATE — no public PhosphorRules header
# surfaces the type (the include lives in matchexpression.cpp).
if(NOT TARGET PhosphorIdentity::PhosphorIdentity)
    find_package(PhosphorIdentity CONFIG REQUIRED)
endif()

# PhosphorFsLoader provides WatchedDirectorySet, which backs the opt-in
# RuleStoreWatcher. PRIVATE — the watcher's public header forward-declares
# the fsloader type and never includes it, so consumers don't inherit the dep.
# Core-only (no Gui), preserving this library's Core-only contract.
if(NOT TARGET PhosphorFsLoader::PhosphorFsLoader)
    find_package(PhosphorFsLoader 0.1.0 REQUIRED)
endif()

# ═══════════════════════════════════════════════════════════════════════════════
# Library
# ═══════════════════════════════════════════════════════════════════════════════

set(phosphorrules_public_HDRS
    include/PhosphorRules/PhosphorRules.h
    include/PhosphorRules/MatchTypes.h
    include/PhosphorRules/WindowQuery.h
    include/PhosphorRules/MatchExpression.h
    include/PhosphorRules/RuleAction.h
    include/PhosphorRules/Rule.h
    include/PhosphorRules/RuleSet.h
    include/PhosphorRules/RuleEvaluator.h
    include/PhosphorRules/RuleStore.h
    include/PhosphorRules/RuleStoreWatcher.h
    include/PhosphorRules/RuleLogging.h
    # In-memory bridges — public API, installed via the install(DIRECTORY)
    # glob below. Listed here so the target carries them as sources (IDE
    # visibility, install-intent parity with the rest of the public
    # headers above). `ExclusionRules` has its body in `src/` (link edge,
    # not header inline); `ContextRuleBridge` and `IdentityKey` are still
    # header-only.
    include/PhosphorRules/ContextRuleBridge.h
    include/PhosphorRules/ExclusionRules.h
    include/PhosphorRules/IdentityKey.h
)

set(phosphorrules_SRCS
    src/rulelogging.h
    src/rulelogging.cpp
    src/matchexpression.cpp
    src/ruleaction.cpp
    src/rule.cpp
    src/ruleset.cpp
    src/ruleevaluator.cpp
    src/rulestore.cpp
    src/rulestorewatcher.cpp
    src/exclusionrules.cpp
)

add_library(PhosphorRules SHARED
    ${phosphorrules_public_HDRS}
    ${phosphorrules_SRCS}
)

add_library(PhosphorRules::PhosphorRules ALIAS PhosphorRules)

generate_export_header(PhosphorRules
    EXPORT_FILE_NAME ${CMAKE_CURRENT_BINARY_DIR}/phosphorrules_export.h
    EXPORT_MACRO_NAME PHOSPHORRULES_EXPORT
)

if(NOT DEFINED KDE_INSTALL_INCLUDEDIR)
    include(GNUInstallDirs)
    set(KDE_INSTALL_INCLUDEDIR ${CMAKE_INSTALL_INCLUDEDIR})
endif()

target_include_directories(PhosphorRules
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
        $<INSTALL_INTERFACE:${KDE_INSTALL_INCLUDEDIR}>
)

target_compile_features(PhosphorRules PUBLIC cxx_std_20)

# Suppress GCC 14's `-Wsfinae-incomplete` for moc-generated TUs. Qt6's MOC
# emits boilerplate that probes Q_OBJECT subclasses in SFINAE expressions
# before their full definition is visible — the de-facto Qt6 build shape.
# Scoped PRIVATE so downstream consumers still see the diagnostic on their
# own code.
include(CheckCXXCompilerFlag)
check_cxx_compiler_flag("-Wno-sfinae-incomplete" PWR_HAS_NO_SFINAE_INCOMPLETE)
if(PWR_HAS_NO_SFINAE_INCOMPLETE)
    target_compile_options(PhosphorRules PRIVATE -Wno-sfinae-incomplete)
endif()

target_link_libraries(PhosphorRules
    PUBLIC
        Qt6::Core
        # WindowQuery surfaces PhosphorProtocol::WindowType — type must be
        # visible to callers that include WindowQuery.h.
        PhosphorProtocol::Types
    PRIVATE
        # WindowId::appIdMatches backs the AppIdMatches operator; PRIVATE
        # because no public PhosphorRules header surfaces the type.
        PhosphorIdentity::PhosphorIdentity
        # WatchedDirectorySet backs RuleStoreWatcher; PRIVATE because the
        # watcher's header forward-declares the type and the .cpp owns the dep.
        PhosphorFsLoader::PhosphorFsLoader
)

set_target_properties(PhosphorRules PROPERTIES
    VERSION ${PHOSPHORRULES_VERSION}
    SOVERSION 0
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    UNITY_BUILD OFF
)

# ═══════════════════════════════════════════════════════════════════════════════
# Tests
# ═══════════════════════════════════════════════════════════════════════════════

if(CMAKE_PROJECT_NAME STREQUAL "PhosphorRules" OR BUILD_TESTING)
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

install(TARGETS PhosphorRules
    EXPORT PhosphorRulesTargets
    LIBRARY DESTINATION ${KDE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${KDE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${KDE_INSTALL_BINDIR}
)

install(EXPORT PhosphorRulesTargets
    FILE PhosphorRulesTargets.cmake
    NAMESPACE PhosphorRules::
    DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorRules
)

include(CMakePackageConfigHelpers)
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/PhosphorRulesConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorRulesConfig.cmake"
    INSTALL_DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorRules
)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorRulesConfigVersion.cmake"
    VERSION ${PHOSPHORRULES_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorRulesConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorRulesConfigVersion.cmake"
    DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorRules
)

install(DIRECTORY include/PhosphorRules/
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/PhosphorRules
    FILES_MATCHING PATTERN "*.h"
)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/phosphorrules_export.h
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/PhosphorRules
)
