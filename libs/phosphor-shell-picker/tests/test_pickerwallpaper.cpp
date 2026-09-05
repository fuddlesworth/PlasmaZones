// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// The picker's wallpaper half, against a fake service with
// WallpaperService's method names: hovering a candidate previews it on
// the strip's own output, leaving clears the preview, Apply persists and
// then clears (in that order, so the surface never resolves to anything
// but the path it is already showing). And WallpaperSurface itself: it
// follows the fake's effective path for its screen, crossfading only
// once the incoming image is Ready and dropping a request that fails.

#include <PhosphorShell/PanelWindow.h>

#include <QDir>
#include <QGuiApplication>
#include <QHash>
#include <QImage>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QScreen>
#include <QSignalSpy>
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>
#include <QVariantMap>

#include <memory>

// WallpaperService's surface as the picker and the wallpaper surface see
// it, with the calls recorded in order.
class FakeWallpaperService : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString path READ path NOTIFY pathChanged)

public:
    using QObject::QObject;

    QStringList calls;

    QString path() const
    {
        return m_configured.value(QString());
    }

    Q_INVOKABLE QString effectivePath(const QString& screenName) const
    {
        const QString preview =
            m_preview.contains(screenName) ? m_preview.value(screenName) : m_preview.value(QString());
        if (!preview.isEmpty()) {
            return preview;
        }
        return m_configured.contains(screenName) ? m_configured.value(screenName) : m_configured.value(QString());
    }

    Q_INVOKABLE bool setPath(const QString& path, const QString& screenName)
    {
        calls.append(QStringLiteral("set:") + path + QLatin1Char('@') + screenName);
        if (screenName.isEmpty()) {
            m_configured.clear();
        }
        m_configured.insert(screenName, path);
        Q_EMIT pathChanged();
        Q_EMIT effectivePathChanged(screenName);
        return true;
    }

    Q_INVOKABLE bool setPreview(const QString& path, const QString& screenName)
    {
        calls.append(QStringLiteral("preview:") + path + QLatin1Char('@') + screenName);
        m_preview.insert(screenName, path);
        Q_EMIT effectivePathChanged(screenName);
        return true;
    }

    Q_INVOKABLE void clearPreview(const QString& screenName)
    {
        calls.append(QStringLiteral("clear:") + screenName);
        if (screenName.isEmpty()) {
            m_preview.clear();
        } else {
            m_preview.remove(screenName);
        }
        Q_EMIT effectivePathChanged(screenName);
    }

Q_SIGNALS:
    void pathChanged();
    void effectivePathChanged(const QString& screenName);

private:
    QHash<QString, QString> m_configured;
    QHash<QString, QString> m_preview;
};

// The retint runner, so a preview never spawns matugen.
class FakeRunner : public QObject
{
    Q_OBJECT

public:
    using QObject::QObject;

    Q_INVOKABLE void run(const QString&)
    {
    }
    Q_INVOKABLE void cancel()
    {
    }

Q_SIGNALS:
    void paletteReady(const QVariantMap& tokens, const QString& wallpaperPath);
    void failed(const QString& wallpaperPath, const QString& reason);
};

namespace {

// The candidate fixtures are placeholders (text files with image names),
// which is all the picker half needs: the scan lists, the fake never
// decodes. The surface half decodes, so it paints its own images.
QString fixtures()
{
    return QDir(QStringLiteral(PHOSPHOR_PICKER_FIXTURES)).filePath(QStringLiteral("wallpapers"));
}

QString fixture(const QString& name)
{
    return QDir(fixtures()).filePath(name);
}

// Builds one object from `source` under `import Phosphor.Picker`.
QObject* build(QQmlEngine& engine, const char* source)
{
    QQmlComponent component(&engine);
    component.setData(QByteArray("import Phosphor.Picker\n") + source, QUrl(QStringLiteral("qrc:/test.qml")));
    if (component.status() != QQmlComponent::Ready) {
        qWarning() << component.errorString();
        return nullptr;
    }
    return component.create();
}

} // namespace

class TestPickerWallpaper : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        // ShellEngine registers PanelWindow into Phosphor.Shell at
        // construction; there is no engine here, so the surface's base
        // type is registered by hand under the same name.
        qmlRegisterType<PhosphorShell::PanelWindow>("Phosphor.Shell", 1, 0, "PanelWindow");
    }

    void hoverPreviewsOnThisScreenAndLeavingClears()
    {
        QQmlEngine engine;
        FakeWallpaperService wallpaper;
        FakeRunner runner;
        std::unique_ptr<QObject> picker(makePicker(engine, wallpaper, runner));
        QVERIFY(picker);
        const QString current = fixture(QStringLiteral("alpha.png"));
        const QString other = fixture(QStringLiteral("Beta.JPG"));
        wallpaper.setPath(current, QString());
        wallpaper.calls.clear();

        QVERIFY(QMetaObject::invokeMethod(picker.get(), "reset"));
        // Opening with the current wallpaper selected previews nothing.
        QVERIFY(!wallpaper.calls.contains(QStringLiteral("preview:") + current + QStringLiteral("@DP-1")));
        wallpaper.calls.clear();

        QVERIFY(QMetaObject::invokeMethod(picker.get(), "hoverCandidate", Q_ARG(QString, other), Q_ARG(bool, true)));
        QCOMPARE(wallpaper.calls, QStringList{QStringLiteral("preview:") + other + QStringLiteral("@DP-1")});

        // Leaving with the current wallpaper still selected clears.
        QVERIFY(QMetaObject::invokeMethod(picker.get(), "hoverCandidate", Q_ARG(QString, other), Q_ARG(bool, false)));
        QCOMPARE(wallpaper.calls.last(), QStringLiteral("clear:DP-1"));

        // A selection previews too, and leaving a hover falls back to it.
        wallpaper.calls.clear();
        QVERIFY(QMetaObject::invokeMethod(picker.get(), "selectCandidate", Q_ARG(QString, other)));
        QCOMPARE(wallpaper.calls, QStringList{QStringLiteral("preview:") + other + QStringLiteral("@DP-1")});
        QVERIFY(QMetaObject::invokeMethod(picker.get(), "hoverCandidate", Q_ARG(QString, current), Q_ARG(bool, true)));
        QCOMPARE(wallpaper.calls.last(), QStringLiteral("preview:") + current + QStringLiteral("@DP-1"));
        QVERIFY(QMetaObject::invokeMethod(picker.get(), "hoverCandidate", Q_ARG(QString, current), Q_ARG(bool, false)));
        QCOMPARE(wallpaper.calls.last(), QStringLiteral("preview:") + other + QStringLiteral("@DP-1"));

        // Escape: the preview clears with the palette.
        wallpaper.calls.clear();
        QSignalSpy closed(picker.get(), SIGNAL(closed()));
        QVERIFY(QMetaObject::invokeMethod(picker.get(), "close"));
        QCOMPARE(wallpaper.calls, QStringList{QStringLiteral("clear:DP-1")});
        QCOMPARE(closed.count(), 1);
    }

    void applyPersistsThenClearsThePreview()
    {
        QQmlEngine engine;
        FakeWallpaperService wallpaper;
        FakeRunner runner;
        std::unique_ptr<QObject> picker(makePicker(engine, wallpaper, runner));
        QVERIFY(picker);
        const QString current = fixture(QStringLiteral("alpha.png"));
        const QString other = fixture(QStringLiteral("Beta.JPG"));
        wallpaper.setPath(current, QString());

        QVERIFY(QMetaObject::invokeMethod(picker.get(), "reset"));
        QVERIFY(QMetaObject::invokeMethod(picker.get(), "selectCandidate", Q_ARG(QString, other)));
        wallpaper.calls.clear();
        QSignalSpy applied(picker.get(), SIGNAL(applied(QString, bool)));
        QVERIFY(QMetaObject::invokeMethod(picker.get(), "apply", Q_ARG(bool, false)));
        QCOMPARE(wallpaper.calls,
                 (QStringList{QStringLiteral("set:") + other + QStringLiteral("@DP-1"), QStringLiteral("clear:DP-1")}));
        QCOMPARE(applied.count(), 1);
        QCOMPARE(applied.last().at(1).toBool(), false);

        // Apply on all screens targets the all-screens entry. Reopen so
        // the strip re-reads the (now changed) current path first.
        QVERIFY(QMetaObject::invokeMethod(picker.get(), "reset"));
        QVERIFY(QMetaObject::invokeMethod(picker.get(), "selectCandidate", Q_ARG(QString, current)));
        wallpaper.calls.clear();
        QVERIFY(QMetaObject::invokeMethod(picker.get(), "apply", Q_ARG(bool, true)));
        QCOMPARE(wallpaper.calls.first(), QStringLiteral("set:") + current + QStringLiteral("@"));

        // Applying the current wallpaper sets nothing but still clears.
        QVERIFY(QMetaObject::invokeMethod(picker.get(), "reset"));
        wallpaper.calls.clear();
        QVERIFY(QMetaObject::invokeMethod(picker.get(), "apply", Q_ARG(bool, false)));
        QCOMPARE(wallpaper.calls, QStringList{QStringLiteral("clear:DP-1")});
    }

    void surfaceFollowsItsScreenAndCrossfadesWhenReady()
    {
        QQmlEngine engine;
        FakeWallpaperService wallpaper;
        std::unique_ptr<QObject> surfaceObject(build(engine, "WallpaperSurface {}"));
        QVERIFY(surfaceObject);
        auto* surface = qobject_cast<PhosphorShell::PanelWindow*>(surfaceObject.get());
        QVERIFY(surface);

        // The Background-layer, full-screen, click-through recipe.
        QCOMPARE(surface->panelLayer(), PhosphorShell::PanelWindow::LayerBackground);
        QCOMPARE(surface->alignment(), PhosphorShell::PanelWindow::Fill);
        QVERIFY(!surface->exclusiveZoneEnabled());
        QCOMPARE(surface->keyboardFocus(), PhosphorShell::PanelWindow::None);
        QVERIFY(surface->hasExplicitInputRegion());
        QVERIFY(surface->inputRegion().toList().isEmpty());

        QScreen* screen = QGuiApplication::primaryScreen();
        QVERIFY(screen);
        surface->setScreen(screen);
        QCOMPARE(surface->thickness(), screen->geometry().height());
        const QString mine = screen->name();

        QTemporaryDir images;
        QVERIFY(images.isValid());
        QImage pixels(4, 4, QImage::Format_RGBA8888);
        pixels.fill(Qt::red);
        const QString alpha = QDir(images.path()).filePath(QStringLiteral("alpha.png"));
        const QString beta = QDir(images.path()).filePath(QStringLiteral("beta.png"));
        QVERIFY(pixels.save(alpha));
        QVERIFY(pixels.save(beta));
        wallpaper.setPath(alpha, QString());
        surface->setProperty("service", QVariant::fromValue<QObject*>(&wallpaper));
        QTRY_COMPARE(surface->property("shownPath").toString(), alpha);

        // Another output's preview is not this surface's business.
        wallpaper.setPreview(beta, QStringLiteral("not-") + mine);
        QCOMPARE(surface->property("pendingPath").toString(), QString());
        QCOMPARE(surface->property("shownPath").toString(), alpha);

        // Its own: loads behind, then swaps.
        wallpaper.setPreview(beta, mine);
        QTRY_COMPARE(surface->property("shownPath").toString(), beta);
        QCOMPARE(surface->property("pendingPath").toString(), QString());

        // Clearing returns to the configured path.
        wallpaper.clearPreview(mine);
        QTRY_COMPARE(surface->property("shownPath").toString(), alpha);

        // A path that cannot load is dropped and the front stays.
        wallpaper.setPreview(QDir(images.path()).filePath(QStringLiteral("missing.png")), QString());
        QTRY_COMPARE(surface->property("pendingPath").toString(), QString());
        QCOMPARE(surface->property("shownPath").toString(), alpha);

        // No service: the ground alone.
        surface->setProperty("service", QVariant::fromValue<QObject*>(nullptr));
        QCOMPARE(surface->property("shownPath").toString(), QString());
    }

private:
    QObject* makePicker(QQmlEngine& engine, FakeWallpaperService& wallpaper, FakeRunner& runner)
    {
        QObject* picker = build(engine, "Picker { screenName: \"DP-1\" }");
        if (!picker) {
            return nullptr;
        }
        picker->setProperty("wallpaper", QVariant::fromValue<QObject*>(&wallpaper));
        auto* retint = picker->property("retint").value<QObject*>();
        auto* candidates = picker->property("candidates").value<QObject*>();
        if (!retint || !candidates) {
            delete picker;
            return nullptr;
        }
        retint->setProperty("runner", QVariant::fromValue<QObject*>(&runner));
        candidates->setProperty("directories", QStringList{fixtures()});
        return picker;
    }
};

QTEST_MAIN(TestPickerWallpaper)
#include "test_pickerwallpaper.moc"
