// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

#include "phosphorsettingsui_export.h"

namespace PhosphorSettingsUi {

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
class PHOSPHORSETTINGSUI_EXPORT StagingDomain : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool dirty READ isDirty NOTIFY dirtyChanged)
    QML_NAMED_ELEMENT(StagingDomain)
    QML_UNCREATABLE("StagingDomain is an abstract base; subclass in C++.")

public:
    explicit StagingDomain(QObject* parent = nullptr);
    ~StagingDomain() override;

    // Q_INVOKABLE so QML can read isDirty() imperatively; apply/discard
    // below are slots, so QML can call all three through the same shape.
    Q_INVOKABLE virtual bool isDirty() const = 0;

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
    /** Optional async-completion signal. Domains that perform their
     *  apply() / discard() out-of-band (D-Bus call, file I/O on a
     *  worker thread, etc.) should emit this when the operation
     *  finishes so the chrome can surface in-flight + failure state
     *  to the user. The bool return on apply() is too coarse for that
     *  shape — it pins success at function return but cannot signal a
     *  later daemon error or a partial-failure that the user can
     *  retry.
     *
     *  Domains whose apply() is fully synchronous don't need to emit
     *  this — the existing dirtyChanged transition tells the chrome
     *  the work is done. Until the first consumer emits it, the
     *  signal is intentionally inert.
     *
     *  @p ok      true on a clean apply / discard.
     *  @p error   user-readable error message (empty when ok). Caller
     *             owns translation context — emit translated text. */
    void applyResult(bool ok, const QString& error);
    /** Companion to applyResult for discard() — separate signal so the
     *  chrome can wire distinct user-visible states. */
    void discardResult(bool ok, const QString& error);
};

} // namespace PhosphorSettingsUi
