# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later

# PhosphorAnimationQml unit tests. Headless — uses offscreen QPA.
# Exercises the Q_GADGET wrappers via QMetaObject reflection +
# QtQuickClockManager singleton contracts.

find_package(Qt6 REQUIRED COMPONENTS Test Quick)

function(paq_add_test _name)
    set(_sources ${ARGN})
    add_executable(${_name} ${_sources})
    set_target_properties(${_name} PROPERTIES AUTOMOC ON)
    target_include_directories(${_name}
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
    )
    target_link_libraries(${_name}
        PRIVATE
            Qt6::Test
            Qt6::Core
            Qt6::Gui
            Qt6::Quick
            PhosphorAnimation::PhosphorAnimation
            PhosphorAnimation::PhosphorAnimationQml
    )
    add_test(NAME ${_name} COMMAND ${_name})
    set_tests_properties(${_name} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_LOGGING_RULES=phosphoranimation.debug=false"
        LABELS "phosphoranimation;phosphoranimation-qml"
    )
endfunction()

paq_add_test(paq_test_phosphoreasing test_phosphoreasing.cpp)
paq_add_test(paq_test_phosphorspring test_phosphorspring.cpp)
paq_add_test(paq_test_phosphorcurve test_phosphorcurve.cpp)
paq_add_test(paq_test_phosphorprofile test_phosphorprofile.cpp)
paq_add_test(paq_test_qtquickclockmanager test_qtquickclockmanager.cpp)
paq_add_test(paq_test_phosphoranimatedreal test_phosphoranimatedreal.cpp)
paq_add_test(paq_test_phosphoranimatedtyped test_phosphoranimatedtyped.cpp)
paq_add_test(paq_test_phosphorprofileregistry test_phosphorprofileregistry.cpp)
paq_add_test(paq_test_phosphormotionanimation test_phosphormotionanimation.cpp)
paq_add_test(paq_test_animatedvalue_realwindow test_animatedvalue_realwindow.cpp)
paq_add_test(paq_test_motionanimation_behavior test_motionanimation_behavior.cpp)
# The Behavior-site test loads `import org.phosphor.animation` from
# QML, which requires the static QML module to be linked into the test
# binary so the QmlEngine resolves the URI without external plugin
# discovery (the SHARED plugin lives at install-time only).
target_link_libraries(paq_test_motionanimation_behavior
    PRIVATE PhosphorAnimation::PhosphorAnimationQml PhosphorAnimationQmlplugin Qt6::Qml)
# Linking the auto-generated `PhosphorAnimationQmlplugin` static lib
# alongside the backing PhosphorAnimationQml gives Q_IMPORT_PLUGIN in
# the test source a real symbol to anchor — without it the linker
# drops the qt_static_plugin_org_phosphor_animationPlugin definition.
