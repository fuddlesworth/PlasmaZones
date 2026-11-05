# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# phosphorctl, typed IPC CLI for the phosphor-shell.
#
# Links Qt6::Core + Qt6::Network only (no GUI, no QML). Uses
# QCommandLineParser / QJsonDocument / QLocalSocket, all of which
# are idiomatic across the codebase already, so the binary follows
# the project's "no vendoring" policy without dragging Go / Rust
# tooling in. Reuses phosphor-libs/libs/phosphor-ipc's IpcProtocol.h to keep the
# wire format authoritative across server + client TUs (mirrors the
# phosphor-protocol-shared-between-server-and-client precedent).

# The same Qt floor as phosphor-libs/libs/phosphor-ipc/CMakeLists.txt, which
# this CLI links: the tree's QT_MIN_VERSION, or the library's own fallback
# when built on its own. Without an explicit floor a downstream consumer
# building against an older Qt6 could link this CLI against a library
# compiled with a newer Qt6 ABI.
if(DEFINED QT_MIN_VERSION)
    set(PHOSPHORCTL_QT_MIN ${QT_MIN_VERSION})
else()
    set(PHOSPHORCTL_QT_MIN "6.10.0")
endif()
find_package(Qt6 ${PHOSPHORCTL_QT_MIN} REQUIRED COMPONENTS Core Network)

add_executable(phosphorctl
    main.cpp
    client.cpp
    subcommands_common.cpp
    subcommand_call.cpp
    subcommand_list.cpp
    subcommand_schema.cpp
    subcommand_subscribe.cpp
)

target_link_libraries(phosphorctl
    PRIVATE
        PhosphorIpc::PhosphorIpc
        Qt6::Core
        Qt6::Network
)

target_compile_features(phosphorctl PRIVATE cxx_std_20)

set_target_properties(phosphorctl PROPERTIES
    UNITY_BUILD OFF
)

if(NOT DEFINED KDE_INSTALL_BINDIR)
    include(GNUInstallDirs)
    set(KDE_INSTALL_BINDIR ${CMAKE_INSTALL_BINDIR})
endif()

install(TARGETS phosphorctl
    RUNTIME DESTINATION ${KDE_INSTALL_BINDIR}
)
