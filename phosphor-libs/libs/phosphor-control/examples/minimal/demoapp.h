// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "PhosphorControl/ApplicationController.h"

namespace PhosphorControlExamplesMinimal {

/** Demo ApplicationController that wires up two pages so the chrome has
 *  something to route. Real apps subclass ApplicationController the same
 *  way and pass it to SettingsAppWindow.controller. */
// DemoApp is constructed in C++ by main() and passed to QML via
// `engine.setInitialProperties({{ "controller", ... }})` — it never needs
// to be addressable as a QML type. No QML_NAMED_ELEMENT / QML_UNCREATABLE
// macros: registering them would only add a deadwood entry to the
// minimal-example qmltypes that nothing in QML can or should consume.
class DemoApp : public PhosphorControl::ApplicationController
{
    Q_OBJECT

public:
    explicit DemoApp(QObject* parent = nullptr);
};

} // namespace PhosphorControlExamplesMinimal
