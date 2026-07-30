// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_scrolling_adaptor.cpp
 * @brief Behaviour of ScrollingAdaptor, the org.plasmazones.Scrolling surface.
 *
 * The contract-sync test covers the XML's SHAPE only — that the declared
 * methods and signals exist with the right signatures. What it cannot check
 * is whether they do what their DocStrings promise, which is what this file
 * pins:
 *
 *  1. focusColumn and visibleStripJson both refuse a screen the engine does
 *     not own, so a wheel event or a preview query aimed at a non-scrolling
 *     monitor is never redirected onto the active scrolling one.
 *  2. visibleStripJson emits the rects the XML documents, INCLUDING
 *     zoneNumber, and normalizes them to 0..1 per axis.
 *  3. scrollingScreensChanged is gated on an actual change: the engine
 *     re-announces an identical set on every desktop switch, and a wire
 *     consumer comparing successive payloads must not see a phantom one.
 *  4. clearEngine leaves every slot answering safely.
 */

#include <QTest>
#include <QSignalSpy>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <PhosphorEngine/PlacementEngineBase.h>
#include <PhosphorScrollEngine/ScrollEngine.h>

#include "dbus/scrollingadaptor/scrollingadaptor.h"

using namespace PlasmaZones;
using PhosphorScrollEngine::ScrollEngine;

class TestScrollingAdaptor : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init()
    {
        m_engine = new ScrollEngine(nullptr, nullptr, nullptr);
        // Headless geometry seam: without a work area the strip resolves no
        // rects at all, and visibleStripJson would return "[]" for every
        // screen — including the one it is supposed to describe.
        const auto geometry = [](const QString&) {
            return QRect(0, 0, 1200, 800);
        };
        m_engine->setScreenGeometryProviders(geometry, geometry);
        m_parent = new QObject(nullptr);
        m_adaptor = new ScrollingAdaptor(m_engine, m_parent);
        m_engine->setActiveScreens({QStringLiteral("DP-1")});
    }

    void cleanup()
    {
        m_adaptor->clearEngine();
        delete m_parent;
        m_parent = nullptr;
        m_adaptor = nullptr;
        delete m_engine;
        m_engine = nullptr;
    }

    // The XML promises an empty array for a screen with no strip or one that
    // is not scrolling. A screen the engine does not own must take the second
    // branch rather than describing the active screen's strip.
    void testVisibleStripJson_refusesForeignScreen()
    {
        m_engine->windowOpened(QStringLiteral("app|a"), QStringLiteral("DP-1"), 0, 0);

        QCOMPARE(m_adaptor->visibleStripJson(QStringLiteral("HDMI-2")), QStringLiteral("[]"));
        QCOMPARE(m_adaptor->visibleStripJson(QString()), QStringLiteral("[]"));

        // Positive control: the screen the engine DOES own describes a strip,
        // so the two refusals above are not just "this fixture has no rects".
        QVERIFY(m_adaptor->visibleStripJson(QStringLiteral("DP-1")) != QStringLiteral("[]"));
    }

    // After the screen leaves scrolling mode the preview goes empty. Two
    // things independently produce that — the engine releases the screen's
    // state, and the adaptor's isActiveOnScreen gate — so this pins the
    // OUTCOME the settings app depends on, not either mechanism. Removing
    // just the gate would not fail this, and that is accurate: the gate is
    // documented as belt and braces at its own call site.
    void testVisibleStripJson_emptyAfterModeExit()
    {
        m_engine->windowOpened(QStringLiteral("app|a"), QStringLiteral("DP-1"), 0, 0);
        m_engine->windowOpened(QStringLiteral("app|b"), QStringLiteral("DP-1"), 0, 0);
        QVERIFY(m_adaptor->visibleStripJson(QStringLiteral("DP-1")) != QStringLiteral("[]"));

        m_engine->setActiveScreens({});
        QVERIFY(!m_engine->isActiveOnScreen(QStringLiteral("DP-1")));
        QCOMPARE(m_adaptor->visibleStripJson(QStringLiteral("DP-1")), QStringLiteral("[]"));
    }

    // The documented payload: normalized rects, each carrying the 1-based
    // visible column slot. zoneNumber is the field a consumer needs to map a
    // rect back to a scroll zone, and it is the one the DocString used to
    // omit.
    void testVisibleStripJson_shapeAndZoneNumbers()
    {
        m_engine->windowOpened(QStringLiteral("app|a"), QStringLiteral("DP-1"), 0, 0);
        m_engine->windowOpened(QStringLiteral("app|b"), QStringLiteral("DP-1"), 0, 0);

        const QJsonDocument doc = QJsonDocument::fromJson(m_adaptor->visibleStripJson(QStringLiteral("DP-1")).toUtf8());
        QVERIFY(doc.isArray());
        const QJsonArray arr = doc.array();
        QVERIFY(!arr.isEmpty());

        int expectedOrdinal = 1;
        for (const QJsonValue& value : arr) {
            QVERIFY(value.isObject());
            const QJsonObject obj = value.toObject();
            for (const QLatin1String key :
                 {QLatin1String("x"), QLatin1String("y"), QLatin1String("width"), QLatin1String("height")}) {
                QVERIFY2(obj.contains(key), key.data());
                const double v = obj.value(key).toDouble(-1.0);
                QVERIFY(v >= 0.0 && v <= 1.0);
            }
            QVERIFY(obj.contains(QLatin1String("zoneNumber")));
            // Ordinals are the VISIBLE slot, so they run 1, 2, … in payload
            // order regardless of how many columns are parked off screen.
            QCOMPARE(obj.value(QLatin1String("zoneNumber")).toInt(), expectedOrdinal);
            ++expectedOrdinal;
        }
    }

    // focusColumn's own doc: gated on the engine owning the screen, because
    // the engine's screen fallback would otherwise redirect a wheel event
    // from a non-scrolling monitor onto the active scrolling one.
    void testFocusColumn_ignoresForeignScreenAndBadDelta()
    {
        m_engine->windowOpened(QStringLiteral("app|a"), QStringLiteral("DP-1"), 0, 0);
        m_engine->windowOpened(QStringLiteral("app|b"), QStringLiteral("DP-1"), 0, 0);

        QSignalSpy activateSpy(m_engine, &PhosphorEngine::PlacementEngineBase::activateWindowRequested);

        m_adaptor->focusColumn(QStringLiteral("HDMI-2"), -1); // not ours
        m_adaptor->focusColumn(QString(), -1); // no screen at all
        m_adaptor->focusColumn(QStringLiteral("DP-1"), 0); // not a direction
        m_adaptor->focusColumn(QStringLiteral("DP-1"), 2); // not a direction either
        QCOMPARE(activateSpy.count(), 0);

        // Positive control: the same call with a real screen and a real
        // direction DOES move focus, so the refusals above discriminate.
        m_adaptor->focusColumn(QStringLiteral("DP-1"), -1);
        QCOMPARE(activateSpy.count(), 1);
    }

    // The engine re-emits an unchanged screen set on every desktop switch for
    // the tiling channel's benefit. This adaptor must not relay those.
    void testScreensChanged_suppressesUnchangedSets()
    {
        QSignalSpy spy(m_adaptor, &ScrollingAdaptor::scrollingScreensChanged);

        m_engine->setActiveScreens({QStringLiteral("DP-1"), QStringLiteral("DP-2")});
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().at(0).toStringList(), (QStringList{QStringLiteral("DP-1"), QStringLiteral("DP-2")}));

        // Same set again, and again in a different order: neither is a change.
        m_engine->setActiveScreens({QStringLiteral("DP-1"), QStringLiteral("DP-2")});
        m_engine->setActiveScreens({QStringLiteral("DP-2"), QStringLiteral("DP-1")});
        QCOMPARE(spy.count(), 1);

        // A genuine change does get through.
        m_engine->setActiveScreens({QStringLiteral("DP-1")});
        QCOMPARE(spy.count(), 2);
        QCOMPARE(spy.at(1).at(0).toStringList(), (QStringList{QStringLiteral("DP-1")}));
    }

    // Shutdown contract shared with the sibling adaptors: a late D-Bus call
    // after clearEngine answers safely instead of dereferencing the engine.
    void testClearedEngine_slotsAnswerSafely()
    {
        m_engine->windowOpened(QStringLiteral("app|a"), QStringLiteral("DP-1"), 0, 0);
        m_adaptor->clearEngine();

        QCOMPARE(m_adaptor->visibleStripJson(QStringLiteral("DP-1")), QStringLiteral("[]"));
        QVERIFY(m_adaptor->scrollingScreens().isEmpty());
        m_adaptor->focusColumn(QStringLiteral("DP-1"), -1); // must not crash
    }

private:
    ScrollEngine* m_engine = nullptr;
    QObject* m_parent = nullptr;
    ScrollingAdaptor* m_adaptor = nullptr;
};

QTEST_MAIN(TestScrollingAdaptor)
#include "test_scrolling_adaptor.moc"
