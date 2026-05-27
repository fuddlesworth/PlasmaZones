// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <PhosphorSettingsUi/PageController.h>
#include <QObject>

namespace PlasmaZones {

/// Thin PhosphorSettingsUi::PageController wrapper that lets SettingsController
/// register its existing page controllers (which are plain QObjects, not
/// PageController subclasses) with the framework's PageRegistry without
/// changing each controller's inheritance.
///
/// The adapter is the framework-facing identity (id + lifecycle hooks); the
/// underlying delegate stays exposed via SettingsController's per-page
/// Q_PROPERTYs so existing QML bindings (`settingsController.generalPage.x`)
/// continue to work unchanged.
///
/// Per-page apply/discard semantics are deliberately no-ops here — dirty
/// tracking is centralised in SettingsController and orchestrated through
/// the single SettingsStagingDomain registered alongside these adapters.
///
/// Lifetime: `delegate` is either `nullptr` (the adapter wraps a "virtual"
/// page with no concrete controller — drill-down headers and leaves whose
/// QML binds directly to Settings) or a QObject expected to outlive the
/// adapter. The adapter itself is parented to `ApplicationController`
/// (which `SettingsController` owns via a `std::unique_ptr` declared after
/// the page sub-controllers); the delegate (when present) is a child of
/// `SettingsController`. Reverse-order destruction tears down
/// `ApplicationController` first → adapters die → `SettingsController` then
/// destroys delegate children. There is no path where a non-null delegate
/// disappears while the adapter is still alive. A raw pointer is therefore
/// safe AND matches the CONSTANT Q_PROPERTY contract; the earlier QPointer
/// would silently null out under contract violation without firing NOTIFY
/// (CONSTANT properties must not change).
class PageAdapter : public PhosphorSettingsUi::PageController
{
    Q_OBJECT
    Q_PROPERTY(QObject* delegate READ delegate CONSTANT)

public:
    explicit PageAdapter(QString id, QObject* delegate, QObject* parent = nullptr);
    ~PageAdapter() override;

    QObject* delegate() const;

    bool isDirty() const override;
    void apply() override;
    void discard() override;

private:
    QObject* m_delegate = nullptr;
};

} // namespace PlasmaZones
