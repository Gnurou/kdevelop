/***************************************************************************
 *   Copyright (C) 2003 by Mario Scalas                                    *
 *   mario.scalas@libero.it                                                *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#ifndef CVSPARTIMPL_H
#define CVSPARTIMPL_H

#include <qobject.h>
#include <qstringlist.h>
#include <qguardedptr.h>
#include <kurl.h>

class CvsPart;
class KDialogBase;
class KURL;
class KURL::List;
class CvsProcessWidget;
class KDevMainWindow;
class KDevCore;
class KDevDiffFrontend;

// Available Cvs operations
enum CvsOperation
{
    opAdd, opCommit, opUpdate, opRevert, opRemove, opLog, opDiff, opTag, opUnTag
};

/**
* @short This is the base class for implementation of the core service.
*
* This is an attempt to separate the container part (CvsPart) and its implementation
* for reducing code complexity for module (cvspart.{h,cpp} was becoming too
* cumbersome). So a CvsPart can have several implementations, one directly wrapping
* 'cvs' command and another using cervisia's cvsservice.
*
* @author Mario Scalas
*/
class CvsPartImpl : public QObject
{
    Q_OBJECT
public:
    /**
    * Costructor
    * @param part the CvsPart component
    * @param name
    */
    CvsPartImpl( CvsPart *part, const char *name=0 );
    /**
    * Destructor
    */
    virtual ~CvsPartImpl();

    /**
    * Do login into repository. The component will show a dialog requesting the
    * needed data to the user.
    */
    virtual void login() = 0;
    /**
    * Do logout. Of course one must be logged into repository first ;-)
    */
    virtual void logout() = 0;
    /**
    * Do checkout of module from some remote directory. Requested data will be
    * collected here.
    */
    virtual void checkout() = 0;
    /**
    * Commit the specified files (as KURL) to repository.
    * @param urlList
    */
    virtual void commit( const KURL::List& urlList ) = 0;
    /**
    * Update the specified files (as KURL): files will be
    * updated if not locally modified.
    * @param urlList
    */
    virtual void update( const KURL::List& urlList ) = 0;
    /**
    * Add the specified files (as KURL) to repository.
    * @param urlList
    */
    virtual void add( const KURL::List& urlList, bool binary = false ) = 0;
    /**
    * Remove the specified files (as KURL) from repository.
    * @param urlList
    */
    virtual void remove( const KURL::List& urlList ) = 0;
    /**
    * Revert the specified files (as KURL) with the most recent version
    * present in repository.
    * @param urlList
    */
    virtual void revert( const KURL::List& urlList ) = 0;
    /**
    * Produce a log of changes about the specified files.
    * @param urlList
    */
    virtual void log( const KURL::List& urlList ) = 0;
    /**
    * Produce a diff of the the specified files (as KURL). The diff could
    * be displayed in the diff frontend or in an ad-hoc container.
    * @param urlList
    */
    virtual void diff( const KURL::List& urlList ) = 0;
    /**
    * Tag the specified files (as KURL) with a release or branch tag.
    * @param urlList
    */
    virtual void tag( const KURL::List& urlList ) = 0;
    /**
    * Remove tag from the specified files (as KURL) in repository.
    * @param urlList
    */
    virtual void unTag( const KURL::List& urlList ) = 0;
    /**
    * Add the specified files (as KURL) to the .cvsignore file.
    * DSDSDS
    * @param urlList
    */
    virtual void addToIgnoreList( const KURL::List& urlList ) = 0;
    /**
    * Commit the specified files (as KURL) to repository.
    * @param urlList
    */
    virtual void removeFromIgnoreList( const KURL::List& urlList ) = 0;
    /**
    * Creates a new project with cvs support, that is will import the
    * generated sources in the repository.
    * @param dirName path to project directory on local system
    * @param cvsRsh value for the CVS_RSH env var (for accessing :ext:
    *        repositories)
    */
    virtual void createNewProject( const QString &dirName,
        const QString &cvsRsh, const QString &location,
        const QString &message, const QString &module, const QString &vendor,
        const QString &release, bool mustInitRoot ) = 0;

// Helpers
public:
    /**
    * @return a reference to the process widget: many worker methods
    * display their output in it and the CvsPart will embed it in the
    * bottom embedded view.
    */
    CvsProcessWidget *processWidget() const;

signals:
    void warning( const QString &msg );
    // Emitted when the component has terminated checkout operation
    // @param checkedDir directory where the module has been checked out
    //        (will be empty if the operation failed)
    void checkoutFinished( QString checkedDir );

// Methods
protected:
    /**
    * Call this every time a slot for cvs operations starts!! (It will setup the
    * state (file/dir URL, ...).
    * It will also display proper error messages so the caller must only exit if
    * it fails (return false); if return true than basic requisites for cvs operation
    * are satisfied.
    * @return true and the valid URLs paths in m_fileList if the operation can be performed,
    *         false otherwise.
    */
    virtual bool prepareOperation( const KURL::List &someUrls, CvsOperation op );
    /**
    * Call this every time a slot for cvs operations ends!! (It will restore the state for a new
    * operation).
    */
    virtual void doneOperation();
    /**
    *   @return true if the @p url is present in CVS/Entry file
    */
    static bool isRegisteredInRepository( const QString &projectDirectory, const KURL &url );
    /*
    * Ideally this function will take a bunch of URLs and validate them (they are valid files,
    * are files registered in CVS, are on a supported filesystem, ...). Currently checks
    * only for files belonging to the repository ;)
    * param @projectDirectory
    * param @urls list of KURL to check (the list can be modified during the operation)
    * parap @op type of cvs operation, as pecified in @see CvsOperation enum
    */
    static void validateURLs( const QString &projectDirectory, KURL::List &urls, CvsOperation op );

    /*
    * Add file(s) to their respective ignore list. This means that, for example, if you
    * add '/home/mario/src/myprj/mylib/module1/bad.cpp' then the string 'bad.cpp' will be
    * appended to file '/home/mario/src/myprj/mylib/module1/.cvsignore'.
    * param @projectDirectory
    * param @urls list of urls to be added to the check list.
    */
    static void addToIgnoreList( const QString &projectDirectory, const KURL &url );
    static void addToIgnoreList( const QString &projectDirectory, const KURL::List &urls );

    /*
    * Remove file(s) from their respective .ignore files. As specified for @see addToIgnoreList
    * function, this means that, for example, if you remove '/home/mario/src/myprj/mylib/module1/bad.cpp'
    * then a search for the string 'bad.cpp' will be performed on file
    * '/home/mario/src/myprj/mylib/module1/.cvsignore': if found, it will be removed, otherwise
    * nothing will be removed.
    * param @projectDirectory
    * param @urls list of urls to be removed from the check list.
    */
    static void removeFromIgnoreList( const QString &projectDirectory, const KURL &url );
    static void removeFromIgnoreList( const QString &projectDirectory, const KURL::List &urls );

    KDevMainWindow *mainWindow() const;
    KDevCore *core() const;
    QString projectDirectory() const;
    KDevDiffFrontend *diffFrontend() const;

// Data members
protected:
    // File name for the changelog file
    static const QString changeLogFileName;
    // Four spaces for every log line (except the first which includes the
    // developers name)
    static const QString changeLogPrependString;

    CvsPart *m_part;

    // Reference to widget integrated in the "bottom tabbar" (IDEAL)
    // (_Must_ be initialized by derived class)
    QGuardedPtr<CvsProcessWidget> m_widget;

    // These are the file path (relative to project directory) of the files the operation
    // has been requested for.
    QStringList m_fileList;
};

#endif
