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
*
* You should have received a copy of the GNU General Public
* License along with this program; if not, write to the
* Free Software Foundation, Inc.,
* 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
*/

#include "clangparsejob.h"

#include <llvm/Support/Host.h>

#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/HeaderSearchOptions.h>
#include <clang/Basic/TargetOptions.h>
#include <clang/Basic/TargetInfo.h>
#include <clang/Basic/FileManager.h>
#include <clang/Basic/SourceManager.h>
#include <clang/Lex/Preprocessor.h>
#include <clang/Basic/Diagnostic.h>
#include <clang/AST/ASTContext.h>
#include <clang/AST/ASTConsumer.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/Parse/Parser.h>
#include <clang/Parse/ParseAST.h>

#include <language/interfaces/iproblem.h>
#include <language/duchain/topducontext.h>
#include <language/duchain/duchain.h>
#include <language/duchain/duchainutils.h>
#include <language/duchain/duchainlock.h>

class KDevDiagnosticConsumer : public clang::DiagnosticConsumer
{
public:
  virtual void HandleDiagnostic(clang::DiagnosticsEngine::Level DiagLevel,
                                const clang::Diagnostic &Info);
  virtual DiagnosticConsumer *clone(clang::DiagnosticsEngine &Diags) const;
};

void KDevDiagnosticConsumer::HandleDiagnostic(clang::DiagnosticsEngine::Level diagLevel,
                                                      const clang::Diagnostic &info)
{
    clang::SourceManager &sm(info.getSourceManager());
    llvm::SmallVector<char, 10> out;
    info.FormatDiagnostic(out);
    
    QString str(QByteArray(out.data(), out.size_in_bytes()));

    KDevelop::ProblemPointer p(new KDevelop::Problem);
    qDebug() << info.getNumRanges();
    clang::SourceLocation location(info.getLocation());
    clang::SourceLocation endLocation(info.getLocation());
    QString bName(KUrl(sm.getBufferName(location)).toLocalFile());
    // TODO correctly calculate range!
    KDevelop::DocumentRange range(KDevelop::IndexedString(bName.toUtf8().constData(), bName.size()), KDevelop::SimpleRange(
        KDevelop::SimpleCursor(sm.getSpellingLineNumber(location) - 1, sm.getSpellingColumnNumber(endLocation) - 1), 0));
    p->setFinalLocation(range);
    p->setDescription(str);
    p->setSource(KDevelop::ProblemData::Parser);
    KDevelop::ProblemData::Severity severity;
    switch (diagLevel) {
        case clang::DiagnosticsEngine::Ignored:
        case clang::DiagnosticsEngine::Note:
            severity = KDevelop::ProblemData::Hint;
            break;
        case clang::DiagnosticsEngine::Warning:
            severity = KDevelop::ProblemData::Warning;
            break;
        case clang::DiagnosticsEngine::Error:
        case clang::DiagnosticsEngine::Fatal:
        default:
            severity = KDevelop::ProblemData::Error;
            break;
    }
    p->setSeverity(severity);

    qDebug() << "parsing error" << p->description() << p->finalLocation() << bName;

    KDevelop::DUChainWriteLocker lock(KDevelop::DUChain::lock());
    KDevelop::ReferencedTopDUContext rTopContext(KDevelop::DUChainUtils::standardContextForUrl(KUrl(bName)));
    rTopContext->addProblem(p);
    lock.unlock();
}

clang::DiagnosticConsumer *KDevDiagnosticConsumer::clone(clang::DiagnosticsEngine &Diags) const
{
    return new KDevDiagnosticConsumer();
}

class MyASTConsumer : public clang::ASTConsumer, public clang::RecursiveASTVisitor<MyASTConsumer>
{
private:
    clang::SourceManager &_sm;
    clang::LangOptions _lo;

    int offsetOf(const clang::SourceLocation &s) const
    {
        return _sm.getFileOffset(s);
    }

    int offsetOfEnd(const clang::SourceLocation &s) const
    {
        return _sm.getFileOffset(clang::Lexer::getLocForEndOfToken(s, 0, _sm, _lo));
    }

public:
    MyASTConsumer(clang::SourceManager &sm, clang::LangOptions &lo) : clang::ASTConsumer(), _sm(sm), _lo(lo) { }
    virtual ~MyASTConsumer() { }

    virtual void HandleTranslationUnit(clang::ASTContext &ctx) {
        TraverseDecl(ctx.getTranslationUnitDecl());
    }

    virtual bool VisitTypeDecl(clang::TypeDecl *decl) {
        //std::cout << "type decl " << decl->getNameAsString() << (void *)decl << std::endl;
        clang::SourceLocation location(decl->getLocation());
        //std::cout << offsetOf(location) << " " << offsetOfEnd(location) << std::endl;
        return clang::RecursiveASTVisitor<MyASTConsumer>::VisitTypeDecl(decl);
    }

    virtual bool VisitVarDecl(clang::VarDecl *decl) {
        //std::cout << "decl " << decl->getNameAsString() << (void *)decl << " " << decl->getType().getAsString() << std::endl;
        clang::SourceLocation location(decl->getLocation());
        //std::cout << offsetOf(location) << " " << offsetOfEnd(location) << std::endl;

        return clang::RecursiveASTVisitor<MyASTConsumer>::VisitVarDecl(decl);
    }

    virtual bool VisitMemberExpr(clang::MemberExpr *expr) {
        //std::cout << "member " << expr->getMemberDecl()->getNameAsString() << std::endl;
        clang::SourceLocation location(expr->getMemberLoc());
        //std::cout << offsetOf(location) << " " << offsetOfEnd(location) << std::endl;

        return clang::RecursiveASTVisitor<MyASTConsumer>::VisitMemberExpr(expr);
    }

    virtual bool VisitDeclRefExpr(clang::DeclRefExpr *expr) {
        //std::cout << "decl ref " << expr->getDecl()->getNameAsString() << (void *)expr->getDecl() << std::endl;
        clang::SourceLocation location(expr->getLocation());
        //std::cout << offsetOf(location) << " " << offsetOfEnd(location) << std::endl;

        return clang::RecursiveASTVisitor<MyASTConsumer>::VisitDeclRefExpr(expr);
    }
/*
    virtual bool VisitStmt(clang::Stmt *stmt) {
        stmt->dump();
        return clang::RecursiveASTVisitor<MyASTConsumer>::VisitStmt(stmt);
    }
    */
/*
    virtual bool VisitTypeLoc(clang::TypeLoc tloc) {
        std::cout << "typeloc " << tloc.getType().getAsString() << std::endl;
        return clang::RecursiveASTVisitor<MyASTConsumer>::VisitTypeLoc(tloc);
    }
*/
    virtual bool VisitValue(clang::ValueDecl *val) {
        std::cout << "value" << std::endl;
        return true;
    }
};

class CLangParseJobPrivate {
public:
    CLangParseJobPrivate(const QString &f);
    void run();

    clang::CompilerInstance ci;
    clang::LangOptions &lo;
    clang::HeaderSearchOptions &so;
    KUrl url;
};

CLangParseJobPrivate::CLangParseJobPrivate (const QString& f) : ci(), lo(ci.getLangOpts()), so(ci.getHeaderSearchOpts()), url(f)
{
    ci.createDiagnostics(0, 0, new KDevDiagnosticConsumer());

    clang::TargetOptions to;
    to.Triple = llvm::sys::getDefaultTargetTriple();
    ci.setTarget(clang::TargetInfo::CreateTargetInfo(ci.getDiagnostics(), to));

    lo.C99 = 1;
    lo.GNUMode = 0;
    lo.GNUKeywords = 1;
    lo.CPlusPlus = 0;
    lo.CPlusPlus0x = 0;
    lo.Bool = 0;
    lo.NoBuiltin = 0;

    so.UseBuiltinIncludes = true;
    //so.UseStandardCXXIncludes = true;
    //so.UseStandardSystemIncludes = true;
    //so.ResourceDir = "/usr/include";
    so.AddPath("/usr/lib/clang/3.1/include/", clang::frontend::Angled, false, false, false);
    so.AddPath("/usr/include/", clang::frontend::Angled, false, false, false);
    so.AddPath("/usr/include/linux", clang::frontend::Angled, false, false, false);
    //so.AddPath("/usr/include/c++/4.7.1", clang::frontend::Angled, false, false, false);
    //so.AddPath("/usr/include/c++/4.7.1/tr1", clang::frontend::Angled, false, false, false);
    //so.AddPath("/usr/include/c++/4.7.1/x86_64-unknown-linux-gnu", clang::frontend::Angled, false, false, false);

    ci.createFileManager();
    ci.createSourceManager(ci.getFileManager());
    ci.createPreprocessor();
    ci.createASTContext();

    ci.getPreprocessorOpts().UsePredefines = true;

    clang::ASTConsumer *astConsumer = new MyASTConsumer(ci.getSourceManager(), lo);
    ci.setASTConsumer(astConsumer);

    const clang::FileEntry *pFile = ci.getFileManager().getFile(f.toUtf8().constData());
    ci.getSourceManager().createMainFileID(pFile);
}

void CLangParseJobPrivate::run()
{
    KDevelop::DUChainWriteLocker lock(KDevelop::DUChain::lock());
    KDevelop::ReferencedTopDUContext rTopContext(KDevelop::DUChainUtils::standardContextForUrl(url));
    if (!rTopContext.data()) {
        KDevelop::TopDUContext* topContext;
        topContext = new KDevelop::TopDUContext(KDevelop::IndexedString(url.toLocalFile()), KDevelop::RangeInRevision(KDevelop::CursorInRevision(0, 0), KDevelop::CursorInRevision(1000, 0)));
        KDevelop::DUChain::self()->addDocumentChain(topContext);
        rTopContext = KDevelop::DUChainUtils::standardContextForUrl(url);
    }
    rTopContext->clearProblems();
    lock.unlock();

    clang::ASTConsumer &astConsumer = ci.getASTConsumer();
    ci.getDiagnosticClient().BeginSourceFile(ci.getLangOpts(),
                                             &ci.getPreprocessor());
    clang::ParseAST(ci.getPreprocessor(), &astConsumer, ci.getASTContext());
    ci.getDiagnosticClient().EndSourceFile();
}


CLangParseJob::CLangParseJob (const KUrl& url) : ParseJob (url)
{
    d = new CLangParseJobPrivate(url.toLocalFile());
    qDebug() << "CLANG OK!";
}

CLangParseJob::~CLangParseJob()
{
    delete d;
    qDebug() << "CLANG DEST OK!";
}

void CLangParseJob::run()
{
    d->run();
}


#include "clangparsejob.moc"
