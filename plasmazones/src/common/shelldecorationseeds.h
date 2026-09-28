// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <PhosphorSurface/DecorationProfileTree.h>
#include <QVariantMap>

namespace PhosphorTheme {
class AppearanceWatcher;
}
namespace PlasmaZones {
class Settings;

// Application composition only: core Settings has no dependency on shell theme
// libraries. User profiles are overlaid by Settings above these runtime defaults.
PhosphorSurfaceShaders::DecorationProfileTree shellDecorationSeedTree(const QVariantMap& appearance,
                                                                      bool desktopStyleActive);
void bindShellDecorationSeeds(Settings& settings, PhosphorTheme::AppearanceWatcher& appearance);
void installShellDecorationSeeds(Settings& settings);
}
