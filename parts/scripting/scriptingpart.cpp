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

#include <Python.h>
#include <stdlib.h>
#include <dcopclient.h>
#include <kdebug.h>
#include <kstandarddirs.h>

#include "kdevcore.h"
#include "scriptingfactory.h"
#include "scriptingpart.h"


static ScriptingPart *scripting_part;
extern "C" {
    void initkdevelopc();
    void initpydcopc();
}


ScriptingPart::ScriptingPart(KDevApi *api, QObject *parent, const char *name)
    : KDevPart(api, parent, name)
{
    setInstance(ScriptingFactory::instance());
    
    QString xml = QString::fromLatin1("<!DOCTYPE kpartgui SYSTEM \"kpartgui.dtd\">\n"
                                      "<kpartgui version=\"1\" name=\"editorpart\">\n"
                                      "<MenuBar>\n"
                                      "</MenuBar>\n"
                                      "</kpartgui>");
    guiDocument.setContent(xml);
    setDOMDocument(guiDocument);

    scripting_part = this;

    QString moddir = KGlobal::dirs()->findResourceDir("data", "kdevscripting/kdevelop.py") + "kdevscripting";
    char *env = strdup(QString::fromLatin1("PYTHONPATH=%1").arg(moddir).latin1());
    putenv(env);
    Py_Initialize();
    free(env);
    
    kdDebug(9011) << "Init kdevelopc" << endl;
    initkdevelopc();
    
    kdDebug(9011) << "Init pydcopc" << endl;
    initpydcopc();

    kdDebug(9011) << "import kdevelop" << endl;
    PyRun_SimpleString((char*)"import kdevelop");

    kdDebug(9011) << "from init import *" << endl;
    PyRun_SimpleString((char*)"from init import *");

#if 0
    QString initfile = locate("data", "kdevpython/init.py");
    FILE *f1 = fopen(initfile.latin1(), "r");
    kdDebug(9011) << "evaluate init.py" << endl;
    PyRun_SimpleFile(f1, (char*)initfile.latin1());
    fclose(f1);
#endif
}


ScriptingPart::~ScriptingPart()
{}


PyObject *ScriptingPart::addMenuItem(PyObject *args)
{
    char *menu, *submenu;
    PyObject *func;
    
    if (!PyArg_ParseTuple(args, (char*)"ssO", &menu, &submenu, &func))
        return 0;

    if (!PyCallable_Check(func)) {
        kdDebug(9011) << "Scripting function not callable" << endl;
        return 0;
    }
    
    QString menustr = QString::fromLatin1(menu);
    QString submenustr = QString::fromLatin1(submenu);
    QString ident = menustr + submenustr;

    Py_XINCREF(func);
    
    actions.insert(ident, func);
    
    (void) new KAction(submenustr, 0, this, SLOT(slotScriptAction()),
                       actionCollection(), ident.latin1());
    
    QDomElement el = guiDocument.documentElement();
    el = el.namedItem("MenuBar").toElement();
    QDomElement child = el.firstChild().toElement();
    while (!child.isNull()) {
        if (child.tagName() == "Menu" && child.attribute("name") == menustr)
            break;
        child = child.nextSibling().toElement();
    }
    if (child.isNull()) {
        child = guiDocument.createElement(QString::fromLatin1("Menu"));
        child.setAttribute(QString::fromLatin1("name"), menustr);
        el.appendChild(child);
    }
    el = child;
    
    child = guiDocument.createElement(QString::fromLatin1("Action"));
    child.setAttribute(QString::fromLatin1("name"), ident);
    el.appendChild(child);

    kdDebug(9011) << "New dom document: " << guiDocument.toString() << endl;
    
    setDOMDocument(guiDocument);

    Py_INCREF(Py_None);
    return Py_None;
}



PyObject *ScriptingPart::appId(PyObject *args)
{
    if (!PyArg_ParseTuple(args, (char*)""))
        return NULL;

    return Py_BuildValue((char*)"s", DCOPClient::mainClient()->appId().data());
}


void ScriptingPart::slotScriptAction()
{
    QString ident = QString::fromLatin1(sender()->name());
    kdDebug(9011) << "Action " << ident << " activated" << endl;

    PyObject *arglist = Py_BuildValue((char*)"()");
    PyEval_CallObject(actions[ident], arglist);
}


extern "C" {

    static PyObject *kdevelopc_addMenuItem(PyObject */*self*/, PyObject *args)
    {
        return scripting_part->addMenuItem(args);
    }
    static PyObject *kdevelopc_appId(PyObject */*self*/, PyObject *args)
    {
        return scripting_part->appId(args);
    }

    static struct PyMethodDef kdevelopc_methods[] = {
        { (char*)"appId",       kdevelopc_appId,       METH_VARARGS, NULL },
        { (char*)"addMenuItem", kdevelopc_addMenuItem, METH_VARARGS, NULL },
        { NULL,                 NULL,                0,            NULL }
    };

    static void initkdevelopc()
    {
        (void) Py_InitModule((char*)"kdevelopc", kdevelopc_methods);
    }
}

#include "scriptingpart.moc"
