// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorShell/phosphorshell_export.h>

#include <QHash>
#include <QMargins>
#include <QObject>
#include <QPointer>
#include <QScreen>
#include <QSize>
#include <QUrl>
#include <QVariantMap>

#include <functional>
#include <memory>
#include <vector>

QT_BEGIN_NAMESPACE
class QFileSystemWatcher;
class QQmlEngine;
class QTimer;
QT_END_NAMESPACE

namespace PhosphorLayer {
class IScreenProvider;
class Surface;
class SurfaceFactory;
} // namespace PhosphorLayer

namespace PhosphorShell {

class PanelWindow;
class PersistentProperties;
class ScreenModel;
class ShellGlobal;

class PHOSPHORSHELL_EXPORT ShellEngine : public QObject
{
    Q_OBJECT
public:
    struct Deps
    {
        PhosphorLayer::SurfaceFactory* surfaceFactory = nullptr;
        PhosphorLayer::IScreenProvider* screenProvider = nullptr;
    };

    explicit ShellEngine(Deps deps, QObject* parent = nullptr);
    ~ShellEngine() override;

    /// Build the shell from `shellUrl`. Returns true once the QML root and
    /// every panel surface exist.
    ///
    /// SINGLE-SHOT for anything past the argument checks. The early guards
    /// (empty URL, null dependency) leave the object untouched and CAN be
    /// retried; a failure inside the build cannot, and a second call is
    /// refused with `failed`. Recovery from that is a fresh ShellEngine, not
    /// a retry on this one.
    ///
    /// On failure `failed` carries the reason. Note the two shapes it comes
    /// in: a rejected call leaves a previously loaded shell running and
    /// `engine()` non-null, while a failed build tears everything down and
    /// leaves `engine()` null. `engine()` is the discriminator.
    ///
    /// After a failed FIRST load the file watcher stays armed, so editing
    /// the file recovers. That recovery reports `reloaded`, never `loaded`:
    /// `loaded` is emitted only by a successful load() call. A consumer that
    /// mounts its UI on `loaded` alone stays dark after one bad shell.qml.
    bool load(const QUrl& shellUrl);

    /// The current QML engine, or null before a successful load and after a
    /// failed one.
    ///
    /// NON-OWNING, and NOT STABLE: every hot reload destroys this engine and
    /// builds a fresh one, so a cached pointer dangles after the first file
    /// save. Anything that must survive a reload belongs in an engine hook,
    /// which is re-run against each new engine.
    [[nodiscard]] QQmlEngine* engine() const;

    /// Register a callback that fires whenever a fresh QQmlEngine is
    /// created — at startup AND on hot-reload (file watcher triggers
    /// rebuild). Use this to install image providers, register
    /// engine-scoped singletons, or set additional context properties
    /// without forcing ShellEngine to depend on the modules that
    /// supply them. The callback is invoked synchronously, in
    /// registration order, after the engine's own context properties
    /// are set but before any QML is loaded.
    ///
    /// A hook must tolerate being called for an engine that is destroyed
    /// moments later: hooks run before the QML is parsed, and a load that
    /// then fails tears that engine down immediately.
    ///
    /// Hooks cannot be removed, and the list is replayed on every reload, so
    /// EVERYTHING A HOOK CAPTURES MUST OUTLIVE THIS ShellEngine. For a hook
    /// capturing stack objects that means declaring them before the engine,
    /// so reverse destruction takes the engine first.
    ///
    /// Registering a hook from inside a hook is permitted but does not join
    /// the pass already running: addEngineHook invokes a late arrival itself
    /// when an engine already exists, and the in-flight loop deliberately
    /// does not pick it up, so it runs exactly once for that engine.
    using EngineHook = std::function<void(QQmlEngine*)>;
    void addEngineHook(EngineHook hook);

    /// The space this shell's own panels reserve on `screen`, per edge: the
    /// largest exclusive zone advertised to the compositor on each edge by a
    /// materialized panel there. Zero on every edge for a screen with no
    /// reserving panel, and for a null screen.
    ///
    /// For a popout that wants to hang from the bar rather than float mid-
    /// screen: its surface is full-bleed and cannot ask the compositor
    /// where the bar ends, but this engine placed the bar and knows. Read
    /// live, so a reload that changes a panel's thickness is reflected on
    /// the next open.
    [[nodiscard]] QMargins reservedMarginsFor(QScreen* screen) const;

Q_SIGNALS:
    /// Emitted at the very top of a hot reload, BEFORE any teardown.
    ///
    /// `loaded`, `reloaded` and `failed` all report after the fact, which
    /// is useless to anything holding objects built from the outgoing
    /// QQmlEngine: by the time they fire, that engine and every object it
    /// created are gone. A consumer that owns engine-scoped state (a
    /// popout transport holding live surfaces, say) needs a chance to drain
    /// while the graph is still valid.
    ///
    /// Covers RELOAD only, not destruction: ~ShellEngine tears down without
    /// emitting anything, so a consumer must also drain from its own
    /// shutdown path (QGuiApplication::aboutToQuit, or before the engine
    /// goes out of scope). Nor is it paired one-to-one with `reloaded`: a
    /// reload whose rebuild fails emits this and then `failed`, never
    /// `reloaded`, and the next file change emits it again, so the drain must
    /// be idempotent.
    void aboutToReload();

    /// A successful load() call. Emitted at most once per instance, and NOT
    /// emitted when a shell that failed its first load later recovers
    /// through the watcher; that reports `reloaded`. A consumer that mounts
    /// on `loaded` alone stays dark after one bad shell.qml.
    void loaded();
    /// A successful rebuild after a file or screen change, including the
    /// first successful build following a failed load().
    void reloaded();
    /// Something went wrong, with a human-readable reason.
    ///
    /// Two POSTCONDITIONS share this signal. A rejected call (empty URL,
    /// null dependency, a second load()) leaves any running shell untouched
    /// and `engine()` non-null. A failed build has already torn everything
    /// down and leaves `engine()` null. Check `engine()` to tell them apart
    /// before deciding whether to retry, fall back or exit.
    void failed(const QString& reason);

private Q_SLOTS:
    void onFileChanged();
    void onScreensChanged();

private:
    /// Build a fresh QQmlEngine, instantiate the shell QML, and
    /// materialize its panels. Shared by the initial load() and the
    /// hot-reload onFileChanged() path. Emits failed() and returns false
    /// if the QML fails to load or instantiate.
    [[nodiscard]] bool buildAndMaterialize();
    /// Build a layer surface for every PanelWindow the QML graph declares.
    /// Returns false only when the ROOT panel's surface fails, which leaves
    /// the QML root destroyed and the engine unusable; a non-root failure is
    /// logged and the remaining panels are still materialized. On false,
    /// `failureReason` (when non-null) receives the message for `failed`,
    /// which the CALLER emits after tearing down.
    [[nodiscard]] bool materializePanels(QString* failureReason);
    /// STATIC: touches no engine state. Everything it needs arrives as an
    /// argument, and saying so in the signature keeps a future edit from
    /// quietly reaching for a member.
    static void installDynamicAutoFit(PanelWindow* panel, PhosphorLayer::Surface* surface, QSize screenSize);
    /// Mask the surface's input region down to the painted band, so the
    /// shadow strip beyond `thickness` stops accepting pointer events meant
    /// for the window underneath. Re-applied on resize and on changes to the
    /// geometry inputs it derives from.
    /// STATIC, for the same reason as installDynamicAutoFit above.
    static void installInputRegion(PanelWindow* panel, PhosphorLayer::Surface* surface);
    void teardown();
    void setupWatcher();
    void savePersistentState();
    void restorePersistentState();
    /// Every PersistentProperties in this generation's object graph.
    ///
    /// Not a plain findChildren from the root: materializePanels detaches
    /// each PanelWindow from the QML root and hands it to a Surface, so a
    /// PersistentProperties declared inside a non-root panel is no longer a
    /// descendant of m_rootRef. Scanning only the root would silently drop
    /// its state across a hot reload and never register it as a
    /// ShellGlobal singleton. The root is scanned too, and the result is
    /// deduplicated because the root may itself be one of the panels.
    [[nodiscard]] QList<PersistentProperties*> collectPersistentProperties() const;

    QUrl m_shellUrl;
    std::unique_ptr<QQmlEngine> m_engine;
    std::unique_ptr<QObject> m_rootObject;
    // Non-owning observer of the QML root. When the root is a PanelWindow,
    // ownership transfers to a Surface and m_rootObject is released — but
    // findChildren scans (PersistentProperties save/restore) still need a
    // pointer to walk the tree. QPointer auto-clears when the wrapper
    // window destroys the root.
    QPointer<QObject> m_rootRef;
    Deps m_deps;
    std::vector<std::unique_ptr<PhosphorLayer::Surface>> m_surfaces;
    // The panels materialized in this generation, in creation order. They
    // are owned by their Surface's wrapper window, not by this, so these
    // are QPointers: a surface torn down out of band leaves a null entry
    // rather than a dangling one. Kept so the PersistentProperties scans
    // can reach inside a panel that was detached from the QML root.
    std::vector<QPointer<PanelWindow>> m_panels;
    // What each materialized panel reserved, for reservedMarginsFor().
    // Recorded beside the surface rather than re-derived from it: a
    // Surface exposes its window, not the PanelWindow it adopted, and the
    // zone actually sent to the compositor is the Role's, which is
    // computed once at materialization and would otherwise be lost.
    // Cleared with m_surfaces. QPointer: a screen can die before the
    // reload that rebuilds this list, and a dead entry must read as "no
    // screen", not as a match.
    struct ReservedEdge
    {
        QPointer<QScreen> screen;
        int edge = 0; // PanelWindow::Edge, kept as int to spare the header the include
        int zone = 0;
    };
    std::vector<ReservedEdge> m_reserved;
    QFileSystemWatcher* m_watcher = nullptr;
    QTimer* m_reloadTimer = nullptr;
    ScreenModel* m_screenModel = nullptr;
    ShellGlobal* m_shellGlobal = nullptr;
    QHash<QString, QVariantMap> m_persistentState;
    std::vector<EngineHook> m_engineHooks;
};

} // namespace PhosphorShell
