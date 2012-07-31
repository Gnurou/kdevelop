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

#include <interfaces/ilanguage.h>
#include <language/interfaces/iproblem.h>
#include <language/interfaces/icodehighlighting.h>
#include <language/interfaces/ilanguagesupport.h>
#include <language/duchain/topducontext.h>
#include <language/duchain/duchain.h>
#include <language/duchain/duchainutils.h>
#include <language/duchain/duchainlock.h>
#include <language/backgroundparser/urlparselock.h>
#include <languages/cpp/cpplanguagesupport.h>
#include <languages/cpp/cppduchain/environmentmanager.h>
#include <language/duchain/builders/abstractcontextbuilder.h>
#include <language/duchain/builders/abstracttypebuilder.h>
#include <language/duchain/builders/abstractdeclarationbuilder.h>
#include <language/duchain/builders/abstractusebuilder.h>

using namespace KDevelop;

class KDevDiagnosticConsumer : public clang::DiagnosticConsumer
{
public:
    KDevDiagnosticConsumer(clang::LangOptions _lo) : lo(_lo) {}
  virtual void HandleDiagnostic(clang::DiagnosticsEngine::Level DiagLevel,
                                const clang::Diagnostic &Info);
  virtual DiagnosticConsumer *clone(clang::DiagnosticsEngine &Diags) const;

  clang::LangOptions lo;
};

void KDevDiagnosticConsumer::HandleDiagnostic(clang::DiagnosticsEngine::Level diagLevel,
                                                      const clang::Diagnostic &info)
{
    clang::SourceManager &sm(info.getSourceManager());
    llvm::SmallVector<char, 10> out;
    info.FormatDiagnostic(out);
    
    QString str(QByteArray(out.data(), out.size_in_bytes()));

    ProblemPointer p(new Problem);
    qDebug() << info.getNumRanges();
    clang::SourceLocation location(info.getLocation());
    QString bName(KUrl(sm.getBufferName(location)).toLocalFile());
    p->setDescription(str);
    p->setSource(ProblemData::Parser);
    ProblemData::Severity severity;
    switch (diagLevel) {
        case clang::DiagnosticsEngine::Ignored:
        case clang::DiagnosticsEngine::Note:
            severity = ProblemData::Hint;
            break;
        case clang::DiagnosticsEngine::Warning:
            severity = ProblemData::Warning;
            break;
        case clang::DiagnosticsEngine::Error:
        case clang::DiagnosticsEngine::Fatal:
        default:
            severity = ProblemData::Error;
            break;
    }
    p->setSeverity(severity);

    // TODO correctly calculate range!
    if (info.getNumRanges() > 0) {
        clang::SourceLocation begLocation(info.getRange(0).getBegin());
        clang::SourceLocation endLocation(clang::Lexer::getLocForEndOfToken(info.getRange(0).getEnd(), 0, sm, lo));

        DocumentRange range(IndexedString(bName.toUtf8().constData(), bName.size()), SimpleRange(SimpleCursor(sm.getSpellingLineNumber(begLocation) - 1, sm.getSpellingColumnNumber(begLocation) - 1),
            SimpleCursor(sm.getSpellingLineNumber(endLocation) - 1, sm.getSpellingColumnNumber(endLocation) - 1)));
        p->setFinalLocation(range);
    } else {
        DocumentRange range(IndexedString(bName.toUtf8().constData(), bName.size()), SimpleRange(SimpleCursor(sm.getSpellingLineNumber(location) - 1, sm.getSpellingColumnNumber(clang::Lexer::getLocForEndOfToken(location, 0, sm, lo)) - 1), 0));
        p->setFinalLocation(range);
    }

    qDebug() << "parsing error" << p->description() << p->finalLocation() << bName;

    DUChainWriteLocker lock(DUChain::lock());
    ReferencedTopDUContext rTopContext(DUChainUtils::standardContextForUrl(KUrl(bName)));
    if (rTopContext) rTopContext->addProblem(p);
    else qDebug() << "No top context to report error to!";
    lock.unlock();
}

clang::DiagnosticConsumer *KDevDiagnosticConsumer::clone(clang::DiagnosticsEngine &Diags) const
{
    return new KDevDiagnosticConsumer(lo);
}

/* TODO horrible. But easiest way for now to share between declarations and uses builders */
QMap<clang::Decl *, DUContext *> declMap;

class CLangContextBuilder : public AbstractContextBuilder<clang::Decl, clang::NamedDecl>
{
public:
    CLangContextBuilder() {}
    CLangContextBuilder(clang::SourceManager &sm, clang::LangOptions &lo) : _sm(&sm), _lo(lo) { }

    virtual ~CLangContextBuilder() { }

    CursorInRevision toCursor(const clang::SourceLocation &sl)
    {
        return CursorInRevision(_sm->getSpellingLineNumber(sl) - 1, _sm->getSpellingColumnNumber(sl) - 1);
    }

    clang::SourceLocation endOf(const clang::SourceLocation &sl)
    {
        return clang::Lexer::getLocForEndOfToken(sl, 0, *_sm, _lo);
    }

    virtual void setContextOnNode(clang::Decl* node, DUContext* context)
    {
        qDebug() << "setContextOnNode" << node;
        declMap[node] = context;
    }

    virtual DUContext* contextFromNode(clang::Decl* node)
    {
        qDebug() << "contextFromNode" << node << declMap.contains(node);
        if (!declMap.contains(node)) return 0;
        else return declMap[node];
    }

    virtual RangeInRevision editorFindRange(clang::Decl* fromNode, clang::Decl* toNode)
    {
        CursorInRevision fBegin(toCursor(fromNode->getLocStart()));
        CursorInRevision fEnd(toCursor(endOf(toNode->getLocEnd())));
        return RangeInRevision(fBegin, fEnd);
    }

    virtual QualifiedIdentifier identifierForNode(clang::NamedDecl* node)
    {
        if (!node) return QualifiedIdentifier();
        else return QualifiedIdentifier(QString(node->getQualifiedNameAsString().c_str()));
    }

protected:
    KUrl _url;
    clang::SourceManager *_sm;
    clang::LangOptions _lo;
};

class CLangDeclBuilder :
    public AbstractDeclarationBuilder<clang::Decl, clang::NamedDecl, CLangContextBuilder>,
    public clang::ASTConsumer, public clang::RecursiveASTVisitor<CLangDeclBuilder>
{
public:
    CLangDeclBuilder(const KUrl &url, clang::SourceManager &sm, clang::LangOptions &lo) { _url = url; _sm = &sm; lo = _lo; }
    virtual ~CLangDeclBuilder() { }

    virtual void HandleTranslationUnit(clang::ASTContext &ctx)
    {
        clang::Decl *tuDecl = ctx.getTranslationUnitDecl();
        qDebug() << "Start parsing" << _url.toLocalFile();
        build(IndexedString(_url.toLocalFile()), tuDecl);
    }

    /// Should only be called once on the root node
    virtual void startVisiting(clang::Decl* node)
    {
        qDebug() << "startVisiting called!\n";
        TraverseDecl(node);
    }

    virtual bool VisitTypeDecl(clang::TypeDecl *decl);
    virtual bool VisitVarDecl(clang::VarDecl *decl);
    virtual bool VisitMemberExpr(clang::MemberExpr *expr);
    virtual bool VisitDeclRefExpr(clang::DeclRefExpr *expr);
    virtual bool TraverseFunctionDecl(clang::FunctionDecl *func);
    virtual bool VisitValue(clang::ValueDecl *val);
};

/**
 * Register new type
 */
bool CLangDeclBuilder::VisitTypeDecl(clang::TypeDecl *decl)
{
    clang::SourceLocation location(decl->getLocation());
    CursorInRevision tBegin(toCursor(decl->getLocStart()));
    CursorInRevision tEnd(toCursor(endOf(decl->getLocEnd())));
    CursorInRevision nBegin(toCursor(decl->getLocation()));
    CursorInRevision nEnd(toCursor(endOf(decl->getLocation())));
    qDebug() << "type" << decl->getNameAsString().c_str() << tBegin.line << tBegin.column << tEnd.line << tEnd.column << nBegin.line << nBegin.column << nEnd.line << nEnd.column;

    return clang::RecursiveASTVisitor<CLangDeclBuilder>::VisitTypeDecl(decl);
}

/**
 * Variable declaration within the current context
 */
bool CLangDeclBuilder::VisitVarDecl(clang::VarDecl *decl)
{
    CursorInRevision vBegin(toCursor(decl->getLocation()));
    CursorInRevision vEnd(toCursor(endOf(decl->getLocation())));
    RangeInRevision nRange(vBegin, vEnd);
    qDebug() << "var" << decl->getNameAsString().c_str() << decl->getType().getAsString().c_str() << vBegin.line << vBegin.column << vEnd.line << vEnd.column;

    DUChainWriteLocker lock(DUChain::lock());
    DeclarationPointer kDecl(openDeclaration<Declaration>(QualifiedIdentifier(decl->getQualifiedNameAsString().c_str()), nRange, DeclarationIsDefinition));
    //currentContext()->createUse(currentContext()->topContext()->indexForUsedDeclaration(kDecl.data()), nRange);
    lock.unlock();

    bool ret = clang::RecursiveASTVisitor<CLangDeclBuilder>::VisitVarDecl(decl);

    closeDeclaration();
    return ret;
}

/**
 * Member access (e.g. type.member)
 */
bool CLangDeclBuilder::VisitMemberExpr(clang::MemberExpr *expr)
{
    clang::SourceLocation location(expr->getMemberLoc());

    return clang::RecursiveASTVisitor<CLangDeclBuilder>::VisitMemberExpr(expr);
}

bool CLangDeclBuilder::VisitDeclRefExpr(clang::DeclRefExpr *expr)
{
    CursorInRevision vBegin(toCursor(expr->getLocation()));
    CursorInRevision vEnd(toCursor(endOf(expr->getLocation())));
    RangeInRevision nRange(vBegin, vEnd);
    clang::Decl *decl = expr->getDecl();

    if (decl->classofKind(clang::Decl::Var)) {
        clang::VarDecl *vDecl = static_cast<clang::VarDecl *>(decl);
        qDebug() << "decl ref" << vDecl->getNameAsString().c_str() << vBegin.line << vBegin.column << vEnd.line << vEnd.column;
        RangeInRevision dRange(toCursor(vDecl->getLocation()), toCursor(endOf(vDecl->getLocation())));
        DUChainWriteLocker lock(DUChain::lock());
        DeclarationPointer kDecl(openDeclaration<Declaration>(QualifiedIdentifier(vDecl->getQualifiedNameAsString().c_str()), dRange, DeclarationIsDefinition));
        currentContext()->createUse(currentContext()->topContext()->indexForUsedDeclaration(kDecl.data()), nRange);
        lock.unlock();
        closeDeclaration();
    } else {
        qDebug() << "decl ref" << decl->getDeclKindName();
    }

    return clang::RecursiveASTVisitor<CLangDeclBuilder>::VisitDeclRefExpr(expr);
}

/**
 * Build and process a new DUContext for the function
 */
bool CLangDeclBuilder::TraverseFunctionDecl(clang::FunctionDecl *func)
{
    CursorInRevision fBegin(toCursor(func->getLocStart()));
    CursorInRevision fEnd(toCursor(endOf(func->getLocEnd())));
    CursorInRevision nBegin(toCursor(func->getLocation()));
    CursorInRevision nEnd(toCursor(endOf(func->getLocation())));
    RangeInRevision nRange(nBegin, nEnd);
    qDebug() << "func" << func->getNameAsString().c_str() << fBegin.line << fBegin.column << fEnd.line << fEnd.column;
    qDebug() << "opening context";
    // TODO if is definition, should pass in 3rd arg
    //FunctionDeclaration *fdecl = openDeclaration<FunctionDeclaration>(func, func);
    
    DUChainWriteLocker lock(DUChain::lock());
    FunctionDeclaration *fdecl = openDeclaration<FunctionDeclaration>(QualifiedIdentifier(func->getQualifiedNameAsString().c_str()), nRange);
    lock.unlock();

    openContext(func, DUContext::Function, func);

    bool ret = clang::RecursiveASTVisitor<CLangDeclBuilder>::TraverseFunctionDecl(func);

    qDebug() << "closing context";
    closeContext();
    closeDeclaration();
    qDebug() << "func" << func->getNameAsString().c_str() << "done!";
    return ret;
}

bool CLangDeclBuilder::VisitValue(clang::ValueDecl *val)
{
    std::cout << "value" << std::endl;
    return true;
}







class CLangParseJobPrivate {
public:
    CLangParseJobPrivate(const KUrl &u);
    void run(CLangParseJob *parent);

    clang::CompilerInstance ci;
    clang::LangOptions &lo;
    clang::HeaderSearchOptions &so;
    KUrl url;
};

CLangParseJobPrivate::CLangParseJobPrivate (const KUrl& u) : ci(), lo(ci.getLangOpts()), so(ci.getHeaderSearchOpts()), url(u)
{
    lo.C99 = 1;
    lo.GNUMode = 0;
    lo.GNUKeywords = 1;
    lo.CPlusPlus = 0;
    lo.CPlusPlus0x = 0;
    lo.Bool = 0;
    lo.NoBuiltin = 0;

    ci.createDiagnostics(0, 0, new KDevDiagnosticConsumer(lo));

    clang::TargetOptions to;
    to.Triple = llvm::sys::getDefaultTargetTriple();
    ci.setTarget(clang::TargetInfo::CreateTargetInfo(ci.getDiagnostics(), to));

    so.UseBuiltinIncludes = true;
    //so.UseStandardCXXIncludes = true;
    //so.UseStandardSystemIncludes = true;
    //so.ResourceDir = "/usr/include";
    so.AddPath("/usr/lib/clang/3.1/include/", clang::frontend::Angled, false, false, false);
    so.AddPath("/usr/include/", clang::frontend::Angled, false, false, false);
    so.AddPath("/usr/include/linux", clang::frontend::Angled, false, false, false);
    //so.AddPath("/usr/include/c++/4.7.1/tr1", clang::frontend::Angled, false, false, false);
    //so.AddPath("/usr/include/c++/4.7.1/x86_64-unknown-linux-gnu", clang::frontend::Angled, false, false, false);

    ci.createFileManager();
    ci.createSourceManager(ci.getFileManager());
    ci.createPreprocessor();
    ci.createASTContext();

    ci.getPreprocessorOpts().UsePredefines = true;

    const clang::FileEntry *pFile = ci.getFileManager().getFile(url.toLocalFile().toUtf8().constData());
    // TODO create MemoryBuffer from file already loaded in KDevelop. See line 534 of cppparsejob.cpp, isOpenInEditor. Tracker allows to get the document.
    // ICore::self()->languageController()->backgroundParser()->trackerForUrl(document())
    // tracker->document->text()
    // Then:
    // ci.getSourceManager().setMainFileID(ci.getSourceManager().createFileIDForMemBuffer())
    ci.getSourceManager().createMainFileID(pFile);
}

void CLangParseJobPrivate::run(CLangParseJob* parent)
{
    /*
    DUChainWriteLocker lock(DUChain::lock());
    ReferencedTopDUContext rTopContext(DUChainUtils::standardContextForUrl(url));
    if (!rTopContext.data()) {
        qDebug() << "Creating new context!";
        TopDUContext* topContext = new TopDUContext(IndexedString(url.toLocalFile()), RangeInRevision(CursorInRevision(0, 0), CursorInRevision(INT_MAX, INT_MAX)));

        topContext->setType(DUContext::Global);

        DUChain::self()->addDocumentChain(topContext);
        rTopContext = DUChainUtils::standardContextForUrl(url);
        Cpp::EnvironmentFile *pFile = new Cpp::EnvironmentFile(parent->document(), topContext);
        DUChain::self()->updateContextEnvironment(rTopContext.data(), pFile);
    }
    rTopContext->clearProblems();
    lock.unlock();
    */

    // AST parse
    clang::ASTConsumer *astConsumer = new CLangDeclBuilder(url, ci.getSourceManager(), lo);

    ci.getDiagnosticClient().BeginSourceFile(ci.getLangOpts(),
                                             &ci.getPreprocessor());
    clang::ParseAST(ci.getPreprocessor(), astConsumer, ci.getASTContext());
    ci.getDiagnosticClient().EndSourceFile();

    /*
    CppLanguageSupport::self()->codeHighlighting()->highlightDUChain(rTopContext);
    parent->setDuChain(rTopContext);*/
}


CLangParseJob::CLangParseJob (const KUrl& url) : ParseJob (url)
{
    d = new CLangParseJobPrivate(url);
    qDebug() << "CLANG OK!";
}

CLangParseJob::~CLangParseJob()
{
    delete d;
    qDebug() << "CLANG DEST OK!";
}

void CLangParseJob::run()
{
    qDebug() << "CLANG PARSE BEGIN" << d->url;

    UrlParseLock urlLock(document());

    readContents();

    qDebug() << "MODIFICATION:" << contents().modification;

    if (abortRequested())
        return abortJob();

    d->run(this);
}

#include "clangparsejob.moc"
