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
#include <llvm/Support/MemoryBuffer.h>

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

#include <interfaces/icore.h>
#include <interfaces/ilanguage.h>
#include <interfaces/ilanguagecontroller.h>
#include <language/backgroundparser/backgroundparser.h>
#include <language/interfaces/iproblem.h>
#include <language/interfaces/icodehighlighting.h>
#include <language/interfaces/ilanguagesupport.h>
#include <language/duchain/topducontext.h>
#include <language/duchain/duchain.h>
#include <language/duchain/duchainutils.h>
#include <language/duchain/duchainlock.h>
#include <language/duchain/duchainregister.h>
#include <language/duchain/topducontextdata.h>
#include <language/duchain/types/integraltype.h>
#include <language/backgroundparser/urlparselock.h>
#include <languages/cpp/cpplanguagesupport.h>
#include <languages/cpp/cppduchain/environmentmanager.h>
#include <languages/cpp/cppduchain/navigation/navigationwidget.h>
#include <language/duchain/builders/abstractcontextbuilder.h>
#include <language/duchain/builders/abstracttypebuilder.h>
#include <language/duchain/builders/abstractdeclarationbuilder.h>
#include <language/duchain/builders/abstractusebuilder.h>

#include <QElapsedTimer>

using namespace KDevelop;

class KDevDiagnosticConsumer : public clang::DiagnosticConsumer
{
public:
    KDevDiagnosticConsumer(clang::LangOptions _lo, IndexedString url) : lo(_lo), _url(url) {}
  virtual void HandleDiagnostic(clang::DiagnosticsEngine::Level DiagLevel,
                                const clang::Diagnostic &Info);
  virtual DiagnosticConsumer *clone(clang::DiagnosticsEngine &Diags) const;

  clang::LangOptions lo;
private:
  IndexedString _url;
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

        DocumentRange range(_url, SimpleRange(SimpleCursor(sm.getSpellingLineNumber(begLocation) - 1, sm.getSpellingColumnNumber(begLocation) - 1),
            SimpleCursor(sm.getSpellingLineNumber(endLocation) - 1, sm.getSpellingColumnNumber(endLocation) - 1)));
        p->setFinalLocation(range);
    } else {
        DocumentRange range(_url, SimpleRange(SimpleCursor(sm.getSpellingLineNumber(location) - 1, sm.getSpellingColumnNumber(clang::Lexer::getLocForEndOfToken(location, 0, sm, lo)) - 1), 0));
        p->setFinalLocation(range);
    }

    qDebug() << "parsing error" << p->description() << p->finalLocation() << _url.toUrl();

    DUChainWriteLocker lock(DUChain::lock());
    ReferencedTopDUContext rTopContext(DUChainUtils::standardContextForUrl(_url.toUrl()));
    if (rTopContext) rTopContext->addProblem(p);
    else qDebug() << "No top context to report error to!";
    lock.unlock();
}

clang::DiagnosticConsumer *KDevDiagnosticConsumer::clone(clang::DiagnosticsEngine &Diags) const
{
    return new KDevDiagnosticConsumer(lo, _url);
}

template <class T>
class CLangDUContextTpl : public T {
public:
    enum {
      Identity = T::Identity + 80
    };

    template<class p1>
    CLangDUContextTpl(p1& data) : T(data) {}

    template <class p1, class p2>
    CLangDUContextTpl(const p1& range, p2* parent, bool anonymous = false) : T(range, parent, anonymous)
    {
      static_cast<DUChainBase*>(this)->d_func_dynamic()->setClassId(this);
    }

    template <class p1, class p2, class p3>
    CLangDUContextTpl(const p1& url, const p2& range, p3* file = 0) : T(url, range, file)
    {
      static_cast<DUChainBase*>(this)->d_func_dynamic()->setClassId(this);
    }

    virtual QWidget* createNavigationWidget(Declaration* decl = 0, TopDUContext* topContext = 0,
                                        const QString& htmlPrefix = QString(),
                                        const QString& htmlSuffix = QString()) const
    {
        if( decl == 0 ) {
          KUrl u( T::url().str() );
          IncludeItem i;
          i.pathNumber = -1;
          i.name = u.fileName();
          i.isDirectory = false;
          i.basePath = u.upUrl();

          return new Cpp::NavigationWidget( i, TopDUContextPointer(topContext ? topContext : this->topContext()), htmlPrefix, htmlSuffix );
        } else {
          return new Cpp::NavigationWidget( DeclarationPointer(decl), TopDUContextPointer(topContext ? topContext : this->topContext()), htmlPrefix, htmlSuffix );
        }
    }
};

typedef CLangDUContextTpl<TopDUContext> CLangTopDUContext;
REGISTER_DUCHAIN_ITEM_WITH_DATA(CLangTopDUContext, TopDUContextData);

typedef CLangDUContextTpl<DUContext> CLangDUContext;
REGISTER_DUCHAIN_ITEM_WITH_DATA(CLangDUContext, DUContextData);

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
        _declMap[node] = context;
    }

    virtual DUContext* contextFromNode(clang::Decl* node)
    {
        qDebug() << "contextFromNode" << node << _declMap.contains(node);
        if (!_declMap.contains(node)) return 0;
        else return _declMap[node];
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

    const QMap<clang::Decl *, DUContext *> declMap() const { return _declMap; }

protected:
    IndexedString _url;
    clang::SourceManager *_sm;
    clang::LangOptions _lo;

    virtual DUContext* newContext(const RangeInRevision& range)
    {
        return new CLangDUContext(range, currentContext());
    }

    virtual TopDUContext* newTopContext(const RangeInRevision& range, ParsingEnvironmentFile* file = 0)
    {
        return new CLangTopDUContext(document(), range, file);
    }

    /*
     * Map clang declarations to their corresponding context node.
     */
    QMap<clang::Decl *, DUContext *> _declMap;
};

/**
 * Build contexts and declarations. The mapping between CLang and KDevelop's
 * declarations is available through declMap(). This is useful for the following
 * parsers.
 */
class CLangDeclBuilder :
    public AbstractDeclarationBuilder<clang::Decl, clang::NamedDecl, CLangContextBuilder>,
    public clang::ASTConsumer, public clang::RecursiveASTVisitor<CLangDeclBuilder>
{
public:
    CLangDeclBuilder(const IndexedString &url, clang::SourceManager &sm, clang::LangOptions &lo) { _url = url; _sm = &sm; lo = _lo; }
    virtual ~CLangDeclBuilder() { }

    virtual void HandleTranslationUnit(clang::ASTContext &ctx)
    {
        ReferencedTopDUContext rTopContext;
        {
            DUChainReadLocker l(DUChain::lock());
            rTopContext = DUChainUtils::standardContextForUrl(_url.toUrl());
        }
        if (rTopContext) {
            DUChainWriteLocker l(DUChain::lock());
            rTopContext->deleteUsesRecursively();
            rTopContext->deleteChildContextsRecursively();
            rTopContext->deleteLocalDeclarations();
        }
        clang::Decl *tuDecl = ctx.getTranslationUnitDecl();
        qDebug() << "Start parsing" << _url.str() << "with context" << rTopContext.data();
        build(_url, tuDecl, rTopContext);
    }

    /// Should only be called once on the root node
    virtual void startVisiting(clang::Decl* node)
    {
        qDebug() << "startVisiting called!\n";
        TraverseDecl(node);
    }

    /*
     * Map clang declarations to KDev declarations. That way we can look decls
     * at a glance without using KDev's lookup system.
     */
    QMap<clang::Decl *, DeclarationPointer> clangDecl2kdevDecl;

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

AbstractType::Ptr CLangType2KDevType(const clang::QualType &type)
{
    const clang::Type *t = type.getTypePtr();

    if (t->isBuiltinType()) {
        if (t->isIntegerType()) {
            return AbstractType::Ptr(new IntegralType(IntegralType::TypeInt));
        }
    }
    {
      StructureType *str = new StructureType();
      str->setDeclarationId(DeclarationId(QualifiedIdentifier(type.getAsString().c_str())));
      return AbstractType::Ptr(str);
    }
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
    clangDecl2kdevDecl[decl] = kDecl;
    kDecl->setType(CLangType2KDevType(decl->getType()));
    //kDecl->setType(IntegralType::Ptr(new IntegralType(IntegralType::TypeInt)));
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
        //DeclarationPointer kDecl(openDeclaration<Declaration>(QualifiedIdentifier(vDecl->getQualifiedNameAsString().c_str()), dRange, DeclarationIsDefinition));
        DeclarationPointer kDecl(clangDecl2kdevDecl[decl]);
        currentContext()->createUse(currentContext()->topContext()->indexForUsedDeclaration(kDecl.data()), nRange);
        lock.unlock();
        //closeDeclaration();
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

    DUContext *fContext = openContext(func, DUContext::Function, func);

    bool ret = clang::RecursiveASTVisitor<CLangDeclBuilder>::TraverseFunctionDecl(func);

    lock.lock();
    fContext->setOwner(fdecl);
    lock.unlock();
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
    CLangParseJobPrivate(CLangParseJob *_parent);
    void run();

    clang::CompilerInstance ci;
    clang::LangOptions &lo;
    clang::HeaderSearchOptions &so;
    IndexedString url;

private:
    CLangParseJob *parent;
};

CLangParseJobPrivate::CLangParseJobPrivate (CLangParseJob *_parent) : ci(), lo(ci.getLangOpts()), so(ci.getHeaderSearchOpts()), url(_parent->document()), parent(_parent)
{
    lo.C99 = 1;
    lo.GNUMode = 0;
    lo.GNUKeywords = 1;
    lo.CPlusPlus = 0;
    lo.CPlusPlus0x = 0;
    lo.Bool = 0;
    lo.NoBuiltin = 0;

    ci.createDiagnostics(0, 0, new KDevDiagnosticConsumer(lo, _parent->document()));

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
}

void CLangParseJobPrivate::run()
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

    // Clear previous problems
    {
      DUChainReadLocker l(DUChain::lock());
      ReferencedTopDUContext rTopContext(DUChainUtils::standardContextForUrl(url.toUrl()));
      l.unlock();
      if (rTopContext.data()) {
        DUChainWriteLocker w(DUChain::lock());
        rTopContext->clearProblems();
        w.unlock();
      }
    }

    // Set contents for parser
    llvm::MemoryBuffer *buffer = llvm::MemoryBuffer::getMemBuffer(parent->contents().contents.constData());
    ci.getSourceManager().setMainFileID(ci.getSourceManager().createFileIDForMemBuffer(buffer));

    // AST parse
    // TODO get ILanguage::parseLock?
    clang::ASTConsumer *astConsumer = new CLangDeclBuilder(url, ci.getSourceManager(), lo);

    ci.getDiagnosticClient().BeginSourceFile(ci.getLangOpts(),
                                             &ci.getPreprocessor());
    clang::ParseAST(ci.getPreprocessor(), astConsumer, ci.getASTContext());
    ci.getDiagnosticClient().EndSourceFile();

    DUChainReadLocker l(DUChain::lock());
    ReferencedTopDUContext rTopContext(DUChainUtils::standardContextForUrl(url.toUrl()));
    if (!rTopContext.data()) {
        qDebug() << "ERROR: Cannot get top context for" << url.str();
        return;
    }
    l.unlock();

    DUChainWriteLocker lock(DUChain::lock());
    Cpp::EnvironmentFile *envFile = new Cpp::EnvironmentFile(parent->document(), rTopContext);
    DUChain::self()->updateContextEnvironment(rTopContext, envFile);
    //rTopContext->setFeatures();
    //rTopContext->setFlags();
    parent->setDuChain(rTopContext);
    lock.unlock();

    // TODO only if needed! See cppparsejob::highlightifneeded
    CppLanguageSupport::self()->codeHighlighting()->highlightDUChain(rTopContext);
}


CLangParseJob::CLangParseJob (const KUrl& url) : ParseJob (url)
{
    // TODO IndexedString's c_str are not 0-terminated. Pass the CLangParseJob instead and get the IndexedString from document() when needed.
    d = new CLangParseJobPrivate(this);
    qDebug() << "CLANG OK!" << document().str();
}

CLangParseJob::~CLangParseJob()
{
    delete d;
    qDebug() << "CLANG DEST OK!";
}

void CLangParseJob::run()
{
    qDebug() << "CLANG PARSE BEGIN" << d->url.str();

    UrlParseLock urlLock(document());

    readContents();

    qDebug() << "MODIFICATION:" << contents().modification;
    qDebug() << "CONTENTS:" << QString(contents().contents);

    if (abortRequested())
        return abortJob();

    QElapsedTimer timer;
    timer.start();
    d->run();
    qDebug() << document().str() << "parsed in" << timer.elapsed() << "ms";
}

#include "clangparsejob.moc"
