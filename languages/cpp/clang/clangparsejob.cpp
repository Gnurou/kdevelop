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

#include "clangparsejob.h"

#include <clang-c/Index.h>

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
#include <language/duchain/classdeclaration.h>
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

/**
 * Convenience class to easily convert a CXString into a QString and not care
 * about reference counting.
 */
class QCXString : public QString
{
public:
    QCXString() : QString() {}
    QCXString(const CXString &cstring) : QString(clang_getCString(cstring))
    {
        clang_disposeString(cstring);
    }
};

AbstractType::Ptr typeOfNode(const CXCursor &node)
{
    CXType type = clang_getCursorType(node);

    switch (type.kind) {
        case CXType_Int:
            return AbstractType::Ptr(new IntegralType(IntegralType::TypeInt));
        case CXType_Char_S:
        case CXType_Char_U:
            return AbstractType::Ptr(new IntegralType(IntegralType::TypeChar));
        case CXType_Char16:
            return AbstractType::Ptr(new IntegralType(IntegralType::TypeChar16_t));
        case CXType_Char32:
            return AbstractType::Ptr(new IntegralType(IntegralType::TypeChar32_t));
        case CXType_Unexposed:
        {
            // Probably a struct or something - find the declaration and return
            // its type
            CXCursor decl = clang_getTypeDeclaration(type);
            CXCursorKind declKind = clang_getCursorKind(decl);
            if (declKind == CXCursor_NoDeclFound)
                return AbstractType::Ptr();
            QCXString declName(clang_getCursorSpelling(decl));
            StructureType *structType = new StructureType();
            structType->setDeclarationId(DeclarationId(QualifiedIdentifier(declName)));
            return AbstractType::Ptr(structType);
        }
        case CXType_Invalid:
        default:
            return AbstractType::Ptr();
    }
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

bool operator <(const CXCursor &cursor1, const CXCursor &cursor2)
{
    unsigned int hash1 = clang_hashCursor(cursor1);
    unsigned int hash2 = clang_hashCursor(cursor2);

    return hash1 < hash2;
}

CursorInRevision toCursor(const CXSourceLocation &sl)
{
    unsigned int line, col;
    clang_getExpansionLocation(sl, NULL, &line, &col, NULL);
    return CursorInRevision(line - 1, col - 1);
}

class CLangContextBuilder : public AbstractContextBuilder<CXCursor, CXCursor>
{
public:
    CLangContextBuilder() {}

    virtual ~CLangContextBuilder() { }

    virtual void setContextOnNode(CXCursor* node, DUContext* context)
    {
        _declMap[clang_hashCursor(*node)] = context;
    }

    virtual DUContext* contextFromNode(CXCursor* node)
    {
        unsigned int hash = clang_hashCursor(*node);
        if (!_declMap.contains(hash))
            return 0;
        else
            return _declMap[hash];
    }

    virtual RangeInRevision editorFindRange(CXCursor* fromNode, CXCursor* toNode)
    {
        CXSourceRange fromRange = clang_getCursorExtent(*fromNode);
        CXSourceRange toRange = clang_getCursorExtent(*toNode);

        CursorInRevision fBegin(toCursor(clang_getRangeStart(fromRange)));
        CursorInRevision fEnd(toCursor(clang_getRangeEnd(toRange)));
        return RangeInRevision(fBegin, fEnd);
    }

    virtual QualifiedIdentifier identifierForNode(CXCursor* node)
    {
        if (!node)
            return QualifiedIdentifier();
        else {
            //CXString str = clang_getCursorUSR(*node);
            QCXString str(clang_getCursorDisplayName(*node));
            return QualifiedIdentifier(str);
        }
    }

protected:
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
    QMap<unsigned int, DUContext *> _declMap;

    IndexedString _url;
};

RangeInRevision range2Range(CXSourceRange range)
{
    CursorInRevision fBegin(toCursor(clang_getRangeStart(range)));
    CursorInRevision fEnd(toCursor(clang_getRangeEnd(range)));
    return RangeInRevision(fBegin, fEnd);
}

RangeInRevision rangeForName(CXCursor cursor)
{
    return range2Range(clang_Cursor_getSpellingNameRange(cursor, 0, 0));
}

enum CXChildVisitResult visitCursor(CXCursor cursor, CXCursor parent, CXClientData client_data);

class CLangDeclBuilder :
    public AbstractDeclarationBuilder<CXCursor, CXCursor, CLangContextBuilder>
{
public:
    CLangDeclBuilder(const IndexedString &url) : _url(url) {}
    virtual ~CLangDeclBuilder() { }

    virtual void HandleTranslationUnit(CXTranslationUnit tu)
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
        qDebug() << "Start parsing" << _url.str() << "with context" << rTopContext.data();
        CXCursor topCursor = clang_getTranslationUnitCursor(tu);
        build(_url, &topCursor, rTopContext);
    }

    /// Should only be called once on the root node
    virtual void startVisiting(CXCursor* node)
    {
        clang_visitChildren(*node, visitCursor, this);
    }

    enum CXChildVisitResult _visitCursor(CXCursor cursor, CXCursor parent);

    /*
     * Map clang declarations to KDev declarations. That way we can look decls
     * at a glance without using KDev's lookup system.
     */
    QMap<unsigned int, DeclarationPointer> clangDecl2kdevDecl;
    QMap<QString, DeclarationPointer> clangType2kdevType;

    /*RangeInRevision rangeForLocation(const clang::SourceLocation &start, const clang::SourceLocation& end);
    RangeInRevision rangeForLocation(const clang::SourceLocation &loc);*/
    void createUse(CXCursor refExpr);

private:
    IndexedString _url;
};

enum CXChildVisitResult CLangDeclBuilder::_visitCursor(CXCursor cursor, CXCursor parent)
{
    CXChildVisitResult res = CXChildVisit_Recurse;

    CXCursorKind kind = clang_getCursorKind(cursor);
    QCXString kindSpelling(clang_getCursorKindSpelling(kind));
    QCXString spelling(clang_getCursorDisplayName(cursor));
    CXSourceLocation sl = clang_getCursorLocation(cursor);
    unsigned int line, col;
    CXFile file;
    QCXString fName;

    clang_getExpansionLocation(sl, &file, &line, &col, NULL);
    fName = clang_getFileName(file);
    fprintf(stderr, "%3d,%3d: ", line, col);
    fprintf(stderr, "%s %s", kindSpelling.toAscii().constData(), spelling.toAscii().constData());

    CXType ctype = clang_getCursorType(cursor);
    QCXString types = clang_getTypeKindSpelling(ctype.kind);
    fprintf(stderr, " of type %d %s", ctype.kind, types.toAscii().constData());

    if (clang_isDeclaration(kind)) {
        fprintf(stderr, " declaration \n");

        bool addDecls(!fName.isEmpty());

        DeclarationPointer kDecl;
        DUContext *context = 0;

        if (addDecls) {
            DUChainWriteLocker lock(DUChain::lock());

            switch (kind) {
                case CXCursor_ClassDecl:
                case CXCursor_StructDecl:
                    kDecl = openDeclaration<ClassDeclaration>(QualifiedIdentifier(spelling), rangeForName(cursor), DeclarationIsDefinition);
                    kDecl->setKind(Declaration::Type);
                    kDecl.dynamicCast<ClassDeclaration>()->setClassType(ClassDeclarationData::Struct);
                    context = openContext(&cursor, DUContext::Class, &cursor);
                    context->setOwner(kDecl.data());
                    break;
                case CXCursor_FunctionDecl:
                    kDecl = openDeclaration<FunctionDeclaration>(QualifiedIdentifier(spelling), rangeForName(cursor), DeclarationIsDefinition);
                    kDecl->setKind(Declaration::Type);
                    context = openContext(&cursor, DUContext::Function, &cursor);
                    context->setOwner(kDecl.data());
                    break;
                case CXCursor_FieldDecl:
                    kDecl = openDeclaration<Declaration>(QualifiedIdentifier(spelling), rangeForName(cursor), DeclarationIsDefinition);
                    kDecl->setKind(Declaration::Instance);
                    break;
                case CXCursor_VarDecl:
                case CXCursor_ParmDecl:
                    kDecl = openDeclaration<Declaration>(QualifiedIdentifier(spelling), rangeForName(cursor), DeclarationIsDefinition);
                    kDecl->setKind(Declaration::Instance);
                    break;
                default:
                    kDecl = openDeclaration<Declaration>(QualifiedIdentifier(spelling), rangeForName(cursor), DeclarationIsDefinition);
                    break;
            }
            if (kDecl.data()) {
                kDecl->setType(typeOfNode(cursor));
                clangDecl2kdevDecl[clang_hashCursor(cursor)] = kDecl;
            }
            lock.unlock();
        }

        clang_visitChildren(cursor, visitCursor, this);

        if (context)
            closeContext();
        if (kDecl.data())
            closeDeclaration();
    } else if (clang_isReference(kind) || kind == CXCursor_DeclRefExpr || kind == CXCursor_MemberRefExpr) {
    //if (!clang_Cursor_isNull(clang_getCursorReferenced(cursor))) {
        CXCursor ref = clang_getCursorReferenced(cursor);
        QCXString refName(clang_getCursorDisplayName(ref));
        sl = clang_getCursorLocation(ref);
        clang_getExpansionLocation(sl, NULL, &line, &col, NULL);
        fprintf(stderr, " references %s (%d %d)", refName.toAscii().constData(), line, col);
        fprintf(stderr, "\n");

        createUse(cursor);

        clang_visitChildren(cursor, visitCursor, this);
    } else {
        fprintf(stderr, "\n");
        clang_visitChildren(cursor, visitCursor, this);
    }

    res = CXChildVisit_Continue;
    return res;
}

enum CXChildVisitResult visitCursor(CXCursor cursor, CXCursor parent, CXClientData client_data)
{
    CLangDeclBuilder* builder = (CLangDeclBuilder *)client_data;
    return builder->_visitCursor(cursor, parent);
}

/*
RangeInRevision CLangDeclBuilder::rangeForLocation(const clang::SourceLocation &start, const clang::SourceLocation& end)
{
    return RangeInRevision(toCursor(start), toCursor(endOf(end)));
}

RangeInRevision CLangDeclBuilder::rangeForLocation(const clang::SourceLocation& loc)
{
    return rangeForLocation(loc, loc);
}
*/

void CLangDeclBuilder::createUse(CXCursor refExpr)
{
    CXSourceRange nRange = clang_Cursor_getSpellingNameRange(refExpr, 0, 0);
    RangeInRevision range = range2Range(nRange);
    CXCursor ref = clang_getCursorReferenced(refExpr);
    DUChainWriteLocker lock(DUChain::lock());
    DeclarationPointer kDecl(clangDecl2kdevDecl[clang_hashCursor(ref)]);
    qDebug() << "create use" << kDecl.data() << range.start.line << range.end.column;
    currentContext()->createUse(currentContext()->topContext()->indexForUsedDeclaration(kDecl.data()), range);
    lock.unlock();
}

class CLangParseJobPrivate {
public:
    CLangParseJobPrivate(CLangParseJob *_parent);
    void run();

private:
    CLangParseJob *parent;
    IndexedString url;
};

CLangParseJobPrivate::CLangParseJobPrivate (CLangParseJob *_parent) : parent(_parent), url(_parent->document())
{
}

void CLangParseJobPrivate::run()
{
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

    CXIndex index = clang_createIndex(0, 0);
    CXTranslationUnit tu = clang_parseTranslationUnit(index, url.toUrl().toLocalFile().toAscii().constData(), 0, 0, 0, 0, CXTranslationUnit_None);
    CLangDeclBuilder builder(url);
    builder.HandleTranslationUnit(tu);

    clang_disposeTranslationUnit(tu);
    clang_disposeIndex(index);

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
    d = new CLangParseJobPrivate(this);
}

CLangParseJob::~CLangParseJob()
{
    delete d;
}

void CLangParseJob::run()
{
    UrlParseLock urlLock(document());

    readContents();

    if (abortRequested())
        return abortJob();

    QElapsedTimer timer;
    timer.start();
    d->run();
    qDebug() << document().str() << "parsed in" << timer.elapsed() << "ms";
}

#include "clangparsejob.moc"
