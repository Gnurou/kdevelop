/***************************************************************************
 *   Copyright 1999-2001 by Bernd Gehrmann                                 *
 *   bernd@kdevelop.org                                                    *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#ifndef _GREPVIEWPART_H_
#define _GREPVIEWPART_H_

#include <iplugin.h>
#include <QtCore/QVariant>

class QStringList;
class GrepDialog;
class KDialog;

namespace KDevelop
{
    class IOutputView;
    class IProject;
}

class GrepViewPart : public KDevelop::IPlugin
{
    Q_OBJECT

public:
    GrepViewPart( QObject *parent, const QVariantList & = QVariantList() );
    ~GrepViewPart();

private Q_SLOTS:
    void slotGrep();
    void showDialogWithPattern(const QString& pattern);
    void searchActivated();
    void updateOkButton(const QString& text);

private:
    static QString escape(const QString &str);
    QString currentWord();
    QString currentSelectedWord();

    GrepDialog* m_grepdlg;
    QString m_lastPattern;

    KDevelop::IProject *m_projectForActiveFile;

    KDevelop::IOutputView *m_view;
};

#endif
