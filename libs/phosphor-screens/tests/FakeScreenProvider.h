// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include "PhosphorScreens/IScreenProvider.h"

#include <QString>
#include <QVector>

namespace Phosphor::Screens {

/**
 * @brief Test IScreenProvider — synthesizes outputs with arbitrary geometry.
 *
 * QScreen cannot be constructed by non-platform code, so the screen
 * add / remove / move / resize sequence is otherwise undrivable in a unit
 * test. FakeScreenProvider lets a test build an output topology and then
 * fire the lifecycle signals on demand — `moveScreen` in particular
 * reproduces the transient-origin DPMS-wake scenario behind discussion
 * #465. Synthetic screens carry no QScreen (`PhysicalScreen::qscreen` is
 * null).
 */
class FakeScreenProvider : public IScreenProvider
{
    Q_OBJECT
public:
    explicit FakeScreenProvider(QObject* parent = nullptr);

    QVector<PhysicalScreen> screens() const override;
    PhysicalScreen primaryScreen() const override;

    // ─── Test control ────────────────────────────────────────────────────

    /// Connect a new output and emit screenAdded. @p identifier defaults
    /// to @p name when empty. The first screen added becomes primary.
    void addScreen(const QString& name, const QRect& geometry, const QString& identifier = QString());

    /// Disconnect @p name and emit screenRemoved. No-op if not present.
    void removeScreen(const QString& name);

    /// Change @p name's geometry and emit screenGeometryChanged. No-op if
    /// not present.
    void moveScreen(const QString& name, const QRect& newGeometry);

    /// Designate @p name the primary output (no signal — primary is a
    /// pull-only query in the IScreenProvider contract).
    void setPrimary(const QString& name);

private:
    QVector<PhysicalScreen> m_screens;
    QString m_primaryName;
};

} // namespace Phosphor::Screens
