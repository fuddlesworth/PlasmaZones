// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

import QtQuick
import org.phosphor.control as Settings
import org.phosphor.control.examples.minimal

Settings.SettingsAppWindow {
    // `controller` is a required property declared by
    // SettingsAppWindow (type ApplicationController). main.cpp passes
    // the DemoApp (an ApplicationController subclass) via
    // engine.setInitialProperties({{"controller", &controller}}).
    // Compile-time checked vs the stringly-typed setContextProperty
    // form — pattern that real consumers should mirror.
    title: qsTr("Phosphor Settings Demo")
}
