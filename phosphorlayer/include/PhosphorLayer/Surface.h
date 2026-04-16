// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorLayer/SurfaceConfig.h>
#include <PhosphorLayer/phosphorlayer_export.h>

#include <QObject>
#include <QString>

#include <memory>

QT_BEGIN_NAMESPACE
class QQuickWindow;
QT_END_NAMESPACE

namespace PhosphorLayer {

class ILayerShellTransport;
class IQmlEngineProvider;
class ITransportHandle;
class SurfaceFactory;

/**
 * @brief Bundle of dependencies the Surface inherits from the factory.
 *
 * Kept in the public header because SurfaceFactory::create() returns a
 * Surface* and the dependency pointers need to travel with it; private
 * because only SurfaceFactory constructs Surfaces. No stable ABI guarantee
 * between library versions — revisit if Surface becomes user-constructible.
 */
struct SurfaceDeps
{
    ILayerShellTransport* transport = nullptr;
    IQmlEngineProvider* engineProvider = nullptr; // Optional — nullptr = per-surface engine
    QString loggingCategory;
};

/**
 * @brief One layer-shell surface with a managed lifecycle.
 *
 * Owns its QQuickWindow and (by default) its QQmlEngine. Constructed only
 * via SurfaceFactory so injected dependencies can travel with it.
 *
 * ## Lifecycle (state machine)
 *
 * | From           | Event                    | To            |
 * |----------------|--------------------------|---------------|
 * | Constructed    | warmUp()                 | Warming       |
 * | Constructed    | show()                   | Warming → Shown |
 * | Warming        | (content loaded)         | Hidden        |
 * | Warming        | (content error)          | Failed        |
 * | Hidden/Warming | show()                   | Shown         |
 * | Shown          | hide()                   | Hiding        |
 * | Hiding         | (transport confirmed)    | Hidden        |
 * | any            | (screen removed)         | Recreating    |
 * | Recreating     | (respawn complete)       | prior state   |
 * | any            | (transport rejected)     | Failed        |
 *
 * Invalid transitions (e.g. `show()` from Failed) log `qCWarning` with the
 * debugName and no-op. They never throw.
 */
class PHOSPHORLAYER_EXPORT Surface : public QObject
{
    Q_OBJECT
    Q_PROPERTY(State state READ state NOTIFY stateChanged FINAL)

public:
    enum class State {
        Constructed, ///< Config accepted; no window yet
        Warming, ///< QML compiling / engine initializing (hidden)
        Shown, ///< Layer surface configured by compositor; visible
        Hiding, ///< hide() called; waiting for compositor ack
        Hidden, ///< Fully hidden; can transition back to Shown
        Recreating, ///< Destroyed in response to topology change; will respawn
        Failed, ///< Unrecoverable (content error, transport rejected, etc.)
    };
    Q_ENUM(State)

    ~Surface() override;

    State state() const noexcept;
    const SurfaceConfig& config() const noexcept;

    /// @name Lifecycle
    /// Idempotent and safe to call in any state. Invalid transitions warn
    /// and no-op.
    /// @{
    void show();
    void hide();
    /// Create engine + window + content, leave hidden. Pre-compiles QML so
    /// the first show() is latency-free.
    void warmUp();
    /// @}

    /// @name Escape hatches
    /// Prefer the declarative API. These are for consumers that need to
    /// reach into Qt machinery (installing event filters, connecting to
    /// window-specific signals, etc.).
    /// @{
    QQuickWindow* window() const noexcept;
    ITransportHandle* transport() const noexcept;
    /// @}

Q_SIGNALS:
    void stateChanged(State from, State to);
    void failed(const QString& reason);
    /// Last chance to save content state before a topology-driven teardown.
    /// After this signal, window() returns nullptr until the replacement
    /// Surface is constructed.
    void aboutToRecreate();

private:
    friend class SurfaceFactory;
    Surface(SurfaceConfig cfg, SurfaceDeps deps, QObject* parent);

    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace PhosphorLayer
