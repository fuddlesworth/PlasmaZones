// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

/**
 * @file PhosphorServiceMpris.h
 * @brief Umbrella header for the PhosphorServiceMpris library.
 *
 * Exposes MPRIS2 (`org.mpris.MediaPlayer2.*`) media-player discovery,
 * metadata, and transport controls as Qt/QML-friendly types
 * (`MprisHost`, `MprisPlayer`, `MprisPlayerModel`). No UI; the shell
 * decides how a now-playing card, transport bar, or pop-out media
 * widget is rendered.
 *
 * Extracted from the legacy `phosphor-services` umbrella as part of
 * the Phase 2.0 split documented in
 * `docs/phosphor-shell-design/04-implementation-plan.md`.
 */

#include <PhosphorServiceMpris/MprisHost.h>
#include <PhosphorServiceMpris/MprisPlayer.h>
#include <PhosphorServiceMpris/MprisPlayerModel.h>
#include <PhosphorServiceMpris/QmlRegistration.h>
