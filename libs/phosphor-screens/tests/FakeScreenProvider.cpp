// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "FakeScreenProvider.h"

namespace Phosphor::Screens {

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
    // identifier defaults to the connector name so identifier-keyed
    // lookups still resolve for a screen the test didn't bother to
    // assign a distinct EDID-style id.
    const PhysicalScreen screen{name, identifier.isEmpty() ? name : identifier, geometry, nullptr};
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
}

void FakeScreenProvider::moveScreen(const QString& name, const QRect& newGeometry)
{
    for (auto& screen : m_screens) {
        if (screen.name == name) {
            screen.geometry = newGeometry;
            Q_EMIT screenGeometryChanged(screen);
            return;
        }
    }
}

void FakeScreenProvider::setPrimary(const QString& name)
{
    m_primaryName = name;
}

} // namespace Phosphor::Screens
