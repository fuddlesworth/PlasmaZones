// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Loads Qt .qm translation files at runtime.
// Call once from main() after QGuiApplication is created.

#pragma once

#include "plasmazones_export.h"

class QCoreApplication;

namespace PlasmaZones {
PLASMAZONES_EXPORT void loadTranslations(QCoreApplication* app);
}
