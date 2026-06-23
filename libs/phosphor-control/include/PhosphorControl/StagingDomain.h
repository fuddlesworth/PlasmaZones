// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

#include "phosphorcontrol_export.h"

namespace PhosphorControl {

/**
 * Abstract base for anything that holds staged user changes pending an Apply.
 *
 * A staging domain is the unit ApplicationController orchestrates: it asks
 * each domain whether it is dirty, then dispatches applyAll() / discardAll()
 * across all registered domains in one transaction.
 *
 * The two intended implementations are:
 *   - PageController — a domain that also has a QML page (a row in the sidebar).
 *   - Headless domains — cross-cutting state that spans multiple pages
 *     (e.g. per-screen assignment maps) and is registered directly with the
 *     ApplicationController without a sidebar entry.
 *
 * Implementers must emit dirtyChanged() whenever isDirty() flips. The
 * ApplicationController connects to it to recompute the global dirty flag.
 */
class PHOSPHORCONTROL_EXPORT StagingDomain : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool dirty READ isDirty NOTIFY dirtyChanged)
    QML_NAMED_ELEMENT(StagingDomain)
    QML_UNCREATABLE("StagingDomain is an abstract base; subclass in C++.")

public:
    explicit StagingDomain(QObject* parent = nullptr);
    ~StagingDomain() override;

    // Pure virtual reader — QML accesses it via the `dirty` Q_PROPERTY
    // (READ binding above) or imperatively as `domain.dirty`. No
    // Q_INVOKABLE: that would register a duplicate metaobject entry
    // covering the same getter (the property accessor moc already
    // generates one). Keep the signal/slot/invokable surface minimal.
    virtual bool isDirty() const = 0;

public Q_SLOTS:
    /** Persist staged changes to the backing store. ApplicationController
     *  only invokes apply() on domains whose isDirty() returns true, so
     *  implementations may assume there is staged work to do. Must not
     *  rely on side effects ("stamp last-applied timestamp", "fire
     *  notification") that need to run even when clean. */
    virtual void apply() = 0;

    /** Drop staged changes; reload from the backing store.
     *  ApplicationController only invokes discard() on dirty domains —
     *  same side-effect-free contract as apply(). */
    virtual void discard() = 0;

    /** Load factory defaults into the staged area. Caller still needs apply()
     *  to persist. Default implementation is a no-op for domains that do not
     *  support resetting to defaults. */
    virtual void resetToDefaults();

Q_SIGNALS:
    void dirtyChanged();
    /** Async-completion signal — REQUIRED for use with
     *  ApplicationController::applyAllAsync. The async batch
     *  driver tracks per-domain completion through this signal:
     *  pending counter only ticks down on applyResult, so a
     *  domain that never emits parks the chrome's "Saving…"
     *  state until the 60 s batch timeout fires.
     *
     *  Synchronous domains MUST emit `applyResult(true, {})` at
     *  the end of apply() (and similarly discardResult in
     *  discard()). Async domains (D-Bus call, file I/O on a
     *  worker thread, etc.) emit when the out-of-band work
     *  finishes, with the success bool reflecting the daemon /
     *  worker outcome.
     *
     *  The bool return on apply() is too coarse for this shape —
     *  it pins success at function return but cannot signal a
     *  later daemon error or a partial-failure the user can
     *  retry. applyResult is what the chrome actually waits on.
     *
     *  @p ok      true on a clean apply / discard.
     *  @p error   user-readable error message (empty when ok). Caller
     *             owns translation context — emit translated text. */
    void applyResult(bool ok, const QString& error);
    /** Companion to applyResult for discard() — separate signal so the
     *  chrome can wire distinct user-visible states. Same emit
     *  requirements as applyResult: synchronous domains MUST emit
     *  `discardResult(true, {})` at end-of-discard(). */
    void discardResult(bool ok, const QString& error);
};

} // namespace PhosphorControl
