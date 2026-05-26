// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "PhosphorSettingsUi/ApplicationController.h"

namespace PhosphorSettingsUiExamplesMinimal {

/** Demo ApplicationController that wires up two pages so the chrome has
 *  something to route. Real apps subclass ApplicationController the same
 *  way and pass it to SettingsAppWindow.controller. */
class DemoApp : public PhosphorSettingsUi::ApplicationController
{
    Q_OBJECT
    QML_NAMED_ELEMENT(DemoApp)
    QML_UNCREATABLE("Constructed in C++ by main().")

public:
    explicit DemoApp(QObject* parent = nullptr);
};

} // namespace PhosphorSettingsUiExamplesMinimal
