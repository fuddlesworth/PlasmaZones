// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <PhosphorScreens/ScreenInfo.h>
#include <PhosphorZones/AssignmentEntry.h>
#include <QList>
#include <QString>
#include <QVariantList>
#include <functional>

class QObject;

namespace PlasmaZones {

class ISettings;

/// Re-export of the lib's POD so PlasmaZones-internal callers don't need the
/// `PhosphorScreens::` prefix. Single update-site for any future renames.
// TODO(post-merge): drop this alias once the phosphor-settings-ui lift-and-
// shift settles. Keeping it for now buys migration ergonomics — call sites
// can adopt the lib-qualified spelling incrementally rather than in lockstep
// with this header's churn.
using ScreenInfo = PhosphorScreens::ScreenInfo;

/**
 * @brief Fetch the list of connected screens via D-Bus (daemon) with Qt fallback
 *
 * Each ScreenInfo contains connector name, primary flag, manufacturer, model,
 * resolution, and the stable EDID-based screenId from the daemon. This call
 * stays in PlasmaZones because it speaks the PlasmaZones daemon's specific
 * D-Bus protocol; the generic ScreenInfo POD itself lives in phosphor-screens.
 *
 * @param daemonUnavailable  Optional out-pointer. When non-null, set to true
 *                           if the daemon path failed (empty getScreens reply
 *                           or every per-screen getScreenInfo() probe errored)
 *                           and the Qt-fallback branch ran. Settings UIs can
 *                           use this to render a degraded-fallback banner
 *                           without re-doing the D-Bus probing.
 */
QList<ScreenInfo> fetchScreens(bool* daemonUnavailable = nullptr);

/**
 * @brief Check whether a given monitor is disabled in settings for the given mode
 * @param settings The ISettings instance to query
 * @param mode The mode whose disable list to check
 * @param screenName The connector name of the screen
 */
bool isMonitorDisabledFor(const ISettings* settings, PhosphorZones::AssignmentEntry::Mode mode,
                          const QString& screenName);

/**
 * @brief Enable or disable a monitor in settings for the given mode
 * @param settings The ISettings instance to modify
 * @param mode The mode whose disable list to modify
 * @param screenName The connector name of the screen
 * @param disabled Whether to disable (true) or enable (false)
 * @param onChanged Callback invoked when the disabled list actually changes
 * @return true on success, false if the screen name could not be resolved
 *         (settings is null, screenName is empty, or the connector name
 *         couldn't be canonicalised to a screen id). QML toggle handlers
 *         can use this to revert visual state when the underlying write
 *         couldn't be performed.
 */
bool setMonitorDisabledFor(ISettings* settings, PhosphorZones::AssignmentEntry::Mode mode, const QString& screenName,
                           bool disabled, const std::function<void()>& onChanged);

/**
 * @brief Connect D-Bus screen change signals to a receiver's refreshScreens() slot.
 *
 * Call this in KCM constructors that need screen change tracking. Returns
 * true if BOTH subscriptions succeeded; on partial failure the receiver
 * may miss screenAdded/screenRemoved broadcasts. The receiver MUST have a
 * `refreshScreens()` slot declared in its meta-object (Q_SLOTS).
 */
bool connectScreenChangeSignals(QObject* receiver);

} // namespace PlasmaZones
