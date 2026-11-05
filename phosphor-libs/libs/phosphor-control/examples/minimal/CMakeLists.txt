# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later

# Tiny end-to-end demo proving the phosphor-control framework boots
# and routes pages. Run with:
#   cmake --build build --target phosphor-control-minimal
#   ./build/bin/phosphor-control-minimal

# Inherit the QML policies set by the parent lib's CMakeLists.txt. This
# subdirectory is always built under the parent project (there is no
# cmake_minimum_required / project() declaration here, so it can't be a
# top-level build) — the qt_policy guards exist so a future standalone
# build can opt in without code change. When that future build lands it
# will need its own cmake_minimum_required / project() / find_package
# bootstrap; the guard below is intentionally a no-op under normal
# in-tree builds where the parent already set the policies.
if(COMMAND qt_policy)
    qt_policy(SET QTP0001 NEW)
    # QTP0004 exists only from Qt 6.8 — qt_policy(SET) on an unknown policy
    # fatal-errors, and the COMMAND guard alone doesn't prove the policy
    # exists (qt_policy ships from 6.5). QT_KNOWN_POLICY_*, not
    # if(POLICY ...), which only knows CMP* CMake policies.
    if(QT_KNOWN_POLICY_QTP0004)
        qt_policy(SET QTP0004 NEW)
    endif()
endif()

# Pull in Widgets locally — the parent lib's find_package only lists the
# components the framework itself needs (Core / DBus / Gui / Qml / Quick).
# The minimal demo wants QApplication for the realistic-consumer baseline,
# which lives in Widgets. Keep this find local to the demo so the parent
# lib stays Widgets-free for headless or non-desktop consumers.
find_package(Qt6 6.6 REQUIRED COMPONENTS Widgets)

add_executable(phosphor-control-minimal main.cpp)

set(phosphor_control_minimal_QML_TYPE_SOURCES
    demoapp.cpp
    demoapp.h
    generalpage.cpp
    generalpage.h
)

set(phosphor_control_minimal_qml_FILES
    qml/Main.qml
    qml/GeneralPage.qml
    qml/AboutPage.qml
)

qt_add_qml_module(phosphor-control-minimal
    URI org.phosphor.control.examples.minimal
    VERSION 1.0
    RESOURCE_PREFIX /qt/qml
    # SOURCES lists the C++ types Qt should register into the QML
    # module — without this, qmltyperegistrar misses DemoApp /
    # GeneralPage's QML_NAMED_ELEMENT markers on clean builds that
    # disable global AUTOMOC, and consumers get a runtime
    # "GeneralPage is not a type" failure. Mirrors the parent lib's
    # CMakeLists pattern.
    SOURCES ${phosphor_control_minimal_QML_TYPE_SOURCES}
    QML_FILES ${phosphor_control_minimal_qml_FILES}
    OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/qml/org/phosphor/control/examples/minimal
)

target_link_libraries(phosphor-control-minimal
    PRIVATE
        PhosphorControl::PhosphorControl
        PhosphorControl::PhosphorControlQml
        Qt6::Core
        Qt6::Gui
        # QApplication (used in main.cpp instead of QGuiApplication) so the
        # demo matches the realistic Kirigami + org.kde.desktop pattern that
        # consumers building dialogs / file pickers / native colour choosers
        # need. Adds a Widgets dependency but keeps the demo on the same
        # baseline as a production settings app.
        Qt6::Widgets
        Qt6::Qml
        Qt6::Quick
        Qt6::QuickControls2
)

set_target_properties(phosphor-control-minimal PROPERTIES
    AUTOMOC ON
)
