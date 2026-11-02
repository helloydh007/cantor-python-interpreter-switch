/*
    SPDX-License-Identifier: GPL-2.0-or-later
    SPDX-FileCopyrightText: 2012 Filipe Saraiva <filipe@kde.org>
*/

#include "pythonbackend.h"
#include "pythonsession.h"
#include "pythonextensions.h"
#include "pythonsettingswidget.h"
#include "settings.h"
#include "ui_settings.h"

#include <QDebug>
#include <QFileInfo>
#include <QTimer>
#include <QUrl>
#include <QWidget>

#include <KConfigSkeleton>
#include <KPluginFactory>

PythonBackend::PythonBackend(QObject* parent, const QList<QVariant>& args) : Cantor::Backend(parent, args)
{
    new PythonLinearAlgebraExtension(this);
    new PythonPackagingExtension(this);
    new PythonPlotExtension(this);
    new PythonScriptExtension(this);
    new PythonVariableManagementExtension(this);

    //setObjectName(QLatin1String("python3backend"));

    m_lastInterpreter = PythonSettings::interpreter().toLocalFile();
    connect(PythonSettings::self(), &KConfigSkeleton::configChanged, this, &PythonBackend::handleConfigChanged);
}

QWidget* PythonBackend::settingsWidget(QWidget* parent) const
{
    return new PythonSettingsWidget(parent, id());
}

Cantor::Session* PythonBackend::createSession()
{
    auto* session = new PythonSession(this);
    registerSession(session);
    return session;
}

void PythonBackend::registerSession(PythonSession* session)
{
    if (!session)
        return;

    m_sessions.append(session);
    connect(session, &QObject::destroyed, this, [this, session]() {
        m_sessions.removeOne(session);
        m_pendingRestarts.removeOne(session);
    });
}

void PythonBackend::handleConfigChanged()
{
    const QString interpreter = PythonSettings::interpreter().toLocalFile();
    if (interpreter == m_lastInterpreter)
        return;

    m_lastInterpreter = interpreter;

    // Stop the active sessions. They stay Disabled and the next evaluation
    // logs in again on demand with the new interpreter, so the settings dialog
    // never has to wait synchronously for an interpreter to start.
    m_pendingRestarts.clear();
    for (PythonSession* session : qAsConst(m_sessions))
    {
        if (session && session->status() != Cantor::Session::Disable)
            m_pendingRestarts.append(session);
    }

    // Only one logout chain is ever queued; later configuration changes just
    // refresh the pending list.
    if (!m_pendingRestarts.isEmpty() && !m_restartScheduled)
    {
        m_restartScheduled = true;
        QTimer::singleShot(0, this, &PythonBackend::logoutNextSession);
    }
}

void PythonBackend::logoutNextSession()
{
    if (m_pendingRestarts.isEmpty())
    {
        m_restartScheduled = false;
        return;
    }

    PythonSession* session = m_pendingRestarts.takeFirst();
    if (session && m_sessions.contains(session) && session->status() != Cantor::Session::Disable)
        session->logout();

    if (m_pendingRestarts.isEmpty())
        m_restartScheduled = false;
    else
        QTimer::singleShot(0, this, &PythonBackend::logoutNextSession);
}

QString PythonBackend::id() const
{
    return QLatin1String("python");
}

QString PythonBackend::version() const
{
    return QLatin1String("3.6");
}

Cantor::Backend::Capabilities PythonBackend::capabilities() const
{
    qDebug()<<"Requesting capabilities of PythonSession";

    Backend::Capabilities cap =
        Cantor::Backend::SyntaxHighlighting |
        Cantor::Backend::Completion         |
        Cantor::Backend::SyntaxHelp         |
        Cantor::Backend::IntegratedPlots;

    if(PythonSettings::variableManagement())
        cap |= Cantor::Backend::VariableManagement;

    return cap;
}

QUrl PythonBackend::helpUrl() const
{
    return QUrl(i18nc("The url to the documentation Python", "https://docs.python.org/3/"));
}

QString PythonBackend::description() const
{
    return i18n("<b>Python</b> is a remarkably powerful dynamic programming language that is used in a wide variety of application domains. " \
                "There are several Python packages to scientific programming.");
}

KConfigSkeleton* PythonBackend::config() const
{
    return PythonSettings::self();
}

bool PythonBackend::requirementsFullfilled(QString* const reason) const
{
    const QString& interpreterPath = PythonSettings::interpreter().toLocalFile();
    if (!interpreterPath.isEmpty() && QFileInfo(interpreterPath).isExecutable())
    {
        return Cantor::Backend::checkExecutable(i18n("Python interpreter"), interpreterPath, reason);
    }

#ifdef Q_OS_WIN
    const QString& path = QStandardPaths::findExecutable(QLatin1String("cantor_pythonserver.exe"));
#else
    const QString& path = QStandardPaths::findExecutable(QLatin1String("cantor_pythonserver"));
#endif
    // The embedded server is the fallback used when the configured interpreter
    // is empty or not usable. If it is missing as well, report the problem for
    // the interpreter the user selected instead of the fallback server.
    if (!interpreterPath.isEmpty() && path.isEmpty())
        return Cantor::Backend::checkExecutable(i18n("Python interpreter"), interpreterPath, reason);
    return Cantor::Backend::checkExecutable(QLatin1String("Cantor Python Server"), path, reason);
}

K_PLUGIN_FACTORY_WITH_JSON(pythonbackend, "pythonbackend.json", registerPlugin<PythonBackend>();)
#include "pythonbackend.moc"

