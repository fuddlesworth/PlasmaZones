// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorLayer/PhosphorLayer.h>

#include "mocks/mockscreenprovider.h"
#include "mocks/mocktransport.h"

#include <QDir>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QTextStream>
#include <QUrl>

using namespace PhosphorLayer;
using PhosphorLayer::Testing::MockScreenProvider;
using PhosphorLayer::Testing::MockTransport;

/**
 * @brief Exercises the async contentUrl loading path (QQmlComponent).
 *
 * The other Surface tests pin content via contentItem to keep the state
 * machine fully synchronous. This test writes a real .qml file to a temp
 * dir and uses contentUrl so QQmlComponent actually loads, parses, and
 * instantiates — covering the Warming → Hidden transition that only fires
 * on statusChanged from async QML.
 */
class TestContentUrl : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_tmpDir;

    QUrl writeQml(QStringView filename, QStringView body)
    {
        QFile f(m_tmpDir.filePath(filename.toString()));
        if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
            qFatal("Could not write temp QML: %s", qPrintable(f.errorString()));
        }
        QTextStream(&f) << body;
        return QUrl::fromLocalFile(f.fileName());
    }

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(m_tmpDir.isValid());
    }

    void validQmlTransitionsToShown()
    {
        const QUrl url = writeQml(u"valid.qml", u"import QtQuick 2.15\nItem { width: 100; height: 100 }\n");

        MockTransport t;
        MockScreenProvider s;
        SurfaceFactory f({&t, &s});
        SurfaceConfig cfg;
        cfg.role = Roles::CenteredModal;
        cfg.contentUrl = url;
        cfg.screen = s.primary();

        auto* surface = f.create(std::move(cfg));
        QVERIFY(surface);
        QSignalSpy spy(surface, &Surface::stateChanged);

        surface->show();
        // QQmlComponent loads local files synchronously by default, so we
        // typically reach Shown before this returns — but also accept an
        // event-loop pump if Qt defers (it does for `import` resolution).
        if (surface->state() != Surface::State::Shown) {
            QTRY_COMPARE_WITH_TIMEOUT(surface->state(), Surface::State::Shown, 2000);
        }
        QVERIFY(surface->window() != nullptr);
        QCOMPARE(t.m_attachCount, 1);
    }

    void invalidQmlSyntaxTransitionsToFailed()
    {
        const QUrl url = writeQml(u"broken.qml", u"this is not QML\n");

        MockTransport t;
        MockScreenProvider s;
        SurfaceFactory f({&t, &s});
        SurfaceConfig cfg;
        cfg.role = Roles::CenteredModal;
        cfg.contentUrl = url;
        cfg.screen = s.primary();
        cfg.debugName = QStringLiteral("broken-qml");

        auto* surface = f.create(std::move(cfg));
        QSignalSpy failSpy(surface, &Surface::failed);

        surface->show();
        if (surface->state() != Surface::State::Failed) {
            QTRY_COMPARE_WITH_TIMEOUT(surface->state(), Surface::State::Failed, 2000);
        }
        QCOMPARE(failSpy.count(), 1);
    }

    void rootObjectMustBeQuickItem()
    {
        // QtObject is a valid QML root but not a QQuickItem — Surface should
        // reject it as unsupported content.
        const QUrl url = writeQml(u"object.qml", u"import QtQml 2.15\nQtObject { property int x: 5 }\n");

        MockTransport t;
        MockScreenProvider s;
        SurfaceFactory f({&t, &s});
        SurfaceConfig cfg;
        cfg.role = Roles::CenteredModal;
        cfg.contentUrl = url;
        cfg.screen = s.primary();

        auto* surface = f.create(std::move(cfg));
        QSignalSpy failSpy(surface, &Surface::failed);

        surface->show();
        if (surface->state() != Surface::State::Failed) {
            QTRY_COMPARE_WITH_TIMEOUT(surface->state(), Surface::State::Failed, 2000);
        }
        QCOMPARE(failSpy.count(), 1);
        QVERIFY(failSpy.at(0).at(0).toString().contains(QStringLiteral("QQuickItem")));
    }

    void windowRootedQmlIsAdopted()
    {
        // Many consumers (PlasmaZones' overlays, panel apps, notification
        // daemons) ship QML where the ROOT type is a Window — they manage
        // size/visibility declaratively and expect the C++ side to adopt
        // the window rather than create its own wrapper. Verify PhosphorLayer
        // supports that pattern.
        const QUrl url = writeQml(u"window.qml",
                                  u"import QtQuick 2.15\nimport QtQuick.Window 2.15\n"
                                  u"Window { width: 200; height: 100; property string tag: 'window-root' }\n");

        MockTransport t;
        MockScreenProvider s;
        SurfaceFactory f({&t, &s});
        SurfaceConfig cfg;
        cfg.role = Roles::CenteredModal;
        cfg.contentUrl = url;
        cfg.screen = s.primary();
        cfg.debugName = QStringLiteral("window-root");

        auto* surface = f.create(std::move(cfg));
        surface->show();
        if (surface->state() != Surface::State::Shown) {
            QTRY_COMPARE_WITH_TIMEOUT(surface->state(), Surface::State::Shown, 2000);
        }
        auto* win = surface->window();
        QVERIFY(win);
        // The Surface's window IS the QML root (not a wrapper around it).
        QCOMPARE(win->property("tag").toString(), QStringLiteral("window-root"));
    }

    void windowPropertiesInjectIntoWindowRoot()
    {
        // createWithInitialProperties is used when windowProperties is non-empty;
        // for Window-rooted QML the properties land on the Window itself.
        const QUrl url = writeQml(u"window-init.qml",
                                  u"import QtQuick.Window 2.15\nWindow { property string injected: 'default' }\n");

        MockTransport t;
        MockScreenProvider s;
        SurfaceFactory f({&t, &s});
        SurfaceConfig cfg;
        cfg.role = Roles::CenteredModal;
        cfg.contentUrl = url;
        cfg.screen = s.primary();
        cfg.windowProperties = {{QStringLiteral("injected"), QStringLiteral("from-config")}};

        auto* surface = f.create(std::move(cfg));
        surface->show();
        if (surface->state() != Surface::State::Shown) {
            QTRY_COMPARE_WITH_TIMEOUT(surface->state(), Surface::State::Shown, 2000);
        }
        QCOMPARE(surface->window()->property("injected").toString(), QStringLiteral("from-config"));
    }

    void contextPropertiesAreVisibleToQml()
    {
        const QUrl url = writeQml(u"ctx.qml", u"import QtQuick 2.15\nItem { property string tag: injectedTag }\n");

        MockTransport t;
        MockScreenProvider s;
        SurfaceFactory f({&t, &s});
        SurfaceConfig cfg;
        cfg.role = Roles::CenteredModal;
        cfg.contentUrl = url;
        cfg.screen = s.primary();
        cfg.contextProperties = {{QStringLiteral("injectedTag"), QStringLiteral("hello-phosphorlayer")}};

        auto* surface = f.create(std::move(cfg));
        surface->show();
        if (surface->state() != Surface::State::Shown) {
            QTRY_COMPARE_WITH_TIMEOUT(surface->state(), Surface::State::Shown, 2000);
        }

        // Verify the QML actually saw the context property: dig into the
        // root item's "tag" property. Safer than reaching for engine->
        // rootContext() because that's been cleared by the time we query.
        auto* win = surface->window();
        QVERIFY(win);
        QVERIFY(win->contentItem());
        QQuickItem* root = nullptr;
        for (QObject* child : win->contentItem()->childItems()) {
            if (auto* qi = qobject_cast<QQuickItem*>(child)) {
                root = qi;
                break;
            }
        }
        QVERIFY(root);
        QCOMPARE(root->property("tag").toString(), QStringLiteral("hello-phosphorlayer"));
    }
};

QTEST_MAIN(TestContentUrl)
#include "test_content_url.moc"
