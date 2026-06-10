// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "PhosphorControl/PhosphorControl.h"

namespace PhosphorControl {

QString version()
{
    // PHOSPHORCONTROL_VERSION_STR is set from PHOSPHORCONTROL_VERSION
    // in libs/phosphor-control/CMakeLists.txt so one bump updates both
    // the install version metadata and this getter.
    return QStringLiteral(PHOSPHORCONTROL_VERSION_STR);
}

} // namespace PhosphorControl
