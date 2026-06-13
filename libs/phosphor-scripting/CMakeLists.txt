# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# PhosphorScripting — generic embedded Luau scripting host.
#
# Owns the Luau virtual machine lifecycle, sandbox, interrupt watchdog,
# bytecode compile/load, and QVariant<->Lua marshalling. Has NO knowledge of
# tiling (or any other domain) — domain bindings (e.g. phosphor-tiles'
# LuauTileAlgorithm) depend on this library and marshal their own params via the
# QVariant API. The lua_State is kept private so no Luau symbols leak across the
# shared-library boundary.
#
# Luau is vendored as a committed source tarball (extern/luau-<ver>.tar.gz,
# pinned to 0.723), extracted from that LOCAL file at configure time and built as
# part of this project by default; pass -DPLASMAZONES_SYSTEM_LUAU=ON to link a
# system-provided Luau instead. Committing the tarball (not a submodule, not the
# unpacked tree) keeps the repo to one ~2 MB blob while staying self-contained:
# GitHub's auto-archive and every distro source tarball include it, so offline
# packaging builds need no network and no special handling.

# 3.18 floor: the vendored-Luau acquisition uses file(ARCHIVE_EXTRACT ...)
# (>= 3.18). A lower floor would fail mid-configure with an unknown-subcommand
# error instead of a clear minimum-version message.
cmake_minimum_required(VERSION 3.18)

set(PHOSPHORSCRIPTING_VERSION "0.1.0")

# Pinned vendored Luau. To bump: drop the new extern/luau-<ver>.tar.gz in,
# update both lines, and delete the old tarball.
set(PHOSPHORSCRIPTING_LUAU_VERSION "0.723")
set(PHOSPHORSCRIPTING_LUAU_SHA256 "74bf6b8842e00d236d390f9431205c73d0cf887c973f9d0656396bbf1eb987bd")

if(NOT PROJECT_VERSION)
    project(PhosphorScripting VERSION ${PHOSPHORSCRIPTING_VERSION} LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
endif()

include(GenerateExportHeader)

find_package(Qt6 6.6 REQUIRED COMPONENTS Core)
find_package(Threads REQUIRED)

# ═══════════════════════════════════════════════════════════════════════════════
# Luau acquisition — committed source tarball (default) or system (opt-in)
# ═══════════════════════════════════════════════════════════════════════════════

option(PLASMAZONES_SYSTEM_LUAU
    "Link a system-provided Luau instead of the committed extern/luau-*.tar.gz" OFF)

if(PLASMAZONES_SYSTEM_LUAU)
    find_path(LUAU_INCLUDE_DIR NAMES lua.h PATH_SUFFIXES luau Luau)
    find_library(LUAU_VM_LIBRARY NAMES Luau.VM luauvm luau_vm)
    find_library(LUAU_COMPILER_LIBRARY NAMES Luau.Compiler luaucompiler luau_compiler)
    if(NOT LUAU_INCLUDE_DIR OR NOT LUAU_VM_LIBRARY OR NOT LUAU_COMPILER_LIBRARY)
        message(FATAL_ERROR
            "PLASMAZONES_SYSTEM_LUAU=ON but a system Luau (lua.h + Luau.VM + Luau.Compiler) "
            "was not found. Install Luau dev files or unset the option to use the committed vendored tarball.")
    endif()
    add_library(Luau.VM UNKNOWN IMPORTED)
    set_target_properties(Luau.VM PROPERTIES
        IMPORTED_LOCATION "${LUAU_VM_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${LUAU_INCLUDE_DIR}")
    add_library(Luau.Compiler UNKNOWN IMPORTED)
    set_target_properties(Luau.Compiler PROPERTIES
        IMPORTED_LOCATION "${LUAU_COMPILER_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${LUAU_INCLUDE_DIR}")
else()
    # Where the committed tarball lives and where we extract it. Kept out of the
    # NOT-TARGET guard so the licence-install below can find the extracted tree.
    set(_p_luau_tarball "${CMAKE_SOURCE_DIR}/extern/luau-${PHOSPHORSCRIPTING_LUAU_VERSION}.tar.gz")
    set(_p_luau_src "${CMAKE_BINARY_DIR}/_luau/luau-${PHOSPHORSCRIPTING_LUAU_VERSION}")
    if(NOT TARGET Luau.VM)
        if(NOT EXISTS "${_p_luau_tarball}")
            message(FATAL_ERROR
                "Vendored Luau tarball missing at ${_p_luau_tarball}. It is committed in "
                "the repository, so an incomplete checkout is the likely cause — or pass "
                "-DPLASMAZONES_SYSTEM_LUAU=ON to link a system Luau instead.")
        endif()
        # Verify the committed blob before building unknown source from it.
        file(SHA256 "${_p_luau_tarball}" _p_luau_actual_sha)
        if(NOT _p_luau_actual_sha STREQUAL "${PHOSPHORSCRIPTING_LUAU_SHA256}")
            message(FATAL_ERROR
                "Vendored Luau tarball SHA256 mismatch:\n  expected ${PHOSPHORSCRIPTING_LUAU_SHA256}\n"
                "  got      ${_p_luau_actual_sha}\nThe committed ${_p_luau_tarball} is corrupt or was swapped.")
        endif()
        # Extract the committed tarball once (a LOCAL file — no network). The
        # archive has a single luau-<ver>/ top-level dir. The re-extract guard
        # keys on a stamp file written AFTER a successful extraction — keying
        # on a file tar writes early (e.g. CMakeLists.txt) would let an
        # aborted extraction (disk full, ^C mid-configure) pass the guard on
        # every later reconfigure and fail confusingly inside the vendored
        # build instead of repairing itself.
        set(_p_luau_extract_stamp "${CMAKE_BINARY_DIR}/_luau/.extracted-${PHOSPHORSCRIPTING_LUAU_VERSION}")
        if(NOT EXISTS "${_p_luau_extract_stamp}")
            file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/_luau")
            file(ARCHIVE_EXTRACT INPUT "${_p_luau_tarball}" DESTINATION "${CMAKE_BINARY_DIR}/_luau")
            file(TOUCH "${_p_luau_extract_stamp}")
        endif()
        set(LUAU_BUILD_CLI   OFF CACHE BOOL "" FORCE)
        set(LUAU_BUILD_TESTS OFF CACHE BOOL "" FORCE)
        set(LUAU_BUILD_WEB   OFF CACHE BOOL "" FORCE)
        set(LUAU_WERROR      OFF CACHE BOOL "" FORCE)
        # EXCLUDE_FROM_ALL so only the targets we actually link (Luau.VM +
        # Luau.Compiler and their deps) build — never Luau.Analysis/CLI, which we
        # don't use.
        add_subdirectory("${_p_luau_src}" "${CMAKE_BINARY_DIR}/extern/luau" EXCLUDE_FROM_ALL)
        # Luau needs exceptions (lua_pcall uses C++ unwinding) — force -fexceptions
        # on the vendored targets so they build correctly regardless of the
        # project's global exception settings. Luau also breaks under unity builds
        # (same-named file-local symbols collide), and is STATIC, so it must be
        # PIC to link into our shared lib. Override on the vendored targets only.
        # Luau.Common is NOT header-only in 0.723: it is a compiled STATIC
        # library (BytecodeWire/StringUtils/TimeTrace TUs) inside the PUBLIC
        # link closure of both Luau.VM and Luau.Compiler, so it needs every
        # override below — most critically -fexceptions (its TUs sit in the
        # lua_pcall unwind path) and PIC (it links into our SHARED lib).
        foreach(_luau_tgt Luau.Ast Luau.Common Luau.Compiler Luau.VM Luau.Bytecode Luau.CodeGen Luau.Config)
            if(TARGET ${_luau_tgt})
                set_target_properties(${_luau_tgt} PROPERTIES
                    POSITION_INDEPENDENT_CODE ON
                    UNITY_BUILD OFF)
                # -w: the vendored sources inherit the project's warning set
                # (KDECompilerSettings' -Wall/-Wextra/-Wpedantic) and drown a
                # clean build in thousands of third-party diagnostics
                # (computed gotos, non-virtual dtors, ...). They are not ours
                # to fix — silence them on the vendored targets only.
                target_compile_options(${_luau_tgt} PRIVATE -fexceptions -w)
                # Treat Luau's interface headers as SYSTEM for consumers so
                # including lua.h / DenseHash.h from our TUs doesn't surface
                # third-party header warnings there either. (The SYSTEM
                # target property needs CMake >= 3.25; on older CMake it is
                # silently ignored and only the in-target -w applies.)
                if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.25")
                    set_target_properties(${_luau_tgt} PROPERTIES SYSTEM ON)
                endif()
            endif()
        endforeach()
    endif()
endif()

# ═══════════════════════════════════════════════════════════════════════════════
# Library
# ═══════════════════════════════════════════════════════════════════════════════

set(phosphorscripting_public_HDRS
    include/PhosphorScripting/LuauEngine.h
    include/PhosphorScripting/LuauWatchdog.h
)

set(phosphorscripting_SRCS
    src/luauengine.cpp
    src/luauwatchdog.cpp
    src/luaumarshal.cpp
    src/luaumarshal.h
)

add_library(PhosphorScripting SHARED
    ${phosphorscripting_public_HDRS}
    ${phosphorscripting_SRCS}
)
add_library(PhosphorScripting::PhosphorScripting ALIAS PhosphorScripting)

generate_export_header(PhosphorScripting
    EXPORT_FILE_NAME ${CMAKE_CURRENT_BINARY_DIR}/phosphorscripting_export.h
    EXPORT_MACRO_NAME PHOSPHORSCRIPTING_EXPORT
)

if(NOT DEFINED KDE_INSTALL_INCLUDEDIR)
    include(GNUInstallDirs)
    set(KDE_INSTALL_INCLUDEDIR ${CMAKE_INSTALL_INCLUDEDIR})
endif()

target_include_directories(PhosphorScripting
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
        $<INSTALL_INTERFACE:${KDE_INSTALL_INCLUDEDIR}>
)

target_compile_features(PhosphorScripting PUBLIC cxx_std_20)

# Luau is PRIVATE: the public headers forward-declare lua_State and expose only a
# QVariant API, so no Luau headers/symbols leak to consumers.
target_link_libraries(PhosphorScripting
    PUBLIC
        Qt6::Core
    PRIVATE
        Threads::Threads
        Luau.VM
        Luau.Compiler
)

set_target_properties(PhosphorScripting PROPERTIES
    VERSION ${PHOSPHORSCRIPTING_VERSION}
    SOVERSION 0
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    UNITY_BUILD OFF
)

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
if(NOT DEFINED KDE_INSTALL_DATADIR)
    include(GNUInstallDirs)
    set(KDE_INSTALL_DATADIR ${CMAKE_INSTALL_DATAROOTDIR})
endif()

# Luau is MIT-licensed (Roblox Corporation) and, in the default vendored
# build, is statically linked into this library — so its licence notice must
# accompany the distribution. Ship it alongside the app's own licences. The
# system-Luau build links a separately-installed Luau that carries its own
# licence, so skip it there.
if(NOT PLASMAZONES_SYSTEM_LUAU)
    # _p_luau_src is the extracted tarball tree (set in the acquisition
    # block). Guarded on the file actually existing: a superbuild/parent
    # scope that pre-defines Luau.VM skips the extraction entirely, and an
    # unconditional rule would then fail at `cmake --install` time against
    # a path that was never created.
    if(EXISTS "${_p_luau_src}/LICENSE.txt")
        install(FILES ${_p_luau_src}/LICENSE.txt
            DESTINATION ${KDE_INSTALL_DATADIR}/licenses/plasmazones
            RENAME LICENSE.Luau)
    endif()
endif()

install(TARGETS PhosphorScripting
    EXPORT PhosphorScriptingTargets
    LIBRARY DESTINATION ${KDE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${KDE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${KDE_INSTALL_BINDIR}
)

install(EXPORT PhosphorScriptingTargets
    FILE PhosphorScriptingTargets.cmake
    NAMESPACE PhosphorScripting::
    DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorScripting
)

include(CMakePackageConfigHelpers)
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/PhosphorScriptingConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorScriptingConfig.cmake"
    INSTALL_DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorScripting
)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorScriptingConfigVersion.cmake"
    VERSION ${PHOSPHORSCRIPTING_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorScriptingConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorScriptingConfigVersion.cmake"
    DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorScripting
)

install(DIRECTORY include/PhosphorScripting/
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/PhosphorScripting
    FILES_MATCHING PATTERN "*.h"
)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/phosphorscripting_export.h
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/PhosphorScripting
)
