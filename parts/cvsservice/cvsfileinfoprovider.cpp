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

#include <qregexp.h>
#include <kurl.h>
#include <kdebug.h>

#include <urlutil.h>
#include <kdevproject.h>

#include <dcopref.h>
#include <cvsjob_stub.h>
#include <cvsservice_stub.h>

#include "cvspart.h"
#include "cvsdir.h"
#include "cvsentry.h"
#include "cvsfileinfoprovider.h"


///////////////////////////////////////////////////////////////////////////////
// class CVSFileInfoProvider
///////////////////////////////////////////////////////////////////////////////

CVSFileInfoProvider::CVSFileInfoProvider( CvsServicePart *parent, CvsService_stub *cvsService )
    : KDevVCSFileInfoProvider( parent ), m_requestStatusJob( 0 ), m_cvsService( cvsService )
{
}

///////////////////////////////////////////////////////////////////////////////

CVSFileInfoProvider::~CVSFileInfoProvider()
{
}

///////////////////////////////////////////////////////////////////////////////

VCSFileInfoMap CVSFileInfoProvider::status( const QString &dirPath ) const
{
    // @todo
    CVSDir cvsdir( projectDirectory() + QDir::separator() + dirPath );
    // Returns an empty map if dir is invalid
    return cvsdir.dirStatus();
}

///////////////////////////////////////////////////////////////////////////////

bool CVSFileInfoProvider::requestStatus( const QString &dirPath, void *callerData )
{
    m_savedCallerData = callerData;
    // @todo
    if (m_requestStatusJob)
    {
        delete m_requestStatusJob;
        m_requestStatusJob = 0;
    }

    DCOPRef job = m_cvsService->status( dirPath, false, false );
    m_requestStatusJob = new CvsJob_stub( job.app(), job.obj() );

    kdDebug() << "Running command : " << m_requestStatusJob->cvsCommand() << endl;
    connectDCOPSignal( job.app(), job.obj(), "jobExited(bool, int)", "slotJobExited(bool, int)", true );
//    connectDCOPSignal( job.app(), job.obj(), "receivedStdout(QString)", "slotReceivedOutput(QString)", true );
    return m_requestStatusJob->execute();
}

///////////////////////////////////////////////////////////////////////////////

void CVSFileInfoProvider::slotJobExited( bool normalExit, int exitStatus )
{
    kdDebug(9017) << "CVSFileInfoProvider::slotJobExited(bool,int)" << endl;
    if (!normalExit)
        return;

    kdDebug(9017) << m_requestStatusJob->output() << endl;

    emit statusReady( parse( m_requestStatusJob->output() ), m_savedCallerData );
}

///////////////////////////////////////////////////////////////////////////////

void CVSFileInfoProvider::slotReceivedOutput( QString someOutput )
{
}

///////////////////////////////////////////////////////////////////////////////

void CVSFileInfoProvider::slotReceivedErrors( QString someErrors )
{
}

///////////////////////////////////////////////////////////////////////////////

QString CVSFileInfoProvider::projectDirectory() const
{
    return owner()->project()->projectDirectory();
}

///////////////////////////////////////////////////////////////////////////////

QMap<QString, VCSFileInfo> CVSFileInfoProvider::parse( QStringList stringStream )
{
    QRegExp rx_recordStart( "^=+$" );
    QRegExp rx_fileName( "^\\b(File: (\\.|-|\\w)+)\\b" );
    QRegExp rx_fileStatus( "(Status: (\\.|-|\\s|\\w)+)" );
    QRegExp rx_fileWorkRev( "^(\\s+Working revision:\\W+(\\d+\\.?)+)$" );
    QRegExp rx_fileRepoRev( "^\\s+(Repository revision:\\W+(\\d+\\.?)+)" );
    //QRegExp rx_stickyTag( "\\s+(Sticky Tag:\\W+(w+|\\(none\\)))" );
    //QRegExp rx_stickyDate( "" ); // @todo but are they useful?? :-/
    //QRegExp rx_stickyOptions( "" ); //@todo

    QString fileName,
        fileStatus,
        workingRevision,
        repositoryRevision,
        stickyTag,
        stickyDate,
        stickyOptions;

    QMap<QString, VCSFileInfo> vcsStates;

    int state = 0,
        lastAcceptableState = 4;

    for (QStringList::const_iterator it=stringStream.begin(); it != stringStream.end(); ++it)
    {
        const QString &s = (*it);

        //qDebug( s );

        if (state == 0 && rx_recordStart.exactMatch( s ))
            ++state;
        else if (state == 1 && rx_fileName.search( s ) >= 0 && rx_fileStatus.search( s ) >= 0)    // FileName
        {
            fileName = rx_fileName.cap();
            fileStatus = rx_fileStatus.cap();

            fileName = fileName.replace( "File:", "" ).stripWhiteSpace();
            fileStatus = fileStatus.replace( "Status:", "" ).stripWhiteSpace();
            ++state; // Next state
        }
        else if (state == 2 && rx_fileWorkRev.search( s ) >= 0)
        {
            workingRevision = rx_fileWorkRev.cap();
            workingRevision = workingRevision.replace( "Working revision:", "" ).stripWhiteSpace();
            ++state;
        }
        else if (state == 3 && rx_fileRepoRev.search( s ) >= 0)
        {
            repositoryRevision = rx_fileRepoRev.cap();
            repositoryRevision = repositoryRevision.replace( "Repository revision:", "" ).stripWhiteSpace();
            ++state; // Reset so we skip all other stuff for this record
        }
/*
        else if (state == 4 && rx_stickyTag.search( s ) >= 0)
        {
            stickyTag = rx_stickyTag.cap();
            ++state;
        }
*/
        else if (state >= lastAcceptableState)
        {
            // Package stuff and put into map
            VCSFileInfo vcsInfo(
                fileName, workingRevision, repositoryRevision,
                String2EnumState( fileStatus ) );

            kdDebug(9017) << vcsInfo.toString() << endl;;

            vcsStates.insert( fileName, vcsInfo );
            state = 0;
        }
    }
    return vcsStates;
}

///////////////////////////////////////////////////////////////////////////////

VCSFileInfo::FileState CVSFileInfoProvider::String2EnumState( QString stateAsString )
{
    if (stateAsString == "Up-to-date")
        return VCSFileInfo::Uptodate;
    if (stateAsString == "Locally Modified")
        return VCSFileInfo::Modified;
    if (stateAsString == "Locally Added")
        return VCSFileInfo::Added;
    else
        return VCSFileInfo::Unknown;
}


#include "cvsfileinfoprovider.moc"
