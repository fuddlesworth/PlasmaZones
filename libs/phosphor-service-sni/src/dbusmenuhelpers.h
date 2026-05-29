// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include "dbustypes.h"

#include <QImage>
#include <QString>
#include <QStringList>
#include <QVariantMap>

namespace PhosphorServiceSni {

/// dbusmenu spec timestamps are unix-epoch milliseconds, truncated to
/// 32 bits. Centralised so the four Event() call sites can't drift
/// (an earlier rev divided by 1000 and produced "stale" rejections
/// on apps with focus-stealing prevention because the timestamps
/// didn't match the platform's input-event clock).
uint dbusmenuTimestamp();

/// Visit each level-1 child variant under a layout struct and unpack
/// it into a DBusMenuLayoutItem. The dbus marshalling already gave
/// us flat (id, props, children-variants); we just need to recurse
/// one level for the rendered list-model row data and lazily for
/// submenus.
DBusMenuLayoutItem unpackLayoutVariant(const QVariant& v);

/// Strip dbusmenu accelerator markers from the `label` property.
/// GTK style uses `_` before the accel char (`__` literal); Qt style
/// uses `&` before the accel char (`&&` literal). We strip both
/// because we don't render underline-on-Alt either way.
QString labelFromProps(const QVariantMap& props);

/// Parse the `shortcut` property: dbusmenu types it as `aas` (array
/// of array of strings), where each outer entry is an alternative
/// key-press and each inner entry is a modifier list terminating in
/// the key. We render only the first chord.
QString shortcutFromProps(const QVariantMap& props);

/// Encode a QImage as a data:image/png;base64 URL. Used for menu
/// icons because they're per-row, short-lived, and small (typically
/// 16x16). Empty input returns an empty string so the QML side can
/// `visible: src.length`.
QString iconToDataUrl(const QImage& img);

/// Resolve a menu row icon. `icon-name` (themed lookup) wins over
/// `icon-data` (PNG bytes) since it's smaller on the wire. The
/// inline-data path decodes through QImageReader with a hard 4 MiB
/// alloc cap and a 4096px dimension cap to bound damage from a
/// hostile menu provider.
QImage iconFromProps(const QVariantMap& props, int size, const QStringList& themePaths);

} // namespace PhosphorServiceSni
