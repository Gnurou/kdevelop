// Here resides the C/C++ code completion parser


/***************************************************************************
                          cppcodecompletionparser.cpp  -  description
                             -------------------
	begin		: Fri Aug 3 21:10:00 CEST 2001
	copyright	: (C) 2001 by Daniel Haberkorn, Victor R�der
	email		: DHaberkorn@GMX.de, Victor_Roeder@GMX.de
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include <kdebug.h>

#include "cppcodecompletionparser.h"

CppCodeCompletionParser::CppCodeCompletionParser ( KEditor::EditDocumentIface* pEditIface, ClassStore* pStore )
{
	m_pEditIface = pEditIface;
	m_pStore = pStore;
}

CppCodeCompletionParser::~CppCodeCompletionParser()
{

}

QString CppCodeCompletionParser::getCompletionText ( int nCol )
{
	int nOffset = nCol;
	QString strCompletionText;

	while ( nOffset > 0 )
	{
		if ( m_strCurLine[nOffset] == '.' ||
			m_strCurLine[nOffset] == '>' && m_strCurLine[nOffset - 1] == '-' ||
			m_strCurLine[nOffset] == ':' && m_strCurLine[nOffset - 1] == ':' ||
			m_strCurLine[nOffset] == ' ' || m_strCurLine[nOffset] == ';' )
		{
			nOffset++;
			break;
		}
		else
		{
			//kdDebug ( 9007 ) << "Offset--;" << endl;
			nOffset--;
		}

		if ( m_strCurLine[nOffset] == ':' && m_strCurLine[nOffset - 1] != ':' ||
			m_strCurLine[nOffset] == '-' && m_strCurLine[nOffset - 1] != '>' )
		{
			return "";
		}
	}

	if ( ( nCol - nOffset ) >= 0 )
		strCompletionText = m_strCurLine.mid ( nOffset, ( nCol - nOffset ) );

	//kdDebug ( 9007 ) << "nCol - nOffset: " << (nCol - nOffset) << endl;

	return strCompletionText;
}

int CppCodeCompletionParser::getNodePos ( int nCol )
{
	int nOffset = 0;
	int nNodePos = 0;

	while ( nOffset < nCol )
	{
		if ( m_strCurLine[nOffset] == '.' ||
			m_strCurLine[nOffset] == '-' && m_strCurLine[nOffset + 1] == '>' ||
			m_strCurLine[nOffset] == ':' && m_strCurLine[nOffset + 1] == ':' )
		{
			nNodePos++;
		}

		nOffset++;
	}

	return nNodePos;
}

QString CppCodeCompletionParser::getNodeText ( int nNode )
{
	if ( nNode <= 0 )
		return "";

	int nFrom = 0;
	int nTo = 0;
	int nNodePos = 0;

	while ( nTo < m_strCurLine.length() )
	{
		if ( m_strCurLine[nTo] == '.' )
		{
			nNodePos++;

			if ( nNodePos < nNode )
				nFrom = nTo + 1;
		}

		if ( m_strCurLine[nTo] == '-' && m_strCurLine[nTo + 1] == '>' ||
			m_strCurLine[nTo] == ':' && m_strCurLine[nTo + 1] == ':' )
		{
			nNodePos++;

			if ( nNodePos < nNode )
				nFrom = nTo + 2;
		}

		//kdDebug ( 9007 ) << "nNodePos: " << nNodePos << " nNode: " << nNode << endl;

		if ( nNodePos == nNode )
		{
			for ( nTo = nFrom; nTo < m_strCurLine.length(); nTo++ )
			{
				if ( m_strCurLine[nTo] == '.' )
				{
					return m_strCurLine.mid ( nFrom, ( nTo - nFrom ) );
				}

				if ( m_strCurLine[nTo] == '-' && m_strCurLine[nTo + 1] == '>' ||
					m_strCurLine[nTo] == ':' && m_strCurLine[nTo + 1] == ':' )
				{
					return m_strCurLine.mid ( nFrom, ( nTo - nFrom ) );
				}
			}
		}

		nTo++;

		//kdDebug ( 9007 ) << "getNodeText::while" << endl;
	}

	return "";
}

QString CppCodeCompletionParser::getNodeDelimiter ( int nNode )
{
	if ( nNode <= 0 )
		return "";

	int nFrom = 0;
	int nTo = 0;
	int nNodePos = 0;

	while ( nTo < m_strCurLine.length() )
	{
		if ( m_strCurLine[nTo] == '.' ||
			m_strCurLine[nTo] == '-' && m_strCurLine[nTo + 1] == '>' ||
			m_strCurLine[nTo] == ':' && m_strCurLine[nTo + 1] == ':' )
		{
			nNodePos++;

			if ( nNodePos < nNode )
				nFrom = nTo + 1;
		}

		//kdDebug ( 9007 ) << "nNodePos: " << nNodePos << " nNode: " << nNode << endl;

		if ( nNodePos == nNode )
		{
			for ( nTo = nFrom; nTo < m_strCurLine.length(); nTo++ )
			{
				if ( m_strCurLine[nTo] == '.' )
				{
					return m_strCurLine.mid ( nTo, 1 );
				}

				if ( m_strCurLine[nTo] == '-' && m_strCurLine[nTo + 1] == '>' ||
					m_strCurLine[nTo] == ':' && m_strCurLine[nTo + 1] == ':' )
				{
					return m_strCurLine.mid ( nTo, 2 );
				}
			}
		}

		nTo++;

		//kdDebug ( 9007 ) << "getNodeText::while" << endl;
	}

	return "";
}

QString CppCodeCompletionParser::getTypeOfObject ( const QString& strObject, int nLine )
{
	return "";
}

QString CppCodeCompletionParser::getReturnTypeOfMethod ( const QString& strMethod )
{
	return "";
}

QValueList<KEditor::CompletionEntry> CppCodeCompletionParser::getEntryListForClass ( const QString& strClass )
{
	QValueList<KEditor::CompletionEntry> list;

	return list;
}

QValueList<KEditor::CompletionEntry> CppCodeCompletionParser::getEntryListForNamespace ( const QString& strNamespace )
{
	QValueList<KEditor::CompletionEntry> list;

	return list;
}

QValueList<KEditor::CompletionEntry> CppCodeCompletionParser::getEntryListForStruct ( const QString& strStruct )
{
	QValueList<KEditor::CompletionEntry> list;

	return list;
}

bool CppCodeCompletionParser::isMethod ( const QString& strNodeText )
{
	return false;
}

bool CppCodeCompletionParser::isObject ( const QString& strNodeText )
{
	return false;
}

bool CppCodeCompletionParser::isClass ( const QString& strNodeText )
{
	return false;
}

bool CppCodeCompletionParser::isStruct ( const QString& strNodeText )
{
	return false;
}

bool CppCodeCompletionParser::isNamespace ( const QString& strNodeText )
{
	return false;
}
