# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later

find_package(Qt6 6.10 REQUIRED COMPONENTS Test Qml Quick)

# Smoke test for the security boundary in ShellLoader (path-traversal /
# absolute-path / dotfile rejection). Adding more PhosphorShell tests
# in this directory keeps them under one CTest target prefix.

# Shared test isolation: a private D-Bus session (so a test that touches D-Bus
# cannot activate the INSTALLED daemon, which then holds the test's stdout pipe
# open and hangs ctest after the test itself passed) plus a per-target XDG
# sandbox (so nothing writes into the developer's real ~/.config/plasmazones).
include(${CMAKE_SOURCE_DIR}/cmake/PhosphorTestIsolation.cmake)
add_executable(test_phosphorshell_shellloader test_shellloader.cpp)
target_link_libraries(test_phosphorshell_shellloader
    PRIVATE
        Qt6::Test
        PhosphorShell::PhosphorShell
)
add_test(NAME test_phosphorshell_shellloader COMMAND test_phosphorshell_shellloader)
phosphor_apply_test_isolation(test_phosphorshell_shellloader)
# APPEND, never a bare SET: `set_tests_properties(... PROPERTIES
# ENVIRONMENT ...)` REPLACES the whole list and would drop the XDG
# sandbox `phosphor_apply_test_isolation` just installed.
phosphor_append_test_environment(test_phosphorshell_shellloader "QT_QPA_PLATFORM=offscreen")
set_tests_properties(test_phosphorshell_shellloader PROPERTIES
    LABELS "phosphorshell"
)

# PerScreen QML helper (Phase 1.5). Loads PerScreen.qml directly from
# the source tree via QQmlComponent::setData with a path-imported
# directory, so the test doesn't depend on the Phosphor.Shell static
# plugin being importable from a unit-test binary.
# perscreen_fakemodel.h declares a Q_OBJECT subclass of QAbstractListModel.
# AUTOMOC needs it listed as a source so the moc'd vtable / staticMetaObject
# get generated and linked into this target.
add_executable(test_phosphorshell_perscreen
    test_perscreen.cpp
    perscreen_fakemodel.h
)
target_link_libraries(test_phosphorshell_perscreen
    PRIVATE
        Qt6::Test
        Qt6::Qml
        Qt6::Quick
        PhosphorShell::PhosphorShell
)
# Source-dir path to the QML directory holding PerScreen.qml. Embedded
# at compile time so the test doesn't need install or relative-path
# resolution from $CWD.
target_compile_definitions(test_phosphorshell_perscreen
    PRIVATE
        PERSCREEN_QML_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../qml/Phosphor/Shell"
)
add_test(NAME test_phosphorshell_perscreen COMMAND test_phosphorshell_perscreen)
phosphor_apply_test_isolation(test_phosphorshell_perscreen)
# APPEND, never a bare SET: `set_tests_properties(... PROPERTIES
# ENVIRONMENT ...)` REPLACES the whole list and would drop the XDG
# sandbox `phosphor_apply_test_isolation` just installed.
phosphor_append_test_environment(test_phosphorshell_perscreen "QT_QPA_PLATFORM=offscreen")
set_tests_properties(test_phosphorshell_perscreen PROPERTIES
    LABELS "phosphorshell"
    TIMEOUT 30
)
