// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorServiceIdle/phosphorserviceidle_export.h>

#include <QObject>
#include <QString>
#include <QVariantList>

#include <memory>

namespace PhosphorServiceIdle {

/**
 * @brief Session idle-management host for Phosphor-based desktop shells.
 *
 * Watches the session for inactivity through a configurable multi-stage timeout
 * policy and inhibits idle on request. It is the policy layer over the raw
 * Wayland clients in `phosphor-wayland` (`IdleNotifier` for `ext-idle-notify-v1`,
 * `IdleInhibitor` for `zwp-idle-inhibit-v1`); it composes them rather than
 * binding the protocols itself, so its public surface is a clean Qt/QML type
 * with no Wayland types leaking out.
 *
 * Configure the ladder with `stages` (each entry a `{ name, timeoutMs }` map);
 * the stages are sorted by ascending timeout. As inactivity grows, `currentStage`
 * advances (1-based) and `idled(stage)` fires; the first activity resets to
 * active (`currentStage == 0`) and fires `resumed()`. The shell decides what each
 * stage *does* (dim, lock, display-off); this service only reports which stage is
 * active.
 *
 * `inhibit()` / `release(cookie)` reference-count idle inhibition; while inhibited
 * the ladder is disarmed and reports active. It is mechanism, not policy: it
 * ships no default stages.
 */
class PHOSPHORSERVICEIDLE_EXPORT IdleService : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool supported READ isSupported CONSTANT)
    Q_PROPERTY(QVariantList stages READ stages WRITE setStages NOTIFY stagesChanged)
    Q_PROPERTY(int currentStage READ currentStage NOTIFY currentStageChanged)
    Q_PROPERTY(QString currentStageName READ currentStageName NOTIFY currentStageChanged)
    Q_PROPERTY(bool idle READ isIdle NOTIFY currentStageChanged)
    Q_PROPERTY(bool inhibited READ isInhibited NOTIFY inhibitedChanged)

public:
    explicit IdleService(QObject* parent = nullptr);
    ~IdleService() override;

    /// Whether the compositor advertises the idle-notification protocol this
    /// service builds on. When false the service constructs but stays inert.
    [[nodiscard]] bool isSupported() const;

    /// The idle ladder, as a list of `{ name: string, timeoutMs: int }` maps,
    /// sorted by ascending timeout. Entries with a non-positive `timeoutMs` are
    /// ignored.
    [[nodiscard]] QVariantList stages() const;
    void setStages(const QVariantList& stages);

    /// 0 when active; otherwise the 1-based index of the deepest stage currently
    /// reached.
    [[nodiscard]] int currentStage() const;
    /// Name of the current stage, or an empty string when active.
    [[nodiscard]] QString currentStageName() const;
    [[nodiscard]] bool isIdle() const;

    [[nodiscard]] bool isInhibited() const;

    /// Acquire an idle inhibition. Returns a cookie to release it with; idle is
    /// inhibited (the ladder disarmed) while any cookie is held. Retain the cookie:
    /// discarding it leaks the inhibition for the process lifetime (cookies are
    /// never reused).
    [[nodiscard]] Q_INVOKABLE int inhibit();
    /// Release a previously acquired inhibition. Returns true if @p cookie was
    /// held; a no-op returning false for an unknown or already-released cookie.
    Q_INVOKABLE bool release(int cookie);

Q_SIGNALS:
    void stagesChanged();
    void currentStageChanged();
    /// Entered idle stage @p stage (1-based).
    void idled(int stage);
    /// Returned to active from any idle stage.
    void resumed();
    void inhibitedChanged();

private:
    Q_DISABLE_COPY_MOVE(IdleService)

    class Private;
    std::unique_ptr<Private> d;
};

} // namespace PhosphorServiceIdle
