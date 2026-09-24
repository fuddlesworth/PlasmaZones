// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorLayer/IScreenProvider.h>

#include <QGuiApplication>
#include <QList>
#include <QScreen>

namespace PhosphorLayer::Testing {

/**
 * @brief Controllable IScreenProvider for unit tests.
 *
 * Does NOT create QScreen instances — Qt doesn't permit that. Uses the
 * QScreen* pointers from QGuiApplication (which the QTEST_MAIN macro
 * initialises with the offscreen QPA). Tests can hide/reveal screens by
 * toggling which of the real screens the mock reports.
 */
class MockScreenProvider : public IScreenProvider
{
public:
    MockScreenProvider()
        : m_notifier(std::make_unique<ScreenProviderNotifier>())
    {
        reset();
    }

    QList<QScreen*> screens() const override
    {
        return m_screens;
    }
    QScreen* primary() const override
    {
        return m_primary;
    }
    QScreen* focused() const override
    {
        return m_focused ? m_focused : m_primary;
    }
    ScreenProviderNotifier* notifier() const override
    {
        return m_notifier.get();
    }

    // Test drivers ─────────────────────────────────────────────────────

    /// Reset to QGuiApplication's current screen set.
    void reset()
    {
        m_screens = qGuiApp ? qGuiApp->screens() : QList<QScreen*>();
        m_primary = qGuiApp ? qGuiApp->primaryScreen() : nullptr;
        m_focused = m_primary;
    }
    void setScreens(QList<QScreen*> s)
    {
        m_screens = std::move(s);
        if (!m_screens.contains(m_primary)) {
            m_primary = m_screens.isEmpty() ? nullptr : m_screens.first();
        }
        Q_EMIT m_notifier->screensChanged();
    }
    void setFocused(QScreen* s)
    {
        m_focused = s;
        Q_EMIT m_notifier->focusChanged();
    }
    void emitScreensChanged()
    {
        Q_EMIT m_notifier->screensChanged();
    }

    QList<QScreen*> m_screens;
    QScreen* m_primary = nullptr;
    QScreen* m_focused = nullptr;
    std::unique_ptr<ScreenProviderNotifier> m_notifier;
};

} // namespace PhosphorLayer::Testing
