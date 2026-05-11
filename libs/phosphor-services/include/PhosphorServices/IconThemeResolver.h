// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorServices/phosphorservices_export.h>

#include <QHash>
#include <QImage>
#include <QObject>
#include <QString>
#include <QStringList>

namespace PhosphorServices {

/// XDG Icon Theme Specification 0.13 resolver.
/// https://specifications.freedesktop.org/icon-theme-spec/latest/
///
/// Singleton because the parsed theme index is expensive to build
/// (walking /usr/share/icons/* parses dozens of index.theme files)
/// and the same theme tree is reused across every tray item.
/// `iconForName()` returns the best-match QImage by walking the
/// active theme + inherited parents + Hicolor fallback, using the
/// spec's distance algorithm to pick a size when no exact match
/// exists.
class PHOSPHORSERVICES_EXPORT IconThemeResolver : public QObject
{
    Q_OBJECT
public:
    /// Process-wide singleton. Created lazily on first call.
    static IconThemeResolver* instance();

    /// Force a theme override. Empty string => fall back to detection
    /// from Qt's icon theme name + GTK env hints.
    void setThemeName(const QString& themeName);
    [[nodiscard]] QString themeName() const;

    /// Look up an icon by name. `extraThemeDir` is the SNI item's
    /// IconThemePath (if any) — when set, it's prepended to the
    /// search path so an app's custom theme wins over the system
    /// one. `size` is the desired logical-pixel size; `scale` is
    /// the device pixel ratio (1 for traditional, 2 for HiDPI).
    /// Returns an empty QImage when no match exists.
    [[nodiscard]] QImage iconForName(const QString& name, int size, int scale = 1,
                                     const QString& extraThemeDir = {}) const;

    /// Bypass theme lookup and decode a raw IconPixmap byte stream
    /// straight off the wire (ARGB32 in network byte order). Picks
    /// the pixmap closest to `size` from the list and converts.
    [[nodiscard]] static QImage decodePixmaps(const QList<QPair<QSize, QByteArray>>& pixmaps, int size);

Q_SIGNALS:
    void themeChanged();

private:
    explicit IconThemeResolver(QObject* parent = nullptr);
    ~IconThemeResolver() override;

    class Private;
    Private* const d;
};

} // namespace PhosphorServices
