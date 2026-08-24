// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <PhosphorPopout/IPopoutTransport.h>

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QString>

#include <functional>

QT_BEGIN_NAMESPACE
class QQmlEngine;
class QQuickItem;
QT_END_NAMESPACE

namespace PhosphorLayer {
class IScreenProvider;
class Surface;
class SurfaceFactory;
} // namespace PhosphorLayer

namespace PhosphorShellApp {

/**
 * @brief The production IPopoutTransport: one layer-shell surface per popout.
 *
 * PopoutController owns the policy (which popout may be open, exclusivity,
 * scopes) and delegates every actual surface to a transport. Until now the
 * only implementations were an in-window demo and a stub that handed back
 * handles without creating anything, so nothing in the shipped shell could
 * open a popout at all. This is the real one.
 *
 * ## Why layer-shell rather than xdg_popup
 *
 * An xdg_popup carries grab semantics: one popup-with-grab per parent
 * surface, and closing one to open another races the compositor's grab
 * release, so the second opens and is immediately dismissed as non-topmost.
 * The legacy example shell papers over that with an 80ms timer. Layer-shell
 * has no grabs, so switching between popouts is just destroying one surface
 * and creating another. It also lets a popout be screen-centred and
 * full-bleed (a modal scrim) rather than tethered to an anchor rectangle.
 *
 * ## Surface shape
 *
 * Every popout gets a full-screen overlay surface: `Layer::Overlay`, all
 * four anchors, `exclusiveZone == -1`. The scrim and the centring both live
 * in QML (PopoutHost paints the backdrop and centres its content), so the
 * surface itself is simply "the whole output, above everything". Note that
 * `Role::isValid()` REJECTS an overlay with `exclusiveZone >= 0`, so -1 here
 * is required rather than merely conventional.
 *
 * Keyboard interactivity follows `PopoutRequest::keyboardFocus`: Exclusive
 * when the popout wants keys (a modal menu with single-key actions), None
 * when it does not, which leaves the surface click-through for keys and lets
 * the focused application keep typing.
 *
 * ## Engine
 *
 * Content is built from the SHELL's QQmlEngine, not from a private one, so a
 * delegate sees the context properties the shell publishes (BarRegistry,
 * PhosphorShell, Environment). The same engine is handed to the Surface as
 * `sharedEngine` so it does not silently construct a second, orphan engine
 * that nothing would ever use.
 *
 * That makes every live popout engine-scoped: a hot reload destroys the
 * engine and with it every instantiated delegate. `setEngine()` is how the
 * shell hands over each fresh engine, and `drain()` is how it says "the
 * current one is about to die" — the shell wires that to
 * ShellEngine::aboutToReload, which fires while the graph is still valid.
 */
class LayerPopoutTransport : public QObject, public PhosphorPopout::IPopoutTransport
{
    Q_OBJECT

public:
    LayerPopoutTransport(PhosphorLayer::SurfaceFactory* factory, PhosphorLayer::IScreenProvider* screens,
                         QObject* parent = nullptr);
    ~LayerPopoutTransport() override;

    /// Adopt the engine every subsequent popout is built from. Called for
    /// the startup engine and again for each hot-reload engine.
    void setEngine(QQmlEngine* engine);

    /// Tear down every live surface synchronously, without notifying the
    /// controller. For shutdown and for the moment before a hot reload
    /// destroys the engine: the surfaces are about to become invalid either
    /// way, and firing dismissed callbacks into a controller that is itself
    /// being torn down is how the demo transport learned to assert. The
    /// silence comes from disconnecting each host before destroying it, not
    /// from discarding the callback, which must survive for the next engine.
    void drain();

    [[nodiscard]] QString openSurface(const PhosphorPopout::PopoutRequest& request) override;
    void closeSurface(const QString& handle) override;
    void setSurfaceDismissedCallback(std::function<void(const QString&)> callback) override;

private Q_SLOTS:
    /// PopoutHost's `dismissed` signal. QML-declared signals have no
    /// compile-time member pointer, so this must be a real slot reachable
    /// through the string-based connect form.
    void onHostDismissed();

private:
    struct Entry
    {
        // QPointer throughout: nothing here is an owning handle. The Surface
        // is parented to the transport and the host dies with the surface's
        // window, so both can go away underneath an Entry that outlives them
        // for the length of a teardown.
        QPointer<PhosphorLayer::Surface> surface;
        QPointer<QQuickItem> hostItem;
        // Set by closeSurface. The contract reserves the dismissed callback
        // for surfaces that close themselves, so a close we initiated must
        // stay silent even though it drains through the same animation.
        bool closing = false;
    };

    void destroyEntry(const QString& handle, Entry entry);

    /// Recover a handle from a host by POINTER IDENTITY. PopoutHost emits
    /// `dismissed` from Component.onDestruction and forbids handlers from
    /// dereferencing it, so the handle cannot be read off the object itself.
    [[nodiscard]] QString handleForHost(const QObject* host) const;

    /// A surface went away without the controller asking: it failed, or its
    /// output was unplugged. Tears the entry down and reports it as a
    /// self-dismissal so the controller's tables do not keep a dead row.
    void onSurfaceGone(const QString& handle, const QString& reason);

    PhosphorLayer::SurfaceFactory* m_factory = nullptr;
    PhosphorLayer::IScreenProvider* m_screens = nullptr;
    QPointer<QQmlEngine> m_engine;
    QHash<QString, Entry> m_entries;
    std::function<void(const QString&)> m_dismissed;
    quint64 m_counter = 0;
};

} // namespace PhosphorShellApp
