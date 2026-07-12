// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "shadersetstore.h"

#include <QString>
#include <QVariantMap>

#include <functional>

namespace PlasmaZones::motionset {

/// Build the motion-set domain configuration for a ShaderSetStore.
///
/// Motion sets snapshot the per-event override FILES in the profiles
/// directory (one JSON per event path). The domain never reaches across the
/// controller boundary itself: writes go through @p writeOverride (wired to
/// `AnimationsPageController::setOverride`, preserving snapshot / pending
/// semantics) and pre-write captures go through @p fileSnapshot.
///
/// @param profilesDir     Absolute path of the per-event override directory.
/// @param setsDir         Absolute path of the motion-sets directory.
/// @param writeOverride   Commits one entry as a per-path override file.
/// @param fileSnapshot    Captures a file's pre-edit content for Discard.
///                        False = the capture failed, and the store then
///                        refuses the write rather than losing the content.
/// @param mutationGuard   Empty when writes are allowed, else the refusal
///                        reason (the controller blocks writes mid-discard).
ShaderSetStore::Config
makeConfig(std::function<QString()> profilesDir, std::function<QString()> setsDir,
           std::function<bool(const QString& /*path*/, const QVariantMap& /*profile*/)> writeOverride,
           std::function<bool(const QString& /*filePath*/)> fileSnapshot, std::function<QString()> mutationGuard);

} // namespace PlasmaZones::motionset
