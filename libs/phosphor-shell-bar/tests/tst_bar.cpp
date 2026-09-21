// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// QtQuickTest runner for the Phosphor.Bar framework. Cases live in the
// tst_*.qml files alongside; the source dir is passed via -input from
// add_test, and the offscreen QPA platform keeps the run headless.
//
// SNI registration is also imperative. Only tst_tray.qml starts the Python
// fixture, on the test runner's isolated session bus.

#include <PhosphorServiceIconTheme/QmlRegistration.h>
#include <PhosphorServiceSni/QmlRegistration.h>
#include <PhosphorShell/QmlRegistration.h>

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusServiceWatcher>
#include <QDebug>
#include <QEventLoop>
#include <QJsonDocument>
#include <QObject>
#include <QProcess>
#include <QQmlContext>
#include <QQmlEngine>
#include <QTimer>
#include <QtQuickTest/quicktest.h>

class TrayFixture : public QObject
{
    Q_OBJECT

public:
    using QObject::QObject;
    ~TrayFixture() override
    {
        stop();
    }

    Q_INVOKABLE bool start()
    {
        if (m_process.state() != QProcess::NotRunning)
            return true;
        const auto bus = QDBusConnection::sessionBus();
        if (!bus.isConnected() || bus.interface()->isServiceRegistered(service()))
            return false;
        m_process.setProcessChannelMode(QProcess::MergedChannels);
        m_process.start(QStringLiteral(PHOSPHOR_TRAY_FIXTURE_PYTHON),
                        {QStringLiteral(PHOSPHOR_TRAY_FIXTURE_PATH), QStringLiteral("empty")});
        if (!m_process.waitForStarted(2000)) {
            qWarning() << "Tray fixture failed to start:" << m_process.errorString();
            return false;
        }
        QEventLoop ready;
        QDBusServiceWatcher watcher(service(), bus, QDBusServiceWatcher::WatchForRegistration);
        connect(&watcher, &QDBusServiceWatcher::serviceRegistered, &ready, &QEventLoop::quit);
        connect(&m_process, &QProcess::finished, &ready, &QEventLoop::quit);
        QTimer::singleShot(3000, &ready, &QEventLoop::quit);
        if (!bus.interface()->isServiceRegistered(service()))
            ready.exec();
        const bool registered = bus.interface()->isServiceRegistered(service());
        if (!registered)
            qWarning().noquote() << "Tray fixture did not register:" << m_process.readAll();
        return registered;
    }

    Q_INVOKABLE void stop()
    {
        if (m_process.state() == QProcess::NotRunning)
            return;
        m_process.terminate();
        if (!m_process.waitForFinished(2000)) {
            m_process.kill();
            m_process.waitForFinished(2000);
        }
    }

    Q_INVOKABLE QVariant call(const QString& method, const QVariantList& arguments)
    {
        auto request =
            QDBusMessage::createMethodCall(service(), QStringLiteral("/org/phosphor/TrayFixture"), service(), method);
        request.setArguments(arguments);
        const auto reply = QDBusConnection::sessionBus().call(request, QDBus::BlockWithGui, 2500);
        if (reply.type() == QDBusMessage::ErrorMessage) {
            qWarning() << "Tray fixture call failed:" << method << reply.errorMessage();
            return false;
        }
        return reply.arguments().isEmpty() ? QVariant(true) : reply.arguments().constFirst();
    }

    Q_INVOKABLE QVariantMap status()
    {
        return QJsonDocument::fromJson(call(QStringLiteral("Status"), {}).toString().toUtf8()).toVariant().toMap();
    }

    Q_INVOKABLE QVariantList events()
    {
        return QJsonDocument::fromJson(call(QStringLiteral("Events"), {}).toString().toUtf8()).toVariant().toList();
    }

private:
    static QString service()
    {
        return QStringLiteral("org.phosphor.TrayFixture");
    }
    QProcess m_process;
};

class Setup : public QObject
{
    Q_OBJECT

public Q_SLOTS:
    void qmlEngineAvailable(QQmlEngine* engine)
    {
        PhosphorShell::registerQmlTypes();
        PhosphorServiceSni::registerQmlTypes();
        PhosphorServiceIconTheme::registerQmlTypes();
        PhosphorServiceIconTheme::installImageProvider(engine);
        engine->rootContext()->setContextProperty(QStringLiteral("trayFixture"), &m_trayFixture);
    }

private:
    TrayFixture m_trayFixture;
};

QUICK_TEST_MAIN_WITH_SETUP(phosphor_shell_bar, Setup)

#include "tst_bar.moc"
