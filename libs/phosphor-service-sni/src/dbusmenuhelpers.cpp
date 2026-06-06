// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "dbusmenuhelpers.h"

#include <PhosphorServiceIconTheme/IconThemeResolver.h>

#include "dbustypes.h"

#include <QBuffer>
#include <QByteArray>
#include <QDBusArgument>
#include <QDBusVariant>
#include <QDateTime>
#include <QHash>
#include <QImageReader>
#include <QIODevice>
#include <QLatin1Char>
#include <QStringLiteral>

namespace PhosphorServiceSni {

uint dbusmenuTimestamp()
{
    return uint(QDateTime::currentMSecsSinceEpoch() & 0xFFFFFFFFu);
}

DBusMenuLayoutItem unpackLayoutVariant(const QVariant& v)
{
    DBusMenuLayoutItem out;
    auto arg = v.value<QDBusArgument>();
    if (arg.currentType() == QDBusArgument::StructureType) {
        arg >> out;
        return out;
    }
    // Some apps send the layout as a Variant<(ia{sv}av)> instead of
    // a straight struct. Unwrap one level, but ONLY after verifying
    // the unwrapped argument is itself a struct. A malicious or
    // buggy sender can deliver a variant wrapping anything else (an
    // integer, an array, an empty argument), and `arg >> out`
    // against a non-struct argument is undefined per QtDBus's
    // assertion semantics.
    if (!v.canConvert<QDBusVariant>()) {
        return out;
    }
    const QDBusVariant dv = v.value<QDBusVariant>();
    auto inner = dv.variant().value<QDBusArgument>();
    if (inner.currentType() != QDBusArgument::StructureType) {
        return out;
    }
    inner >> out;
    return out;
}

QString labelFromProps(const QVariantMap& props)
{
    auto raw = props.value(QStringLiteral("label")).toString();
    QString out;
    out.reserve(raw.size());
    for (int i = 0; i < raw.size(); ++i) {
        const QChar c = raw[i];
        if (c == QLatin1Char('&') || c == QLatin1Char('_')) {
            // Doubled = literal character; keep one copy and skip
            // the duplicate.
            if (i + 1 < raw.size() && raw[i + 1] == c) {
                out.append(c);
                ++i;
                continue;
            }
            // Lone marker at end-of-string (e.g. an app that ships
            // a single `&` as the label) is malformed but should
            // not silently disappear; keep the char so the label
            // survives.
            if (i + 1 >= raw.size()) {
                out.append(c);
                continue;
            }
            // Lone marker mid-string = strip and move on; the NEXT
            // char is the mnemonic but we don't render an underline.
            continue;
        }
        out.append(c);
    }
    return out;
}

QString shortcutFromProps(const QVariantMap& props)
{
    const QVariant raw = props.value(QStringLiteral("shortcut"));
    if (!raw.isValid()) {
        return {};
    }
    // Use qdbus_cast on the variant rather than manual
    // QDBusArgument iteration: hand-rolling beginArray/endArray on
    // a QDBusArgument obtained via `raw.value<QDBusArgument>()`
    // triggers Qt 6's "write from a read-only object" diagnostic
    // (the returned arg's internal state machine flips to read-
    // only and beginArray() is overloaded as a write operation
    // when no metatype id is supplied). qdbus_cast knows about
    // both QList<T> and QStringList natively, so a single cast
    // does the full demarshalling without that wrinkle.
    const QList<QStringList> chords = qdbus_cast<QList<QStringList>>(raw);
    if (chords.isEmpty() || chords.first().isEmpty()) {
        return {};
    }

    static const QHash<QString, QString> modifierDisplay{
        {QStringLiteral("Control"), QStringLiteral("Ctrl")},
        {QStringLiteral("Shift"), QStringLiteral("Shift")},
        {QStringLiteral("Alt"), QStringLiteral("Alt")},
        {QStringLiteral("Super"), QStringLiteral("Super")},
    };

    QStringList rendered;
    for (const auto& part : chords.first()) {
        rendered.append(modifierDisplay.value(part, part));
    }
    return rendered.join(QLatin1Char('+'));
}

QString iconToDataUrl(const QImage& img)
{
    if (img.isNull()) {
        return {};
    }
    QByteArray buf;
    QBuffer dev(&buf);
    dev.open(QIODevice::WriteOnly);
    img.save(&dev, "PNG");
    return QStringLiteral("data:image/png;base64,") + QString::fromLatin1(buf.toBase64());
}

QImage iconFromProps(const QVariantMap& props, int size, const QStringList& themePaths)
{
    const auto iconName = props.value(QStringLiteral("icon-name")).toString();
    if (!iconName.isEmpty()) {
        // dbusmenu has its OWN IconThemePath list (plural, different
        // from SNI's singular). Try each.
        for (const auto& path : themePaths) {
            auto img = PhosphorServiceIconTheme::IconThemeResolver::instance()->iconForName(iconName, size, 1, path);
            if (!img.isNull())
                return img;
        }
        return PhosphorServiceIconTheme::IconThemeResolver::instance()->iconForName(iconName, size, 1, {});
    }
    const auto iconData = props.value(QStringLiteral("icon-data")).toByteArray();
    if (!iconData.isEmpty()) {
        QBuffer buf;
        buf.setData(iconData);
        buf.open(QIODevice::ReadOnly);
        QImageReader reader(&buf);
        reader.setAllocationLimit(4); // MiB
        constexpr int kMaxIconDim = 4096;
        const QSize wireSize = reader.size();
        if (wireSize.isValid() && (wireSize.width() > kMaxIconDim || wireSize.height() > kMaxIconDim))
            return {};
        QImage img;
        if (!reader.read(&img))
            return {};
        return img;
    }
    return {};
}

} // namespace PhosphorServiceSni
