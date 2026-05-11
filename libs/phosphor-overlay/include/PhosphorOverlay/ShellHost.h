// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

// ShellHost — owns the per-screen layer-shell shell-state map and the
// lifecycle bookkeeping (create / destroy / rekey / sync) that any
// multi-slot overlay consumer needs.
//
// Phase 2 foundation: the type holds a `QHash<QString, ShellState>`
// keyed by effective screen id plus a sticky creation-failure set
// (spam-guard for compositors that refuse a shell on a given screen).
// Method moves from OverlayService land in subsequent Phase 2 commits;
// this commit pins the shape of the public type and its accessors so
// the daemon can construct a ShellHost as a member alongside its
// existing per-screen state.

#include <PhosphorOverlay/ShellState.h>
#include <PhosphorOverlay/phosphoroverlay_export.h>

#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>

namespace PhosphorOverlay {

class PHOSPHOROVERLAY_EXPORT ShellHost : public QObject
{
    Q_OBJECT

public:
    explicit ShellHost(QObject* parent = nullptr);
    ~ShellHost() override;

    /// Read-write accessor. Returns the existing ShellState for
    /// @p screenId, default-constructing one in place if absent so
    /// callers can populate fields without an explicit insert.
    ShellState& stateFor(const QString& screenId);

    /// Read-only accessor. Returns nullptr if no state exists for
    /// @p screenId — callers that need to peek without materializing
    /// an empty entry should use this overload.
    const ShellState* stateFor(const QString& screenId) const;

    /// True if a ShellState exists (live or zeroed) for @p screenId.
    bool hasState(const QString& screenId) const;

    /// Remove the ShellState for @p screenId entirely. The library does
    /// NOT delete the shellSurface pointer — the caller's destroy
    /// pathway is expected to schedule that via deleteLater first.
    void removeState(const QString& screenId);

    /// Enumerate every screen id with a ShellState entry (in arbitrary
    /// QHash iteration order).
    QStringList screenIds() const;

    /// Sticky per-screen creation-failure flag. Once set, subsequent
    /// create attempts on @p screenId short-circuit until cleared
    /// (typically on screen hot-plug). Mirrors the legacy
    /// OverlayService::m_notificationCreationFailed semantics.
    void markFailure(const QString& screenId);
    void clearFailure(const QString& screenId);
    bool hasFailure(const QString& screenId) const;

private:
    QHash<QString, ShellState> m_states;
    QSet<QString> m_creationFailed;
};

} // namespace PhosphorOverlay
