/*
    This program is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public License
    as published by the Free Software Foundation; either version 2
    of the License, or (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor,
    Boston, MA  02110-1301, USA.

    ---
    Copyright (C) 2020 Alexander Semke <alexander.semke@web.de>
 */

#include "pythonsettingswidget.h"
#include "settings.h"

#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QProcess>
#include <QPushButton>
#include <QRegularExpression>
#include <QSet>
#include <QStandardItemModel>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <QVariant>
#include <QtConcurrent>

#include <KConfigDialog>
#include <KConfigSkeleton>
#include <KLocalizedString>

namespace
{
const int maxEnvironmentCandidates = 64;
const int maxProbeCount = 24;
const int maxScannedFolders = 500;

QString probePythonVersion(const QString& pythonPath)
{
    if (pythonPath.isEmpty())
        return QString();

    QProcess process;
    process.start(pythonPath, QStringList() << QStringLiteral("-c")
                 << QStringLiteral("import sys;print('%d.%d.%d' % sys.version_info[:3])"));

    if (!process.waitForStarted(1500))
        return QString();

    if (!process.waitForFinished(1500))
    {
        process.kill();
        process.waitForFinished(1000);
        return QString();
    }

    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
        return QString();

    return QString::fromLocal8Bit(process.readAllStandardOutput()).trimmed();
}

bool isEnvironmentFolder(const QString& folder)
{
    return QFile::exists(folder + QLatin1String("/pyvenv.cfg"))
        || QFile::exists(folder + QLatin1String("/bin/python"))
        || QFile::exists(folder + QLatin1String("/bin/python3"))
        || QFile::exists(folder + QLatin1String("/Scripts/python.exe"));
}

void scanFolderForEnvironmentsRecursively(const QString& folder, int depth, int maxDepth,
                                          QStringList& found, int& visited, const QAtomicInt* cancel)
{
    if (!cancel || cancel->loadAcquire())
        return;

    if (folder.isEmpty() || depth > maxDepth || found.size() >= maxEnvironmentCandidates)
        return;

    if (visited >= maxScannedFolders)
        return;

    const QFileInfo folderInfo(folder);
    if (!folderInfo.exists() || !folderInfo.isDir())
        return;

    ++visited;

    if (isEnvironmentFolder(folder))
    {
        found << folder;
        return;
    }

    // Iterate lazily instead of materializing the whole listing, so that an
    // extremely large directory cannot exhaust memory before the visit cap
    // is reached.
    QDirIterator iterator(folder, QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks,
                          QDirIterator::NoIteratorFlags);
    while (iterator.hasNext())
    {
        if (cancel->loadAcquire() || found.size() >= maxEnvironmentCandidates || visited >= maxScannedFolders)
            break;

        iterator.next();
        scanFolderForEnvironmentsRecursively(iterator.fileInfo().absoluteFilePath(), depth + 1, maxDepth,
                                             found, visited, cancel);
    }
}

QList<QPair<QString, QString>> collectEnvironments(const QString& currentPath, const QStringList& roots,
                                                   const QAtomicInt* cancel)
{
    QList<QPair<QString, QString>> candidates;
    QSet<QString> seen;
    int probed = 0;

    const auto addCandidate = [&](const QString& pythonPath) {
        if (!cancel || cancel->loadAcquire() || pythonPath.isEmpty()
            || candidates.size() >= maxEnvironmentCandidates)
            return;

        const QFileInfo info(pythonPath);
        if (!info.exists() || !info.isFile() || !info.isExecutable())
            return;

        QString canonical = info.canonicalFilePath();
        if (canonical.isEmpty())
            canonical = pythonPath;
        if (seen.contains(canonical))
            return;
        seen.insert(canonical);

        QString version;
        if (probed < maxProbeCount)
        {
            ++probed;
            version = probePythonVersion(pythonPath);
        }

        candidates.append(qMakePair(pythonPath, version));
    };

    // The interpreter currently configured in the main setting comes first.
    addCandidate(currentPath);

    // System-wide candidates.
    const QString python3 = QStandardPaths::findExecutable(QStringLiteral("python3"));
    if (!python3.isEmpty())
        addCandidate(python3);

    const QRegularExpression python3Pattern(QStringLiteral("^python3(\\.[0-9]+)*[a-z]*$"));

    QDir usrBin(QStringLiteral("/usr/bin"));
    const QStringList usrBinEntries = usrBin.entryList(QStringList() << QStringLiteral("python3.*"),
                                                       QDir::Files | QDir::Executable, QDir::Name);
    for (const QString& entry : usrBinEntries)
    {
        if (python3Pattern.match(entry).hasMatch())
            addCandidate(usrBin.absoluteFilePath(entry));
    }

    QDir usrLocalBin(QStringLiteral("/usr/local/bin"));
    const QStringList usrLocalBinEntries = usrLocalBin.entryList(QStringList() << QStringLiteral("python3*"),
                                                                 QDir::Files | QDir::Executable, QDir::Name);
    for (const QString& entry : usrLocalBinEntries)
    {
        if (python3Pattern.match(entry).hasMatch())
            addCandidate(usrLocalBin.absoluteFilePath(entry));
    }

    // Environment roots. Folders added by the user are scanned first so that
    // they are not crowded out by a large default environment collection.
    QStringList found;
    int visited = 0;
    for (const QString& root : roots)
    {
        if (!cancel || cancel->loadAcquire())
            break;

        scanFolderForEnvironmentsRecursively(root, 0, 3, found, visited, cancel);
    }

    for (const QString& environmentPath : found)
    {
        if (!cancel || cancel->loadAcquire() || candidates.size() >= maxEnvironmentCandidates)
            break;

        QString interpreterPath;
        const QStringList interpreterCandidates {
            environmentPath + QLatin1String("/bin/python3"),
            environmentPath + QLatin1String("/bin/python"),
            environmentPath + QLatin1String("/Scripts/python.exe")
        };
        for (const QString& candidate : interpreterCandidates)
        {
            const QFileInfo candidateInfo(candidate);
            if (candidateInfo.exists() && candidateInfo.isFile() && candidateInfo.isExecutable())
            {
                interpreterPath = candidate;
                break;
            }
        }

        addCandidate(interpreterPath);
    }

    return candidates;
}

void disableComboItem(QComboBox* comboBox, int index)
{
    auto* model = qobject_cast<QStandardItemModel*>(comboBox->model());
    if (!model)
        return;

    if (QStandardItem* item = model->item(index))
        item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
}
}

PythonSettingsWidget::PythonSettingsWidget(QWidget* parent, const QString& id) : BackendSettingsWidget(parent, id)
{
    setupUi(this);

    m_tabWidget = tabWidget;
    m_tabDocumentation = tabDocumentation;
    connect(tabWidget, &QTabWidget::currentChanged, this, &BackendSettingsWidget::tabChanged);

    connect(envComboBox, QOverload<int>::of(&QComboBox::activated), this, &PythonSettingsWidget::environmentSelected);
    connect(btnDetectEnvs, &QPushButton::clicked, this, &PythonSettingsWidget::refreshEnvironments);
    connect(btnBrowseEnvs, &QPushButton::clicked, this, &PythonSettingsWidget::browseEnvironmentFolder);

    // The settings dialog is shown non-modally and is not deleted by its owner,
    // so arrange for it to be destroyed on close and stop any running scan
    // when the dialog goes away.
    if (KConfigDialog* dialog = qobject_cast<KConfigDialog*>(window()))
    {
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        connect(dialog, &QDialog::rejected, this, &PythonSettingsWidget::cancelEnvironmentScan);
        connect(dialog, &QDialog::accepted, this, &PythonSettingsWidget::cancelEnvironmentScan);
    }

    QTimer::singleShot(0, this, &PythonSettingsWidget::refreshEnvironments);
}

PythonSettingsWidget::~PythonSettingsWidget()
{
    // Ask a still running worker to stop as soon as possible. The worker holds
    // its own reference to the flag, so it stays valid until the worker exits.
    cancelEnvironmentScan();
}

void PythonSettingsWidget::cancelEnvironmentScan()
{
    if (m_scanCancel)
        m_scanCancel->storeRelease(1);
    m_scanCancel.reset();

    // Invalidate a result that is still in flight and allow a later refresh
    // if this widget outlives the scan (e.g. the dialog was only hidden).
    ++m_scanGeneration;
    m_refreshing = false;
    m_refreshPending = false;
}

void PythonSettingsWidget::refreshEnvironments()
{
    if (m_refreshing)
    {
        m_refreshPending = true;
        return;
    }

    m_refreshing = true;

    if (m_scanCancel)
        m_scanCancel->storeRelease(1);

    const QSharedPointer<QAtomicInt> cancel = QSharedPointer<QAtomicInt>::create(0);
    m_scanCancel = cancel;
    const quint64 generation = ++m_scanGeneration;

    const QString currentPath = kcfg_Interpreter->url().toLocalFile();

    // User supplied folders are scanned before the default locations.
    QStringList roots = PythonSettings::searchFolders();
    const QString home = QDir::homePath();
    roots << home + QLatin1String("/.virtualenvs")
          << home + QLatin1String("/venvs")
          << home + QLatin1String("/.local/share/virtualenvs")
          << home + QLatin1String("/miniconda3/envs")
          << home + QLatin1String("/anaconda3/envs")
          << home + QLatin1String("/.conda/envs");
    roots.removeDuplicates();

    envComboBox->blockSignals(true);
    envComboBox->clear();
    envComboBox->addItem(i18n("Scanning for Python interpreters…"));
    disableComboItem(envComboBox, 0);
    envComboBox->blockSignals(false);
    btnDetectEnvs->setEnabled(false);

    auto* watcher = new QFutureWatcher<QList<QPair<QString, QString>>>(this);
    connect(watcher, &QFutureWatcher<QList<QPair<QString, QString>>>::finished, this,
            [this, watcher, generation]() {
                const QList<QPair<QString, QString>> candidates = watcher->result();
                watcher->deleteLater();
                if (generation != m_scanGeneration)
                    return;
                populateEnvironmentCombo(candidates);
            });
    watcher->setFuture(QtConcurrent::run([currentPath, roots, cancel]() {
        return collectEnvironments(currentPath, roots, cancel.data());
    }));
}

void PythonSettingsWidget::populateEnvironmentCombo(const QList<QPair<QString, QString>>& candidates)
{
    const QString currentPath = kcfg_Interpreter->url().toLocalFile();
    const QString currentCanonical = QFileInfo(currentPath).canonicalFilePath();

    envComboBox->blockSignals(true);
    envComboBox->clear();

    int currentIndex = -1;
    for (const QPair<QString, QString>& candidate : candidates)
    {
        const QString path = candidate.first;
        const QString version = candidate.second;

        bool isCurrent = false;
        if (!currentPath.isEmpty())
        {
            if (path == currentPath)
                isCurrent = true;
            else
            {
                const QString itemCanonical = QFileInfo(path).canonicalFilePath();
                isCurrent = !currentCanonical.isEmpty() && !itemCanonical.isEmpty()
                    && itemCanonical == currentCanonical;
            }
        }

        const QString versionText = version.isEmpty() ? QStringLiteral("—") : QStringLiteral("v") + version;
        const QString text = isCurrent
            ? i18n("Current — %1 — %2", versionText, path)
            : QStringLiteral("%1 — %2").arg(versionText, path);

        envComboBox->addItem(text, path);
        envComboBox->setItemData(envComboBox->count() - 1, path, Qt::ToolTipRole);

        if (isCurrent && currentIndex < 0)
            currentIndex = envComboBox->count() - 1;
    }

    if (envComboBox->count() == 0)
    {
        envComboBox->addItem(i18n("No Python interpreters found"));
        disableComboItem(envComboBox, 0);
        currentIndex = 0;
    }
    else if (currentIndex < 0)
        currentIndex = 0;

    envComboBox->setCurrentIndex(currentIndex);
    envComboBox->blockSignals(false);

    btnDetectEnvs->setEnabled(true);
    m_refreshing = false;

    if (m_refreshPending)
    {
        m_refreshPending = false;
        QTimer::singleShot(0, this, &PythonSettingsWidget::refreshEnvironments);
    }
}

void PythonSettingsWidget::environmentSelected(int index)
{
    if (index < 0)
        return;

    const QString path = envComboBox->itemData(index).toString();
    if (path.isEmpty())
        return;

    const QFileInfo info(path);
    if (!info.exists() || !info.isFile() || !info.isExecutable())
        return;

    kcfg_Interpreter->setUrl(QUrl::fromLocalFile(path));
    envComboBox->setToolTip(path);
}

void PythonSettingsWidget::browseEnvironmentFolder()
{
    const QString folder = QFileDialog::getExistingDirectory(this, i18n("Select a folder containing Python environments"));
    if (folder.isEmpty())
        return;

    QStringList folders = PythonSettings::searchFolders();
    const QString canonical = QFileInfo(folder).canonicalFilePath();

    bool alreadyListed = false;
    for (const QString& existing : qAsConst(folders))
    {
        const QString existingCanonical = QFileInfo(existing).canonicalFilePath();
        if (existing == folder
            || (!canonical.isEmpty() && !existingCanonical.isEmpty() && canonical == existingCanonical))
        {
            alreadyListed = true;
            break;
        }
    }

    if (!alreadyListed)
    {
        if (!m_searchFoldersSnapshotValid)
        {
            m_searchFoldersBeforeEdit = folders;
            m_searchFoldersSnapshotValid = true;

            // SearchFolders has no widget of its own, so KConfigDialogManager
            // does not save it. Persist it here and roll it back if the dialog
            // is cancelled.
            if (KConfigDialog* dialog = qobject_cast<KConfigDialog*>(window()))
            {
                m_dialogRejectedConnection = connect(dialog, &QDialog::rejected,
                                                    this, &PythonSettingsWidget::restoreSearchFoldersOnCancel);
                // An Apply commits the change; Cancel afterwards must not undo it.
                connect(dialog, &KConfigDialog::settingsChanged, this, [this](const QString&) {
                    m_searchFoldersSnapshotValid = false;
                    disconnect(m_dialogRejectedConnection);
                });
            }
        }

        folders << folder;
        storeSearchFolders(folders);
        PythonSettings::self()->save();
    }

    refreshEnvironments();
}

void PythonSettingsWidget::restoreSearchFoldersOnCancel()
{
    if (!m_searchFoldersSnapshotValid)
        return;

    storeSearchFolders(m_searchFoldersBeforeEdit);
    PythonSettings::self()->save();

    m_searchFoldersSnapshotValid = false;
    disconnect(m_dialogRejectedConnection);
}

void PythonSettingsWidget::storeSearchFolders(const QStringList& folders)
{
    // PythonSettings has no generated setter because its .kcfgc does not
    // enable mutators, so update the skeleton item directly.
    KConfigSkeletonItem* item = PythonSettings::self()->findItem(QStringLiteral("SearchFolders"));
    if (item)
        item->setProperty(QVariant(folders));
}
