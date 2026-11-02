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

#ifndef PYTHONSETTINGSWIDGET_H
#define PYTHONSETTINGSWIDGET_H

#include "ui_settings.h"
#include "../backendsettingswidget.h"

#include <QAtomicInt>
#include <QList>
#include <QMetaObject>
#include <QPair>
#include <QSet>
#include <QSharedPointer>
#include <QString>
#include <QStringList>

class PythonSettingsWidget : public BackendSettingsWidget, public Ui::PythonSettingsBase
{
  Q_OBJECT

  public:
    explicit PythonSettingsWidget(QWidget* parent = nullptr, const QString& id = QString());
    ~PythonSettingsWidget() override;

  private Q_SLOTS:
    void refreshEnvironments();
    void environmentSelected(int index);
    void browseEnvironmentFolder();
    void restoreSearchFoldersOnCancel();
    void cancelEnvironmentScan();

  private:
    void populateEnvironmentCombo(const QList<QPair<QString, QString>>& candidates);
    void storeSearchFolders(const QStringList& folders);

    bool m_refreshing = false;
    bool m_refreshPending = false;
    QSharedPointer<QAtomicInt> m_scanCancel;
    quint64 m_scanGeneration = 0;
    QStringList m_searchFoldersBeforeEdit;
    bool m_searchFoldersSnapshotValid = false;
    QMetaObject::Connection m_dialogRejectedConnection;
};

#endif /* _PYTHONSETTINGSWIDGET_H */
