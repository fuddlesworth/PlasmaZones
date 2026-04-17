// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorLayer/defaults/DefaultScreenProvider.h>

#include "../internal.h"

#include <QGuiApplication>
#include <QScreen>
#include <QSet>

namespace PhosphorLayer {

class DefaultScreenProvider::Impl
{
public:
    explicit Impl(DefaultScreenProvider* q)
        : m_notifier(new ScreenProviderNotifier(q))
    {
    }

    ScreenProviderNotifier* const m_notifier;
    QSet<QScreen*> m_hookedScreens; // de-dup for Wayland platforms that re-announce screens
};

DefaultScreenProvider::DefaultScreenProvider(QObject* parent)
    : QObject(parent)
    , m_impl(std::make_unique<Impl>(this))
{
    if (auto* app = qGuiApp) {
        // Hook a single screen's geometry signals to the notifier, skipping
        // if the screen is already tracked. Some Wayland platforms
        // re-announce existing screens via screenAdded() after reparenting;
        // QSet lookup is O(1) and avoids the Qt::UniqueConnection
        // restriction (which rejects non-member-function slots).
        auto hookScreen = [this](QScreen* s) {
            if (!s || m_impl->m_hookedScreens.contains(s)) {
                return;
            }
            m_impl->m_hookedScreens.insert(s);
            auto* notifier = m_impl->m_notifier;
            // Drop from the set if the screen vanishes — Qt destroys
            // QScreen on removal, so subsequent re-adds with the same
            // address would otherwise be suppressed forever.
            connect(s, &QObject::destroyed, this, [this, s] {
                m_impl->m_hookedScreens.remove(s);
            });
            connect(s, &QScreen::geometryChanged, notifier, [notifier] {
                Q_EMIT notifier->screensChanged();
            });
            connect(s, &QScreen::availableGeometryChanged, notifier, [notifier] {
                Q_EMIT notifier->screensChanged();
            });
        };

        // Forward QGuiApplication's screen-list signals onto our notifier.
        // Connection via `this` ensures auto-disconnect on destruction.
        connect(app, &QGuiApplication::screenAdded, this, [notifier = m_impl->m_notifier, hookScreen](QScreen* s) {
            hookScreen(s);
            Q_EMIT notifier->screensChanged();
        });
        connect(app, &QGuiApplication::screenRemoved, this, [notifier = m_impl->m_notifier] {
            Q_EMIT notifier->screensChanged();
        });
        connect(app, &QGuiApplication::primaryScreenChanged, this, [notifier = m_impl->m_notifier] {
            Q_EMIT notifier->screensChanged();
            Q_EMIT notifier->focusChanged();
        });
        // Hook any already-connected screens.
        for (QScreen* s : app->screens()) {
            hookScreen(s);
        }
    } else {
        qCWarning(lcPhosphorLayer) << "DefaultScreenProvider: QGuiApplication not initialized — provider is inert";
    }
}

DefaultScreenProvider::~DefaultScreenProvider() = default;

QList<QScreen*> DefaultScreenProvider::screens() const
{
    return qGuiApp ? qGuiApp->screens() : QList<QScreen*>();
}

QScreen* DefaultScreenProvider::primary() const
{
    return qGuiApp ? qGuiApp->primaryScreen() : nullptr;
}

QScreen* DefaultScreenProvider::focused() const
{
    // Qt has no portable "focused screen" concept. Subclass and override
    // if your compositor exposes this.
    return primary();
}

ScreenProviderNotifier* DefaultScreenProvider::notifier() const
{
    return m_impl->m_notifier;
}

} // namespace PhosphorLayer
