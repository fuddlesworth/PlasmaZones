# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# PhosphorWindowRules — the unified window/context rule engine.
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

set(PHOSPHORWINDOWRULES_VERSION "0.1.0")

if(NOT PROJECT_VERSION)
    project(PhosphorWindowRules VERSION ${PHOSPHORWINDOWRULES_VERSION} LANGUAGES CXX)
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
# AppIdMatches match operator. PRIVATE — no public PhosphorWindowRules header
# surfaces the type (the include lives in matchexpression.cpp).
if(NOT TARGET PhosphorIdentity::PhosphorIdentity)
    find_package(PhosphorIdentity CONFIG REQUIRED)
endif()

# PhosphorFsLoader provides WatchedDirectorySet, which backs the opt-in
# WindowRuleStoreWatcher. PRIVATE — the watcher's public header forward-declares
# the fsloader type and never includes it, so consumers don't inherit the dep.
# Core-only (no Gui), preserving this library's Core-only contract.
if(NOT TARGET PhosphorFsLoader::PhosphorFsLoader)
    find_package(PhosphorFsLoader 0.1.0 REQUIRED)
endif()

# ═══════════════════════════════════════════════════════════════════════════════
# Library
# ═══════════════════════════════════════════════════════════════════════════════

set(phosphorwindowrules_public_HDRS
    include/PhosphorWindowRules/PhosphorWindowRules.h
    include/PhosphorWindowRules/MatchTypes.h
    include/PhosphorWindowRules/WindowQuery.h
    include/PhosphorWindowRules/MatchExpression.h
    include/PhosphorWindowRules/RuleAction.h
    include/PhosphorWindowRules/WindowRule.h
    include/PhosphorWindowRules/WindowRuleSet.h
    include/PhosphorWindowRules/RuleEvaluator.h
    include/PhosphorWindowRules/WindowRuleStore.h
    include/PhosphorWindowRules/WindowRuleStoreWatcher.h
    include/PhosphorWindowRules/WindowRuleLogging.h
    # In-memory bridges — public API, installed via the install(DIRECTORY)
    # glob below. Listed here so the target carries them as sources (IDE
    # visibility, install-intent parity with the rest of the public
    # headers above). `ExclusionRules` has its body in `src/` (link edge,
    # not header inline); `ContextRuleBridge` and `IdentityKey` are still
    # header-only.
    include/PhosphorWindowRules/ContextRuleBridge.h
    include/PhosphorWindowRules/ExclusionRules.h
    include/PhosphorWindowRules/IdentityKey.h
)

set(phosphorwindowrules_SRCS
    src/windowrulelogging.h
    src/windowrulelogging.cpp
    src/matchexpression.cpp
    src/ruleaction.cpp
    src/windowrule.cpp
    src/windowruleset.cpp
    src/ruleevaluator.cpp
    src/windowrulestore.cpp
    src/windowrulestorewatcher.cpp
    src/exclusionrules.cpp
)

add_library(PhosphorWindowRules SHARED
    ${phosphorwindowrules_public_HDRS}
    ${phosphorwindowrules_SRCS}
)

add_library(PhosphorWindowRules::PhosphorWindowRules ALIAS PhosphorWindowRules)

generate_export_header(PhosphorWindowRules
    EXPORT_FILE_NAME ${CMAKE_CURRENT_BINARY_DIR}/phosphorwindowrules_export.h
    EXPORT_MACRO_NAME PHOSPHORWINDOWRULES_EXPORT
)

if(NOT DEFINED KDE_INSTALL_INCLUDEDIR)
    include(GNUInstallDirs)
    set(KDE_INSTALL_INCLUDEDIR ${CMAKE_INSTALL_INCLUDEDIR})
endif()

target_include_directories(PhosphorWindowRules
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
        $<INSTALL_INTERFACE:${KDE_INSTALL_INCLUDEDIR}>
)

target_compile_features(PhosphorWindowRules PUBLIC cxx_std_20)

# Suppress GCC 14's `-Wsfinae-incomplete` for moc-generated TUs. Qt6's MOC
# emits boilerplate that probes Q_OBJECT subclasses in SFINAE expressions
# before their full definition is visible — the de-facto Qt6 build shape.
# Scoped PRIVATE so downstream consumers still see the diagnostic on their
# own code.
include(CheckCXXCompilerFlag)
check_cxx_compiler_flag("-Wno-sfinae-incomplete" PWR_HAS_NO_SFINAE_INCOMPLETE)
if(PWR_HAS_NO_SFINAE_INCOMPLETE)
    target_compile_options(PhosphorWindowRules PRIVATE -Wno-sfinae-incomplete)
endif()

target_link_libraries(PhosphorWindowRules
    PUBLIC
        Qt6::Core
        # WindowQuery surfaces PhosphorProtocol::WindowType — type must be
        # visible to callers that include WindowQuery.h.
        PhosphorProtocol::Types
    PRIVATE
        # WindowId::appIdMatches backs the AppIdMatches operator; PRIVATE
        # because no public PhosphorWindowRules header surfaces the type.
        PhosphorIdentity::PhosphorIdentity
        # WatchedDirectorySet backs WindowRuleStoreWatcher; PRIVATE because the
        # watcher's header forward-declares the type and the .cpp owns the dep.
        PhosphorFsLoader::PhosphorFsLoader
)

set_target_properties(PhosphorWindowRules PROPERTIES
    VERSION ${PHOSPHORWINDOWRULES_VERSION}
    SOVERSION 0
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    UNITY_BUILD OFF
)

# ═══════════════════════════════════════════════════════════════════════════════
# Tests
# ═══════════════════════════════════════════════════════════════════════════════

if(CMAKE_PROJECT_NAME STREQUAL "PhosphorWindowRules" OR BUILD_TESTING)
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

install(TARGETS PhosphorWindowRules
    EXPORT PhosphorWindowRulesTargets
    LIBRARY DESTINATION ${KDE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${KDE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${KDE_INSTALL_BINDIR}
)

install(EXPORT PhosphorWindowRulesTargets
    FILE PhosphorWindowRulesTargets.cmake
    NAMESPACE PhosphorWindowRules::
    DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorWindowRules
)

include(CMakePackageConfigHelpers)
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/PhosphorWindowRulesConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorWindowRulesConfig.cmake"
    INSTALL_DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorWindowRules
)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorWindowRulesConfigVersion.cmake"
    VERSION ${PHOSPHORWINDOWRULES_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorWindowRulesConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorWindowRulesConfigVersion.cmake"
    DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorWindowRules
)

install(DIRECTORY include/PhosphorWindowRules/
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/PhosphorWindowRules
    FILES_MATCHING PATTERN "*.h"
)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/phosphorwindowrules_export.h
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/PhosphorWindowRules
)
