// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include "PhosphorScreens/IScreenProvider.h"
#include "phosphorscreenscore_export.h"

class QScreen;

namespace PhosphorScreens {

/**
 * @brief Production IScreenProvider — a thin wrapper over QGuiApplication.
 *
 * Enumerates `QGuiApplication::screens()` and relays `screenAdded`,
 * `screenRemoved`, and per-screen `QScreen::geometryChanged` as the
 * IScreenProvider lifecycle signals. The connector → @ref PhysicalScreen
 * conversion fills in the EDID-aware identifier via ScreenIdentity.
 *
 * Always live: it wires up Qt's screen signals in its constructor, so it
 * needs no explicit start/stop — matching QGuiApplication's own always-on
 * screen tracking. Construct one and inject it via ScreenManagerConfig.
 */
class PHOSPHORSCREENSCORE_EXPORT QtScreenProvider : public IScreenProvider
{
    Q_OBJECT
public:
    explicit QtScreenProvider(QObject* parent = nullptr);

    QVector<PhysicalScreen> screens() const override;
    PhysicalScreen primaryScreen() const override;

private:
    void watchScreen(QScreen* screen);
    void onQtScreenAdded(QScreen* screen);
    void onQtScreenRemoved(QScreen* screen);
};

} // namespace PhosphorScreens
