// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorLayer/phosphorlayer_export.h>

#include <QList>
#include <QObject>

QT_BEGIN_NAMESPACE
class QScreen;
QT_END_NAMESPACE

namespace PhosphorLayer {

/**
 * @brief QObject that emits signals for IScreenProvider state changes.
 *
 * Exposed separately from IScreenProvider because the interface itself is
 * non-QObject (so implementers can freely multiple-inherit it into
 * domain QObjects without the Qt multiple-QObject-inheritance restriction).
 * Implementations return a pointer to a notifier they own; consumers
 * connect to it with type-safe Qt5-style `connect(...)`.
 */
class PHOSPHORLAYER_EXPORT ScreenProviderNotifier : public QObject
{
    Q_OBJECT
public:
    explicit ScreenProviderNotifier(QObject* parent = nullptr);
    ~ScreenProviderNotifier() override;

Q_SIGNALS:
    /// The screen list or geometry has changed. Consumers diff the new
    /// IScreenProvider::screens() against their tracked set.
    void screensChanged();

    /// IScreenProvider::focused() would now return a different QScreen*.
    /// Implementations that can't track focus omit emitting this.
    void focusChanged();
};

/**
 * @brief Source-of-truth interface for the available QScreen set.
 *
 * Abstracted so PlasmaZones can inject a virtual-screen-aware provider
 * (where one physical QScreen may host several logical "screens") while
 * a standalone consumer uses the default QGuiApplication-backed one.
 */
class PHOSPHORLAYER_EXPORT IScreenProvider
{
public:
    IScreenProvider() = default;
    virtual ~IScreenProvider() = default;
    Q_DISABLE_COPY_MOVE(IScreenProvider)

    /// Full list of screens the surfaces should be aware of.
    virtual QList<QScreen*> screens() const = 0;

    /// Canonical "primary" screen. SurfaceFactory falls back here when a
    /// SurfaceConfig doesn't pin a specific QScreen*. Also used as the
    /// recovery target when a bound screen is hot-unplugged.
    virtual QScreen* primary() const = 0;

    /// Screen currently containing the focus / cursor. Implementations that
    /// don't track focus return @ref primary(). Consumers pick between
    /// @ref primary() and @ref focused() themselves when constructing a
    /// SurfaceConfig; the library does not carry an affinity enum.
    virtual QScreen* focused() const = 0;

    /// Notifier for signal-driven updates. Pointer is owned by the provider;
    /// consumers must not delete it. Lifetime >= the provider's.
    virtual ScreenProviderNotifier* notifier() const = 0;
};

} // namespace PhosphorLayer
