// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorLayer/defaults/DefaultScreenProvider.h>

#include "../internal.h"

#include <QGuiApplication>
#include <QScreen>
#include <QSet>
#include <QTimer>

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
    bool m_screensChangedPending = false;
    bool m_focusChangedPending = false;
};

DefaultScreenProvider::DefaultScreenProvider(QObject* parent)
    : QObject(parent)
    , m_impl(std::make_unique<Impl>(this))
{
    // Coalesce bursts of screensChanged/focusChanged emissions onto a single
    // event-loop tick. Qt emits screenAdded + primaryScreenChanged (and
    // occasionally multiple geometryChanged signals) for one physical
    // hot-plug event; consumers binding expensive rebuilds should see that
    // as a single logical change.
    auto scheduleScreens = [this] {
        if (m_impl->m_screensChangedPending) {
            return;
        }
        m_impl->m_screensChangedPending = true;
        QTimer::singleShot(0, this, [this] {
            m_impl->m_screensChangedPending = false;
            Q_EMIT m_impl->m_notifier->screensChanged();
        });
    };
    auto scheduleFocus = [this] {
        if (m_impl->m_focusChangedPending) {
            return;
        }
        m_impl->m_focusChangedPending = true;
        QTimer::singleShot(0, this, [this] {
            m_impl->m_focusChangedPending = false;
            Q_EMIT m_impl->m_notifier->focusChanged();
        });
    };

    if (auto* app = qGuiApp) {
        // Hook a single screen's geometry signals to the notifier, skipping
        // if the screen is already tracked. Some Wayland platforms
        // re-announce existing screens via screenAdded() after reparenting;
        // QSet lookup is O(1) and avoids the Qt::UniqueConnection
        // restriction (which rejects non-member-function slots).
        auto hookScreen = [this, scheduleScreens](QScreen* s) {
            if (!s || m_impl->m_hookedScreens.contains(s)) {
                return;
            }
            m_impl->m_hookedScreens.insert(s);
            // Drop from the set if the screen vanishes — Qt destroys
            // QScreen on removal, so subsequent re-adds with the same
            // address would otherwise be suppressed forever. destroyed
            // fires after screenRemoved; the screenRemoved handler below
            // also prunes eagerly so intermediate primary()/focused()
            // callers never see a dangling QScreen in m_hookedScreens.
            connect(s, &QObject::destroyed, this, [this, s] {
                m_impl->m_hookedScreens.remove(s);
            });
            connect(s, &QScreen::geometryChanged, this, scheduleScreens);
            connect(s, &QScreen::availableGeometryChanged, this, scheduleScreens);
        };

        // Forward QGuiApplication's screen-list signals onto our notifier.
        // Connection via `this` ensures auto-disconnect on destruction.
        connect(app, &QGuiApplication::screenAdded, this, [scheduleScreens, hookScreen](QScreen* s) {
            hookScreen(s);
            scheduleScreens();
        });
        connect(app, &QGuiApplication::screenRemoved, this, [this, scheduleScreens](QScreen* s) {
            // Scrub eagerly — QScreen is still valid inside this handler
            // (Qt destroys it right after), so primary()/focused() callers
            // in the same tick never observe a dangling pointer in
            // m_hookedScreens. The QObject::destroyed hook stays as belt-
            // and-braces for paths that bypass Qt's removal sequencing.
            m_impl->m_hookedScreens.remove(s);
            scheduleScreens();
        });
        connect(app, &QGuiApplication::primaryScreenChanged, this, [scheduleScreens, scheduleFocus] {
            scheduleScreens();
            scheduleFocus();
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
