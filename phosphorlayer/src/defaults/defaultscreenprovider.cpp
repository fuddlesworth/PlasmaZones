// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorLayer/defaults/DefaultScreenProvider.h>

#include "../internal.h"

#include <QGuiApplication>
#include <QScreen>

namespace PhosphorLayer {

class DefaultScreenProvider::Impl
{
public:
    explicit Impl(DefaultScreenProvider* q)
        : m_notifier(new ScreenProviderNotifier(q))
    {
    }

    ScreenProviderNotifier* const m_notifier;
};

DefaultScreenProvider::DefaultScreenProvider(QObject* parent)
    : QObject(parent)
    , m_impl(std::make_unique<Impl>(this))
{
    if (auto* app = qGuiApp) {
        // Forward QGuiApplication's screen-list signals onto our notifier.
        // Connection via `this` ensures auto-disconnect on destruction.
        connect(app, &QGuiApplication::screenAdded, this, [n = m_impl->m_notifier](QScreen* s) {
            if (s) {
                connect(s, &QScreen::geometryChanged, n, [n] {
                    Q_EMIT n->screensChanged();
                });
                connect(s, &QScreen::availableGeometryChanged, n, [n] {
                    Q_EMIT n->screensChanged();
                });
            }
            Q_EMIT n->screensChanged();
        });
        connect(app, &QGuiApplication::screenRemoved, this, [n = m_impl->m_notifier] {
            Q_EMIT n->screensChanged();
        });
        connect(app, &QGuiApplication::primaryScreenChanged, this, [n = m_impl->m_notifier] {
            Q_EMIT n->screensChanged();
            Q_EMIT n->focusChanged();
        });
        // Hook any already-connected screens.
        for (QScreen* s : app->screens()) {
            connect(s, &QScreen::geometryChanged, m_impl->m_notifier, [n = m_impl->m_notifier] {
                Q_EMIT n->screensChanged();
            });
            connect(s, &QScreen::availableGeometryChanged, m_impl->m_notifier, [n = m_impl->m_notifier] {
                Q_EMIT n->screensChanged();
            });
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
