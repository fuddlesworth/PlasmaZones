// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "daemon/rendering/zoneshaderitem.h"

#include <PhosphorSurfaceQuick/SurfaceShaderItem.h>
#include <PhosphorRendering/ZoneLabelTexture.h>

#include <QColor>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QMetaProperty>
#include <QMetaType>
#include <QPointF>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QRegularExpression>
#include <QSet>
#include <QSignalSpy>
#include <QSizeF>
#include <QTest>
#include <QUrl>
#include <QVariantMap>
#include <QVector4D>
#include <qqml.h>

using PhosphorSurfaceQuick::SurfaceShaderItem;
using PlasmaZones::ZoneShaderItem;

namespace {

/// Strip QML comments with a quote-aware scan rather than a regular
/// expression. A `//` inside a string literal (a URL, a regex) would otherwise
/// truncate the rest of that line and hide whatever followed, which turns a
/// real violation into a silent PASS.
QString stripQmlComments(const QString& src)
{
    QString out;
    out.reserve(src.size());
    enum class State {
        Code,
        LineComment,
        BlockComment,
        DoubleQuote,
        SingleQuote,
        Template
    };
    State state = State::Code;
    for (int i = 0; i < src.size(); ++i) {
        const QChar c = src.at(i);
        const QChar next = (i + 1 < src.size()) ? src.at(i + 1) : QChar();
        switch (state) {
        case State::Code:
            if (c == QLatin1Char('/') && next == QLatin1Char('/')) {
                state = State::LineComment;
                ++i;
            } else if (c == QLatin1Char('/') && next == QLatin1Char('*')) {
                state = State::BlockComment;
                ++i;
            } else {
                if (c == QLatin1Char('"'))
                    state = State::DoubleQuote;
                else if (c == QLatin1Char('\''))
                    state = State::SingleQuote;
                else if (c == QLatin1Char('`'))
                    state = State::Template;
                out.append(c);
            }
            break;
        case State::LineComment:
            if (c == QLatin1Char('\n')) {
                state = State::Code;
                out.append(c);
            }
            break;
        case State::BlockComment:
            if (c == QLatin1Char('*') && next == QLatin1Char('/')) {
                state = State::Code;
                ++i;
            } else if (c == QLatin1Char('\n')) {
                // Keep line structure so failure messages stay locatable.
                out.append(c);
            }
            break;
        case State::DoubleQuote:
        case State::SingleQuote:
        case State::Template: {
            out.append(c);
            if (c == QLatin1Char('\\') && !next.isNull()) {
                out.append(next);
                ++i;
                break;
            }
            const QChar closer = state == State::DoubleQuote ? QLatin1Char('"')
                : state == State::SingleQuote                ? QLatin1Char('\'')
                                                             : QLatin1Char('`');
            if (c == closer)
                state = State::Code;
            break;
        }
        }
    }
    return out;
}

/// Every `Binding { ... }` block in @p src, as its exact source span.
///
/// Brace DEPTH, not a regular expression: `Binding\s*\{[^}]*\}` cannot cross a
/// nested brace, so a Binding holding an inline JS object or a nested element
/// evades it entirely and the guard passes on the very shape it exists to
/// catch.
/// The depth walk must be quote-aware for the same reason the comment stripper
/// is: a brace inside a string literal (`value: "}"`) would close the span
/// early and truncate it before the `property:` line, letting the violation
/// through silently. Braces are counted only in Code state.
QStringList bindingElementSpans(const QString& src)
{
    QStringList spans;
    static const QRegularExpression token(QStringLiteral("\\bBinding\\s*\\{"));
    QRegularExpressionMatchIterator it = token.globalMatch(src);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        const int open = src.indexOf(QLatin1Char('{'), m.capturedStart());
        if (open < 0) {
            continue;
        }
        int depth = 0;
        bool closed = false;
        QChar quote; // null while in Code state, else the open delimiter
        for (int i = open; i < src.size(); ++i) {
            const QChar c = src.at(i);
            if (!quote.isNull()) {
                if (c == QLatin1Char('\\')) {
                    ++i; // skip the escaped character
                } else if (c == quote) {
                    quote = QChar();
                }
                continue;
            }
            if (c == QLatin1Char('"') || c == QLatin1Char('\'') || c == QLatin1Char('`')) {
                quote = c;
            } else if (c == QLatin1Char('{')) {
                ++depth;
            } else if (c == QLatin1Char('}')) {
                if (--depth == 0) {
                    spans.append(src.mid(open, i - open + 1));
                    closed = true;
                    break;
                }
            }
        }
        // An unterminated Binding block must fail loudly rather than vanish
        // from the sweep: a silently skipped span is a false pass.
        if (!closed) {
            spans.append(src.mid(open));
        }
    }
    return spans;
}

/// The properties this sweep guards, read off the meta-objects rather than
/// hardcoded, so retyping a third property to QVariant enrols it automatically.
///
/// QVariant IS the selector: these properties are QVariant-typed precisely
/// because their real payload does not survive QML, so the setter takes the
/// variant and unwraps it.
///
/// audioSpectrum is the one deliberate exemption. Its payload (a QVariantList
/// or a QVector<float>) DOES survive a Binding element, and ZoneShaderRenderer
/// drives it through one on purpose, with a `when` guard that a direct
/// assignment cannot express.
QStringList guardedProperties()
{
    QSet<QString> names;
    for (const QMetaObject* mo : {&SurfaceShaderItem::staticMetaObject, &ZoneShaderItem::staticMetaObject}) {
        for (int i = 0; i < mo->propertyCount(); ++i) {
            const QMetaProperty prop = mo->property(i);
            if (prop.metaType().id() != QMetaType::QVariant)
                continue;
            const QString name = QString::fromUtf8(prop.name());
            if (name == QLatin1String("audioSpectrum"))
                continue;
            names.insert(name);
        }
    }
    QStringList sorted(names.constBegin(), names.constEnd());
    sorted.sort();
    return sorted;
}

/// Every shipping .qml under src/, so a new host cannot be added outside the
/// sweep's sight.
QStringList shippingQmlFiles()
{
    QStringList files;
    QDirIterator it(QStringLiteral(P_SOURCE_DIR "/src"), QStringList{QStringLiteral("*.qml")}, QDir::Files,
                    QDirIterator::Subdirectories);
    while (it.hasNext())
        files.append(it.next());
    files.sort();
    return files;
}

} // namespace

/**
 * @brief The QML shapes that deliver an image to a shader item, and the sweep
 *        that keeps the shipping QML on them.
 *
 * Both shader items (ZoneShaderItem here, SurfaceShaderItem from
 * phosphor-surface-quick) take an image through a QVariant-typed property, and
 * a QML Binding element hands that setter an invalid variant. The item-level
 * unit tests live beside each item; this test owns the cross-item contract:
 * the source sweep over every .qml under plasmazones/src, and the pinned
 * demonstrations of the shape that wipes a good payload.
 */
class TestShaderItemQmlBindings : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void initTestCase()
    {
        // Type registration is process-global and permanent, so doing it inside
        // a slot makes every later slot's type availability depend on execution
        // order. Once, up front, instead.
        qmlRegisterType<ZoneShaderItem>("PlasmaZonesTest", 1, 0, "ZoneShaderItem");
    }

    /// No shipping QML may drive a guarded property through a Binding ELEMENT,
    /// and at least one host must still drive it DIRECTLY.
    ///
    /// `Binding { property: "wallpaperTexture"; value: <a QImage> }` hands the
    /// setter an INVALID QVariant. The image does not survive Binding's own
    /// `value` property, and nothing is logged, so the surface silently draws
    /// its no-backdrop appearance. (`Binding on wallpaperTexture { ... }` is
    /// worse still: it never writes at all.) A direct assignment on the item
    /// delivers the payload intact, which is why this is the shape every host
    /// is pinned to.
    ///
    /// Two-sided on purpose. Proving only that the bad shape is absent passes
    /// for a host that stopped writing the property at all, which ships the
    /// same blank preview by a different route, so the presence of a direct
    /// assignment is asserted as well.
    void testShaderItems_noQmlDrivesAGuardedPropertyThroughABindingElement()
    {
        const QStringList guarded = guardedProperties();
        // The derivation must not quietly come back empty (a retype, a moc
        // change), because an empty set makes every assertion below vacuous.
        QVERIFY(guarded.contains(QStringLiteral("wallpaperTexture")));
        QVERIFY(guarded.contains(QStringLiteral("labelsTexture")));

        const QStringList files = shippingQmlFiles();
        QVERIFY2(files.size() > 10, qPrintable(QStringLiteral("swept %1 files").arg(files.size())));

        QSet<QString> directlyAssigned;
        for (const QString& path : files) {
            QFile f(path);
            QVERIFY2(f.open(QIODevice::ReadOnly | QIODevice::Text), qPrintable(path));
            // Comments first: several hosts DOCUMENT the broken shapes at
            // length, and a guard that matched the warning would fail on it.
            const QString src = stripQmlComments(QString::fromUtf8(f.readAll()));
            const QStringList bindings = bindingElementSpans(src);

            for (const QString& prop : guarded) {
                const QString where = QStringLiteral("%1 (%2)").arg(path, prop);

                // `Binding on <prop> { ... }` never writes at all.
                const QRegularExpression bindingOn(QStringLiteral("\\bBinding\\s+on\\s+%1\\b").arg(prop));
                QVERIFY2(!bindingOn.match(src).hasMatch(), qPrintable(where));

                // `Binding { ... property: "<prop>" ... }`, found by brace span
                // so a nested brace inside the block cannot hide it.
                const QRegularExpression propertyLine(QStringLiteral("\\bproperty\\s*:\\s*[\"']%1[\"']").arg(prop));
                for (const QString& span : bindings)
                    QVERIFY2(!propertyLine.match(span).hasMatch(), qPrintable(where));

                // A direct assignment: `<prop>: <expr>` on an item, or the
                // quoted form a createObject initial-property map uses (which
                // also delivers the payload intact). A `property var <prop>:`
                // DECLARATION is not an assignment to the guarded property and
                // does not count.
                const QRegularExpression direct(QStringLiteral("(?<![A-Za-z0-9_])[\"']?%1[\"']?\\s*:").arg(prop));
                QRegularExpressionMatchIterator hits = direct.globalMatch(src);
                while (hits.hasNext()) {
                    const QRegularExpressionMatch m = hits.next();
                    // Skip `property <type> <prop>:` in any spelling.
                    const int lineStart = src.lastIndexOf(QLatin1Char('\n'), m.capturedStart()) + 1;
                    const QString lead = src.mid(lineStart, m.capturedStart() - lineStart);
                    if (lead.contains(QLatin1String("property ")))
                        continue;
                    directlyAssigned.insert(prop);
                    break;
                }
            }
        }

        for (const QString& prop : guarded) {
            QVERIFY2(directlyAssigned.contains(prop),
                     qPrintable(QStringLiteral("no shipping QML assigns %1 directly any more").arg(prop)));
        }
    }

    /// A Binding element DESTROYS a good wallpaper, and that is why the source
    /// sweep above is the real guard.
    ///
    /// This is the shape that reached users: the settings previews drove
    /// `wallpaperTexture` from a Binding, the setter received an INVALID
    /// QVariant, and it resolved that to a null image. The good backdrop was
    /// not merely missed, it was wiped, and the pack fell back to its
    /// no-backdrop appearance.
    ///
    /// The setter cannot defend against this. An invalid QVariant is exactly
    /// what QML `undefined` and an absent key also deliver, so a host clearing
    /// its backdrop and a Binding firing are indistinguishable at the setter,
    /// and clearing is the right answer for the clear. Pinned here so the
    /// mechanism is asserted rather than only described in a comment, and so a
    /// future claim that "a Binding works now" has to change a red test.
    void testSurfaceShaderItem_aBindingElementWipesAGoodWallpaper()
    {
        QImage backdrop(8, 4, QImage::Format_RGBA8888);
        backdrop.fill(Qt::red);

        SurfaceShaderItem item;
        item.setWallpaperTexture(backdrop);
        QVERIFY(!item.wallpaperTexture().isNull());

        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("testItem"), &item);
        engine.rootContext()->setContextProperty(QStringLiteral("testBackdrop"), QVariant::fromValue(backdrop));

        QQmlComponent component(&engine);
        component.setData(R"QML(
import QtQuick
Item {
    Binding { target: testItem; property: "wallpaperTexture"; value: testBackdrop }
}
)QML",
                          QUrl(QStringLiteral("qrc:/test_wallpaper_binding_element.qml")));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        std::unique_ptr<QObject> root(component.create());
        QVERIFY(root);

        // The image does not survive Binding's own `value` property. Nothing is
        // logged, because an invalid variant is the ordinary no-backdrop state.
        QVERIFY(item.wallpaperTexture().isNull());
    }

    /// The same for the zone labels, which reach the item as a QImage from the
    /// settings and editor previews and rely on the registered converter. The
    /// converter never runs, because there is no image left to convert by the
    /// time the setter sees the value.
    void testZoneShaderItem_aBindingElementWipesGoodLabels()
    {
        QImage glyphs(16, 16, QImage::Format_ARGB32_Premultiplied);
        glyphs.fill(Qt::white);

        ZoneShaderItem item;
        item.setProperty("labelsTexture", QVariant::fromValue(glyphs));
        QVERIFY(!item.labelsTexture().isEmpty());

        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("testItem"), &item);
        engine.rootContext()->setContextProperty(QStringLiteral("testGlyphs"), QVariant::fromValue(glyphs));

        QQmlComponent component(&engine);
        component.setData(R"QML(
import QtQuick
Item {
    Binding { target: testItem; property: "labelsTexture"; value: testGlyphs }
}
)QML",
                          QUrl(QStringLiteral("qrc:/test_labels_binding_element.qml")));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        std::unique_ptr<QObject> root(component.create());
        QVERIFY(root);

        QVERIFY(item.labelsTexture().isEmpty());
    }

    /// The zone labels must survive the trip through QML too, from BOTH shapes
    /// the hosts produce: the daemon passes a ZoneLabelTexture payload, while
    /// the settings and editor shader previews pass a full QImage and rely on
    /// the QImage→ZoneLabelTexture converter registered in the item.
    ///
    /// The payload happens to survive a Binding element; a QImage does not, so
    /// the previews drew their zones with no numbers on them. Only the QImage
    /// case ever failed, so both are pinned here rather than the interesting
    /// one alone.
    void testZoneShaderItem_qmlDeliversLabelsAsAPayloadAndAsAnImage()
    {
        QImage glyphs(16, 16, QImage::Format_ARGB32_Premultiplied);
        glyphs.fill(Qt::white);
        const PhosphorRendering::ZoneLabelTexture payload = PhosphorRendering::ZoneLabelTexture::fromImage(glyphs);
        QVERIFY(!payload.isEmpty());

        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("testPayload"), QVariant::fromValue(payload));
        engine.rootContext()->setContextProperty(QStringLiteral("testImage"), QVariant::fromValue(glyphs));

        QQmlComponent component(&engine);
        component.setData(R"QML(
import QtQuick
import PlasmaZonesTest 1.0
Item {
    property alias fromPayload: a
    property alias fromImage: b
    ZoneShaderItem { id: a; labelsTexture: testPayload }
    ZoneShaderItem { id: b; labelsTexture: testImage }
}
)QML",
                          QUrl(QStringLiteral("qrc:/test_labels_binding.qml")));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        std::unique_ptr<QObject> root(component.create());
        QVERIFY(root);

        for (const char* which : {"fromPayload", "fromImage"}) {
            auto* item = root->property(which).value<ZoneShaderItem*>();
            QVERIFY2(item, which);
            QVERIFY2(!item->labelsTexture().isEmpty(), which);
            QCOMPARE(item->labelsTexture().size, glyphs.size());
        }
    }

    /// A host with no labels passes null, which means "no numbers" and must
    /// clear the payload rather than warn or leave the previous one up.
    void testZoneShaderItem_aNullValueClearsTheLabels()
    {
        QImage glyphs(4, 4, QImage::Format_ARGB32_Premultiplied);
        glyphs.fill(Qt::white);

        ZoneShaderItem item;
        item.setProperty("labelsTexture", QVariant::fromValue(glyphs));
        QVERIFY(!item.labelsTexture().isEmpty());

        item.setProperty("labelsTexture", QVariant());
        QVERIFY(item.labelsTexture().isEmpty());
    }
};

QTEST_MAIN(TestShaderItemQmlBindings)
#include "test_shader_item_qml_bindings.moc"
