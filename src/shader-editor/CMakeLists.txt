# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later

set(plasmazones_shader_editor_SRCS
    main.cpp
    shadereditorwindow.cpp
    shadereditorwindow.h
    shaderpackageio.cpp
    shaderpackageio.h
)

add_executable(plasmazones-shader-editor ${plasmazones_shader_editor_SRCS})

target_link_libraries(plasmazones-shader-editor
    PRIVATE
        plasmazones_core
        plasmazones_rendering
        Qt6::Widgets
        KF6::TextEditor
        KF6::I18n
        KF6::CoreAddons
        KF6::WindowSystem
)

install(TARGETS plasmazones-shader-editor DESTINATION ${KDE_INSTALL_BINDIR})

# Precompiled headers for faster compilation
if(ENABLE_PCH)
    target_precompile_headers(plasmazones-shader-editor
        PRIVATE
            <QObject>
            <QString>
            <QList>
            <QVariant>
            <QHash>
            <QDebug>
            <QMainWindow>
    )
endif()

# Desktop file
install(FILES
    org.plasmazones.shader-editor.desktop
    DESTINATION ${KDE_INSTALL_APPDIR}
)
