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

    /// IScreenProvider::primary() would now return a different QScreen*.
    /// Implementations that alias primary to qGuiApp's primary should
    /// emit this on QGuiApplication::primaryScreenChanged; providers
    /// with their own primary policy (focused-monitor primary, virtual-
    /// screen primary) emit when their own state flips. Consumers
    /// observing `isPrimary` roles rely on this rather than reaching
    /// into qGuiApp directly — without it, multi-screen shells whose
    /// provider disagrees with qGuiApp would report a stale primary
    /// until the next full screensChanged.
    void primaryChanged();
};

/**
 * @brief Source-of-truth interface for the available QScreen set.
 *
 * Abstracted so Phosphor can inject a virtual-screen-aware provider
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
    ///
    /// MAY RETURN NULLPTR, and every consumer must handle it. A provider that
    /// cannot observe screen changes at all ships null here rather than an
    /// inert notifier that never emits, so the inability is visible to the
    /// consumer instead of looking like a topology that simply never changes.
    /// @ref DefaultScreenProvider always returns non-null; the null case is
    /// covered by `test_topology`'s `nullNotifierFromProviderDoesNotCrash`.
    ///
    /// This stays nullable even if every in-tree provider comes to return
    /// non-null — the test above is the reason, and it does not depend on who
    /// else implements this. The header is also installed and the class
    /// exported, so the implementations this repo can see need not be the
    /// whole set, and a provider that cannot observe screens is precisely the
    /// case null is here for. Do not tighten the contract, or delete a
    /// consumer's null branch, on the strength of an in-tree sweep.
    ///
    /// A consumer that gets null degrades to a one-shot snapshot taken at
    /// construction: it still answers queries, it just never refreshes. It
    /// must NOT pass the null on to `connect()` — a null sender is not a
    /// crash, but it silently drops the connection and prints one
    /// QObject::connect warning per attempt, which reads as a Qt bug rather
    /// than an unsupported provider. Resolve the pointer ONCE into a local
    /// and branch on it; the interface does not promise a stable pointer
    /// across repeated calls.
    virtual ScreenProviderNotifier* notifier() const = 0;
};

} // namespace PhosphorLayer
