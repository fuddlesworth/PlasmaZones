// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// QmlComponentTileFactory, which had no test at all despite being pure
// engine-testable logic sitting on a contract the control-center surface
// depends on.
//
// The ownership hand-off is the case that matters. The surface destroys
// every tile it materialised on rebuild, and destroy() on a
// CppOwnership object throws and leaks the old tile behind the new one, so
// the factory MUST mark what it returns JavaScriptOwnership. The mirror
// image of that rule, a Q_INVOKABLE handing QML a parentless QObject it
// then garbage-collects, took the shell down five times, which is how much
// a wrong answer here costs.
//
// The type it builds is a trivial one registered into a test-local URI, so
// this needs neither the Phosphor.ControlCenter module nor any service.

#include "shell/QmlComponentTileFactory.h"

#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QRegularExpression>
#include <QScopedPointer>
#include <QTest>
#include <QVariantMap>
#include <qqml.h>

using PhosphorShellApp::QmlComponentTileFactory;

namespace {

// The tile the factory builds. A plain item with one settable property, so
// the initial-properties path has something to land on.
class FakeTile : public QQuickItem
{
    Q_OBJECT
    Q_PROPERTY(QString label READ label WRITE setLabel NOTIFY labelChanged)

public:
    QString label() const
    {
        return m_label;
    }
    void setLabel(const QString& label)
    {
        if (m_label == label) {
            return;
        }
        m_label = label;
        Q_EMIT labelChanged();
    }

Q_SIGNALS:
    void labelChanged();

private:
    QString m_label;
};

constexpr auto kTestUri = "Phosphor.Test.Tiles";

} // namespace

class TestQmlComponentTileFactory : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void metadataIsWhatItWasBuiltWith();
    void aNullEngineIsRefused();
    void anUnknownTypeIsRefused();
    void theTileIsHandedToTheJavaScriptGarbageCollector();
    void initialPropertiesReachTheTile();

private:
    QQmlEngine m_engine;
    QQuickItem m_parent;
};

void TestQmlComponentTileFactory::initTestCase()
{
    qmlRegisterType<FakeTile>(kTestUri, 1, 0, "FakeTile");
}

void TestQmlComponentTileFactory::metadataIsWhatItWasBuiltWith()
{
    const QmlComponentTileFactory factory(QStringLiteral("network"), QStringLiteral("Wi-Fi"),
                                          QString::fromLatin1(kTestUri), QStringLiteral("FakeTile"), {},
                                          {QStringLiteral("network.write")});
    QCOMPARE(factory.id(), QStringLiteral("network"));
    QCOMPARE(factory.displayName(), QStringLiteral("Wi-Fi"));
    QCOMPARE(factory.capabilities(), QStringList{QStringLiteral("network.write")});
}

void TestQmlComponentTileFactory::aNullEngineIsRefused()
{
    QmlComponentTileFactory factory(QStringLiteral("t"), QStringLiteral("T"), QString::fromLatin1(kTestUri),
                                    QStringLiteral("FakeTile"), {}, {});
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("null engine")));
    QVERIFY(!factory.createTile(nullptr, &m_parent));
}

void TestQmlComponentTileFactory::anUnknownTypeIsRefused()
{
    QmlComponentTileFactory factory(QStringLiteral("t"), QStringLiteral("T"), QString::fromLatin1(kTestUri),
                                    QStringLiteral("NoSuchTile"), {}, {});
    // A type the module does not export. The factory reports and returns
    // null, which the surface reads as "unavailable here" rather than as an
    // error, so a mistyped type name must not take the whole grid down.
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("component (error|not ready)")));
    QVERIFY(!factory.createTile(&m_engine, &m_parent));
}

void TestQmlComponentTileFactory::theTileIsHandedToTheJavaScriptGarbageCollector()
{
    QmlComponentTileFactory factory(QStringLiteral("t"), QStringLiteral("T"), QString::fromLatin1(kTestUri),
                                    QStringLiteral("FakeTile"), {}, {});
    QQuickItem* tile = factory.createTile(&m_engine, &m_parent);
    QVERIFY(tile);

    // THE contract. The control-center surface calls destroy() on every tile
    // it built when it rebuilds, and destroy() on a CppOwnership object
    // throws "indestructible object" and leaks the old tile into the grid
    // behind its replacement. A QObject-parented item defaults to
    // CppOwnership, so the factory has to hand it over explicitly.
    QCOMPARE(QQmlEngine::objectOwnership(tile), QQmlEngine::JavaScriptOwnership);
    QCOMPARE(tile->parentItem(), &m_parent);
}

void TestQmlComponentTileFactory::initialPropertiesReachTheTile()
{
    QVariantMap initial{{QStringLiteral("label"), QStringLiteral("Volume")}};
    QmlComponentTileFactory factory(QStringLiteral("t"), QStringLiteral("T"), QString::fromLatin1(kTestUri),
                                    QStringLiteral("FakeTile"), initial, {});
    QQuickItem* tile = factory.createTile(&m_engine, &m_parent);
    QVERIFY(tile);
    // The idle tile is registered with its service in this map, so a
    // silently dropped initial property is a tile bound to nothing.
    QCOMPARE(tile->property("label").toString(), QStringLiteral("Volume"));
}

QTEST_MAIN(TestQmlComponentTileFactory)
#include "test_qml_component_tile_factory.moc"
