/*
* This file is part of KDevelop
*
* Copyright 2012 Alexandre Courbot <gnurou@gmail.com>
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU Library General Public License as
* published by the Free Software Foundation; either version 2 of the
* License, or (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*/

#include "clangdiagnostics.h"

#include <clang/Basic/SourceManager.h>
#include <clang/Lex/Lexer.h>

#include <language/duchain/indexedstring.h>
#include <language/interfaces/iproblem.h>
#include <language/duchain/duchainlock.h>
#include <language/duchain/topducontext.h>
#include <language/duchain/duchain.h>
#include <language/duchain/duchainutils.h>
#include <KUrl>

using namespace KDevelop;

void KDevDiagnosticConsumer::HandleDiagnostic(clang::DiagnosticsEngine::Level diagLevel, const clang::Diagnostic& info)
{
    ProblemData::Severity severity;
    switch (diagLevel) {
        case clang::DiagnosticsEngine::Ignored:
            severity = ProblemData::Hint;
            break;
        case clang::DiagnosticsEngine::Note:
            // TODO Just ignore notes for now - it would be nice to handle them later
            return;
        case clang::DiagnosticsEngine::Warning:
            severity = ProblemData::Warning;
            break;
        case clang::DiagnosticsEngine::Error:
        case clang::DiagnosticsEngine::Fatal:
        default:
            severity = ProblemData::Error;
            break;
    }

    clang::SourceManager& sm(info.getSourceManager());
    llvm::SmallVector<char, 10> out;
    info.FormatDiagnostic(out);
    
    QString str(QByteArray(out.data(), out.size_in_bytes()));

    clang::SourceLocation location(info.getLocation());
    ProblemPointer p(new Problem);
    p->setDescription(str);
    p->setSource(ProblemData::Parser);
    p->setSeverity(severity);

    // TODO handle ranges!
    unsigned int line = sm.getSpellingLineNumber(location);
    unsigned int beginColumn = sm.getSpellingColumnNumber(location);
    unsigned int endColumn = sm.getSpellingColumnNumber(clang::Lexer::getLocForEndOfToken(location, 0, sm, lo));

    DocumentRange range(_url, SimpleRange(SimpleCursor(line - 1, beginColumn - 1), endColumn - beginColumn));
    p->setFinalLocation(range);

    qDebug() << "parsing error" << p->description() << p->finalLocation() << _url.toUrl();

    DUChainWriteLocker lock(DUChain::lock());
    ReferencedTopDUContext rTopContext(DUChainUtils::standardContextForUrl(_url.toUrl()));
    if (rTopContext)
        rTopContext->addProblem(p);
    else
        qDebug() << "No top context to report error to!";
    lock.unlock();
}

clang::DiagnosticConsumer *KDevDiagnosticConsumer::clone(clang::DiagnosticsEngine &Diags) const
{
    return new KDevDiagnosticConsumer(lo, _url);
}

