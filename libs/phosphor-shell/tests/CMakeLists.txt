# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later

find_package(Qt6 6.10 REQUIRED COMPONENTS Test Qml Quick)

# Smoke test for the security boundary in ShellLoader (path-traversal /
# absolute-path / dotfile rejection). Adding more PhosphorShell tests
# in this directory keeps them under one CTest target prefix.
add_executable(test_phosphorshell_shellloader test_shellloader.cpp)
target_link_libraries(test_phosphorshell_shellloader
    PRIVATE
        Qt6::Test
        PhosphorShell::PhosphorShell
)
add_test(NAME test_phosphorshell_shellloader COMMAND test_phosphorshell_shellloader)
set_tests_properties(test_phosphorshell_shellloader
    PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
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
set_tests_properties(test_phosphorshell_perscreen
    PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        LABELS "phosphorshell"
        TIMEOUT 30
)
