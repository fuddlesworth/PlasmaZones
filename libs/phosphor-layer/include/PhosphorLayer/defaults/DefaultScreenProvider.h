// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorLayer/IScreenProvider.h>
#include <PhosphorLayer/phosphorlayer_export.h>

#include <QObject>

#include <memory>

namespace PhosphorLayer {

/**
 * @brief IScreenProvider backed by QGuiApplication::screens().
 *
 * Standalone consumers (panel applets, notification daemons, lockscreens)
 * use this directly. PlasmaZones injects a virtual-screen-aware provider
 * in its place.
 *
 * @par focused() behaviour
 * Returns @ref primary() by default. Qt does not expose a portable
 * "focused screen" concept — that belongs to the compositor. Subclass
 * and override if your compositor exposes the info via a side channel
 * (KWin's QDBus activeScreen, e.g.).
 */
class PHOSPHORLAYER_EXPORT DefaultScreenProvider : public QObject, public IScreenProvider
{
    Q_OBJECT
public:
    explicit DefaultScreenProvider(QObject* parent = nullptr);
    ~DefaultScreenProvider() override;

    QList<QScreen*> screens() const override;
    QScreen* primary() const override;
    QScreen* focused() const override;
    ScreenProviderNotifier* notifier() const override;

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace PhosphorLayer
