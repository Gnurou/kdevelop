/***************************************************************************
 *   Copyright (C) 2001 by Bernd Gehrmann                                  *
 *   bernd@kdevelop.org                                                    *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include <qlabel.h>
#include <qlayout.h>
#include <qpushbutton.h>
#include <qtextstream.h>
#include <kbuttonbox.h>
#include <kdebug.h>
#include <kfiledialog.h>
#include <kmessagebox.h>

#include "kdevmakefrontend.h"
#include "misc.h"
#include "autoprojectpart.h"
#include "autoprojectwidget.h"
#include "addsubprojectdlg.h"


AddSubprojectDialog::AddSubprojectDialog(AutoProjectPart *part, AutoProjectWidget *widget,
                                         SubprojectItem *item, QWidget *parent, const char *name)
    : QDialog(parent, name, true)
{
    setCaption(("Add subproject"));
    
    QLabel *name_label = new QLabel(i18n("&Name:"), this);
    name_edit = new QLineEdit(this);
    name_edit->setFocus();
    name_label->setBuddy(name_edit);

    QVBoxLayout *layout = new QVBoxLayout(this, 10);
    
    QGridLayout *grid = new QGridLayout(1, 2);
    layout->addLayout(grid);
    grid->addWidget(name_label, 0, 0);
    grid->addWidget(name_edit, 0, 1);

    QFrame *frame = new QFrame(this);
    frame->setFrameStyle(QFrame::HLine | QFrame::Sunken);
    layout->addWidget(frame, 0);

    KButtonBox *buttonbox = new KButtonBox(this);
    buttonbox->addStretch();
    QPushButton *ok = buttonbox->addButton(i18n("&OK"));
    QPushButton *cancel = buttonbox->addButton(i18n("Cancel"));
    ok->setDefault(true);
    connect( ok, SIGNAL(clicked()), this, SLOT(accept()) );
    connect( cancel, SIGNAL(clicked()), this, SLOT(reject()) );
    buttonbox->layout();
    layout->addWidget(buttonbox, 0);

    subProject = item;
    m_widget = widget;
    m_part = part;
}


AddSubprojectDialog::~AddSubprojectDialog()
{}


void AddSubprojectDialog::accept()
{
    QString name = name_edit->text().stripWhiteSpace();

    if (name.isEmpty()) {
        KMessageBox::sorry(this, i18n("You have to give the subproject a name"));
        return;
    }

    QListViewItem *childItem = subProject->firstChild();
    while (childItem) {
        if (name == static_cast<SubprojectItem*>(childItem)->subdir) {
            KMessageBox::sorry(this, i18n("A subproject with this name already exists"));
            return;
        }
        childItem = childItem->nextSibling();
    }

    QString relmakefile = (subProject->path + "/" + name + "/Makefile").mid(m_widget->projectDirectory().length()+1);
    kdDebug(9020) << "Relative makefile path: " << relmakefile << endl;
    QDir dir(subProject->path);
    if (!dir.mkdir(name)) {
        KMessageBox::sorry(this, i18n("Could not create subdirectory %1").arg(name));
        return;
    }

    // Adjust SUBDIRS variable in containing Makefile.am
    subProject->variables["SUBDIRS"] += (QCString(" ") + name.latin1());
    QMap<QCString,QCString> replaceMap;
    replaceMap.insert("SUBDIRS", subProject->variables["SUBDIRS"]);
    AutoProjectTool::modifyMakefileam(subProject->path + "/Makefile.am", replaceMap);

    // Create new item in tree view
    SubprojectItem *newitem = new SubprojectItem(subProject, name);
    newitem->subdir = name;
    newitem->path = subProject->path + "/" + name;
    newitem->variables["INCLUDES"] = subProject->variables["INCLUDES"];
    newitem->setOpen(true);
    
    // Move to the bottom of the list
    QListViewItem *lastItem = subProject->firstChild();
    while (lastItem->nextSibling())
        lastItem = lastItem->nextSibling();
    if (lastItem != newitem)
        newitem->moveItem(lastItem);
    
    // Create the new subdirectory and create a Makefile in it
    
    QFile f(m_widget->projectDirectory() + "/" + relmakefile + ".am");
    if (!f.open(IO_WriteOnly)) {
        KMessageBox::sorry(this, i18n("Could not create Makefile.am in subdirectory %1").arg(name));
        return;
    }
    QTextStream stream(&f);
    stream << "INCLUDES = " << newitem->variables["INCLUDES"] << endl;
    f.close();

    QString cmdline = "cd ";
    cmdline += m_widget->projectDirectory();
    cmdline += " && automake ";
    cmdline += relmakefile;
    cmdline += " && CONFIG_HEADERS=config.h CONFIG_FILES=";
    cmdline += relmakefile;
    cmdline += " config.status";
    m_part->makeFrontend()->queueCommand(m_widget->projectDirectory(), cmdline);
    
    QDialog::accept();
}

#include "addsubprojectdlg.moc"
