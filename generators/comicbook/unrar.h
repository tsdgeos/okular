/***************************************************************************
 *   Copyright (C) 2007 by Tobias Koenig <tokoe@kde.org>                   *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 ***************************************************************************/

#ifndef UNRAR_H
#define UNRAR_H

#include <QObject>
#include <QProcess>
#include <QStringList>

#include <unrarflavours.h>

class QEventLoop;
class QTemporaryDir;

#if defined(WITH_KPTY)
class KPtyProcess;
#endif

class Unrar : public QObject
{
    Q_OBJECT

public:
    /**
     * Creates a new unrar object.
     */
    Unrar();

    /**
     * Destroys the unrar object.
     */
    ~Unrar() override;

    /**
     * Opens given rar archive.
     */
    bool open(const QString &fileName);

    /**
     * Returns the list of files from the archive.
     */
    QStringList list();

    /**
     * Returns the content of the file with the given name.
     */
    QByteArray contentOf(const QString &fileName) const;

    /**
     * Returns a new device for reading the file with the given name.
     */
    QIODevice *createDevice(const QString &fileName) const;

    static bool isAvailable();
    static bool isSuitableVersionAvailable();

private Q_SLOTS:
    void readFromStdout();
    void readFromStderr();
    void finished(int exitCode, QProcess::ExitStatus exitStatus);

private:
    int startSyncProcess(const ProcessArgs &args);
    void writeToProcess(const QByteArray &data);

#if defined(WITH_KPTY)
    KPtyProcess *mProcess;
#else
    QProcess *mProcess;
#endif
    QEventLoop *mLoop;
    QString mFileName;
    QByteArray mStdOutData;
    QByteArray mStdErrData;
    QTemporaryDir *mTempDir;
};

#endif
