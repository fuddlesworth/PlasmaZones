// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "FakeScreenProvider.h"

namespace PhosphorScreens {

FakeScreenProvider::FakeScreenProvider(QObject* parent)
    : IScreenProvider(parent)
{
}

QVector<PhysicalScreen> FakeScreenProvider::screens() const
{
    return m_screens;
}

PhysicalScreen FakeScreenProvider::primaryScreen() const
{
    for (const auto& screen : m_screens) {
        if (screen.name == m_primaryName) {
            return screen;
        }
    }
    return m_screens.isEmpty() ? PhysicalScreen{} : m_screens.first();
}

void FakeScreenProvider::addScreen(const QString& name, const QRect& geometry, const QString& identifier)
{
    // Connector names are unique among connected outputs — PhysicalScreen
    // identity keys on `name`, and the manager's name-keyed caches assume
    // it. Refuse a duplicate so a mis-sequenced test (a re-add with no
    // intervening removeScreen) fails loudly rather than passing vacuously
    // against an impossible topology.
    for (const auto& existing : m_screens) {
        if (existing.name == name) {
            qWarning("FakeScreenProvider::addScreen: connector '%s' already present — ignoring duplicate",
                     qPrintable(name));
            return;
        }
    }
    // identifier defaults to the connector name so identifier-keyed
    // lookups still resolve for a screen the test didn't bother to
    // assign a distinct EDID-style id.
    const PhysicalScreen screen{.name = name,
                                .identifier = identifier.isEmpty() ? name : identifier,
                                .geometry = geometry,
                                .qscreen = nullptr};
    m_screens.append(screen);
    if (m_primaryName.isEmpty()) {
        m_primaryName = name;
    }
    Q_EMIT screenAdded(screen);
}

void FakeScreenProvider::removeScreen(const QString& name)
{
    for (int i = 0; i < m_screens.size(); ++i) {
        if (m_screens[i].name == name) {
            const PhysicalScreen removed = m_screens.takeAt(i);
            if (m_primaryName == name) {
                m_primaryName = m_screens.isEmpty() ? QString() : m_screens.first().name;
            }
            Q_EMIT screenRemoved(removed);
            return;
        }
    }
    // Removing a connector that was never added is a mis-sequenced test —
    // warn rather than no-op silently, matching addScreen's fail-loud stance.
    qWarning("FakeScreenProvider::removeScreen: connector '%s' not present — nothing removed", qPrintable(name));
}

void FakeScreenProvider::moveScreen(const QString& name, const QRect& newGeometry)
{
    for (auto& screen : m_screens) {
        if (screen.name == name) {
            if (screen.geometry == newGeometry) {
                // No geometry delta — skip the emit. QtScreenProvider relays
                // QScreen::geometryChanged, which Qt does not fire when
                // geometry() is unchanged; the fake mirrors that for the
                // geometry-move case it models. (It does not model the
                // DPR / orientation deltas a real QScreen also reports.)
                return;
            }
            screen.geometry = newGeometry;
            Q_EMIT screenGeometryChanged(screen);
            return;
        }
    }
    // Moving a connector that was never added is a mis-sequenced test —
    // warn rather than no-op silently, matching removeScreen / setPrimary.
    qWarning("FakeScreenProvider::moveScreen: connector '%s' not present — nothing moved", qPrintable(name));
}

void FakeScreenProvider::setPrimary(const QString& name)
{
    for (const auto& screen : m_screens) {
        if (screen.name == name) {
            m_primaryName = name;
            return;
        }
    }
    // A primary that is not among the connected outputs cannot exist —
    // reject it so primaryScreen() does not silently fall through to
    // m_screens.first() and mask a typo'd connector name in a test.
    qWarning("FakeScreenProvider::setPrimary: connector '%s' not present — primary unchanged", qPrintable(name));
}

} // namespace PhosphorScreens
