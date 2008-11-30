/* This file is part of KDevelop
    Copyright 2006-2007 Hamish Rodda <rodda@kde.org>
    Copyright 2007-2008 David Nolden <david.nolden.kdevelop@art-master.de>

   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public
   License version 2 as published by the Free Software Foundation.

   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public License
   along with this library; see the file COPYING.LIB.  If not, write to
   the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
   Boston, MA 02110-1301, USA.
*/

#include "declarationbuilder.h"

#include "debugbuilders.h"

#include <QByteArray>
#include <typeinfo>

#include "templatedeclaration.h"

#include <ktexteditor/smartrange.h>
#include <ktexteditor/smartinterface.h>

#include "parser/type_compiler.h"
#include "parser/commentformatter.h"

#include <language/duchain/forwarddeclaration.h>
#include <language/duchain/duchain.h>
#include <language/duchain/duchainlock.h>
#include <language/duchain/repositories/itemrepository.h>
#include <language/duchain/types/identifiedtype.h>
#include <language/duchain/namespacealiasdeclaration.h>
#include <language/duchain/aliasdeclaration.h>
#include <util/pushvalue.h>

#include "qtfunctiondeclaration.h"
#include "cppeditorintegrator.h"
#include "name_compiler.h"
#include <language/duchain/classfunctiondeclaration.h>
#include <language/duchain/functiondeclaration.h>
#include <language/duchain/functiondefinition.h>
#include "templateparameterdeclaration.h"
#include "type_compiler.h"
#include "tokens.h"
#include "parsesession.h"
#include "cpptypes.h"
#include "cppduchain.h"
#include "cpptypes.h"
#include "classdeclaration.h"

#include "cppdebughelper.h"

const Identifier& castIdentifier() {
  static Identifier id("operator{...cast...}");
  return id;
}

using namespace KTextEditor;
using namespace KDevelop;
using namespace Cpp;

ClassDeclarationData::ClassType classTypeFromTokenKind(int kind)
{
  switch(kind)
  {
  case Token_struct:
    return ClassDeclarationData::Struct;
  case Token_union:
    return ClassDeclarationData::Union;
  default:
    return ClassDeclarationData::Class;
  }
}

///Returns the context assigned to the given declaration that contains the template-parameters, if available. Else zero.
DUContext* getTemplateContext(Declaration* decl) {
  DUContext* internal = decl->internalContext();
  if( !internal )
    return 0;
  foreach( const DUContext::Import &ctx, internal->importedParentContexts() ) {
    if( ctx.context(decl->topContext()) )
      if( ctx.context(decl->topContext())->type() == DUContext::Template )
        return ctx.context(decl->topContext());
  }
  return 0;
}

bool DeclarationBuilder::changeWasSignificant() const
{
  ///@todo Also set m_changeWasSignificant if publically visible declarations were removed(needs interaction with abstractcontextbuilder)
  return m_changeWasSignificant;
}

DeclarationBuilder::DeclarationBuilder (ParseSession* session)
  : DeclarationBuilderBase(), m_inTypedef(false), m_changeWasSignificant(false), m_ignoreDeclarators(false), m_declarationHasInitializer(false), m_collectQtFunctionSignature(false)
{
  setEditor(new CppEditorIntegrator(session), true);
}

DeclarationBuilder::DeclarationBuilder (CppEditorIntegrator* editor)
  : DeclarationBuilderBase(), m_inTypedef(false), m_changeWasSignificant(false), m_ignoreDeclarators(false), m_declarationHasInitializer(false), m_collectQtFunctionSignature(false)
{
  setEditor(editor, false);
}

ReferencedTopDUContext DeclarationBuilder::buildDeclarations(const Cpp::EnvironmentFilePointer& file, AST *node, IncludeFileList* includes, const ReferencedTopDUContext& updateContext, bool removeOldImports)
{
  ReferencedTopDUContext top = buildContexts(file, node, includes, updateContext, removeOldImports);

  Q_ASSERT(m_accessPolicyStack.isEmpty());
  Q_ASSERT(m_functionDefinedStack.isEmpty());

  return top;
}

// DUContext* DeclarationBuilder::buildSubDeclarations(const HashedString& url, AST *node, KDevelop::DUContext* parent) {
//   DUContext* top = buildSubContexts(url, node, parent);
//
//   Q_ASSERT(m_accessPolicyStack.isEmpty());
//   Q_ASSERT(m_functionDefinedStack.isEmpty());
//
//   return top;
// }

void DeclarationBuilder::visitTemplateParameter(TemplateParameterAST * ast) {
  
  //Backup and zero the parameter declaration, because we will handle it here directly, and don't want a normal one to be created
  ParameterDeclarationAST* paramAST = ast->parameter_declaration;
  ast->parameter_declaration = 0;
  DeclarationBuilderBase::visitTemplateParameter(ast);
  ast->parameter_declaration = paramAST;
  
  if( ast->type_parameter || ast->parameter_declaration ) {
    ///@todo deal with all the other stuff the AST may contain
    TemplateParameterDeclaration* decl;
    if(ast->type_parameter)
      decl = openDeclaration<TemplateParameterDeclaration>(ast->type_parameter->name, ast, Identifier(), false, !ast->type_parameter->name);
    else
      decl = openDeclaration<TemplateParameterDeclaration>(ast->parameter_declaration->declarator ? ast->parameter_declaration->declarator->id : 0, ast, Identifier(), false, !ast->parameter_declaration->declarator);

    DUChainWriteLocker lock(DUChain::lock());
    AbstractType::Ptr type = lastType();
    if( type.cast<CppTemplateParameterType>() ) {
      type.cast<CppTemplateParameterType>()->setDeclaration(decl);
    } else {
      kDebug(9007) << "bad last type";
    }
    decl->setAbstractType(type);

    if( ast->type_parameter && ast->type_parameter->type_id ) {
      //Extract default type-parameter
      QualifiedIdentifier defaultParam;

      QString str;
      ///Only record the strings, because these expressions may depend on template-parameters and thus must be evaluated later
      str += stringFromSessionTokens( editor()->parseSession(), ast->type_parameter->type_id->start_token, ast->type_parameter->type_id->end_token );

      defaultParam = QualifiedIdentifier(str);

      decl->setDefaultParameter(defaultParam);
    }

    if( ast->parameter_declaration ) {
      if( ast->parameter_declaration->expression )
        decl->setDefaultParameter( QualifiedIdentifier( stringFromSessionTokens( editor()->parseSession(), ast->parameter_declaration->expression->start_token, ast->parameter_declaration->expression->end_token ) ) );
    }
    closeDeclaration(ast->parameter_declaration);
  }
}

void DeclarationBuilder::parseComments(const ListNode<size_t> *comments)
{
  setComment(CommentFormatter::formatComment(comments, editor()->parseSession()));
}


void DeclarationBuilder::visitFunctionDeclaration(FunctionDefinitionAST* node)
{

  parseComments(node->comments);
  parseStorageSpecifiers(node->storage_specifiers);
  parseFunctionSpecifiers(node->function_specifiers);

  m_functionDefinedStack.push(node->start_token);

  DeclarationBuilderBase::visitFunctionDeclaration(node);

  m_functionDefinedStack.pop();

  popSpecifiers();
}

void DeclarationBuilder::visitInitDeclarator(InitDeclaratorAST *node)
{
  PushValue<bool> setHasInitialize(m_declarationHasInitializer, (bool)node->initializer);
  DeclarationBuilderBase::visitInitDeclarator(node);
}

void DeclarationBuilder::visitSimpleDeclaration(SimpleDeclarationAST* node)
{
  parseComments(node->comments);
  parseStorageSpecifiers(node->storage_specifiers);
  parseFunctionSpecifiers(node->function_specifiers);

  m_functionDefinedStack.push(0);

  DeclarationBuilderBase::visitSimpleDeclaration(node);

  m_functionDefinedStack.pop();

  popSpecifiers();
}

void DeclarationBuilder::visitDeclarator (DeclaratorAST* node)
{
  if(m_ignoreDeclarators) {
    DeclarationBuilderBase::visitDeclarator(node);
    return;
  }
  //need to make backup because we may temporarily change it
  ParameterDeclarationClauseAST* parameter_declaration_clause_backup = node->parameter_declaration_clause;

  m_collectQtFunctionSignature = !m_accessPolicyStack.isEmpty() && ((m_accessPolicyStack.top() & FunctionIsSlot) || (m_accessPolicyStack.top() & FunctionIsSignal));
  m_qtFunctionSignature = QByteArray();
  
  ///@todo this should be solved more elegantly within parser and AST
  if (node->parameter_declaration_clause) {
    //Check if all parameter declarations are valid. If not, this is a misunderstood variable declaration.
    if(!checkParameterDeclarationClause(node->parameter_declaration_clause))
      node->parameter_declaration_clause = 0;
  }
  if (node->parameter_declaration_clause) {
    Declaration* decl = openFunctionDeclaration(node->id, node);

    if( !m_functionDefinedStack.isEmpty() ) {
        DUChainWriteLocker lock(DUChain::lock());
        decl->setDeclarationIsDefinition( (bool)m_functionDefinedStack.top() );
    }

    applyFunctionSpecifiers();
  } else {
    openDefinition(node->id, node, node->id == 0);
  }

  m_collectQtFunctionSignature = false;

  applyStorageSpecifiers();

  DeclarationBuilderBase::visitDeclarator(node);

  if (node->parameter_declaration_clause) {
    if (!m_functionDefinedStack.isEmpty() && m_functionDefinedStack.top() && node->id) {

      DUChainWriteLocker lock(DUChain::lock());
      //We have to search for the fully qualified identifier, so we always get the correct class
      QualifiedIdentifier id = currentContext()->scopeIdentifier(false) + identifierForNode(node->id);
      id.setExplicitlyGlobal(true);

      if (id.count() > 1 ||
           (m_inFunctionDefinition && (currentContext()->type() == DUContext::Namespace || currentContext()->type() == DUContext::Global))) {
        SimpleCursor pos = currentDeclaration()->range().start;//editor()->findPosition(m_functionDefinedStack.top(), KDevelop::EditorIntegrator::FrontEdge);
        // TODO: potentially excessive locking

        QList<Declaration*> declarations = currentContext()->findDeclarations(id, pos, AbstractType::Ptr(), 0, DUContext::OnlyFunctions);

        FunctionType::Ptr currentFunction = lastType().cast<FunctionType>();
        int functionArgumentCount = 0;
        if(currentFunction)
          functionArgumentCount = currentFunction->arguments().count();

        for( int cycle = 0; cycle < 3; cycle++ ) {
          bool found = false;
          ///We do 2 cycles: In the first cycle, we want an exact match. In the second, we accept approximate matches.
          foreach (Declaration* dec, declarations) {
            if (dec->isForwardDeclaration())
              continue;
            if(dec == currentDeclaration() || dec->isDefinition())
              continue;
            //Compare signatures of function-declarations:
            if(dec->abstractType()->indexed() == lastType()->indexed())
            {
              //The declaration-type matches this definition, good.
            }else{
              if(cycle == 0) {
                //First cycle, only accept exact matches
                continue;
              }else if(cycle == 1){
                //Second cycle, match by argument-count
                FunctionType::Ptr matchFunction = dec->type<FunctionType>();
                if(currentFunction && matchFunction && currentFunction->arguments().count() == functionArgumentCount ) {
                  //We have a match
                }else{
                  continue;
                }
              }else if(cycle == 2){
                //Accept any match, so just continue
              }
              if(FunctionDefinition::definition(dec) && wasEncountered(FunctionDefinition::definition(dec)))
                continue; //Do not steal declarations
            }

            if(FunctionDefinition* funDef = dynamic_cast<FunctionDefinition*>(currentDeclaration()))
              funDef->setDeclaration(dec);

            found = true;
            break;
          }
          if(found)
            break;
        }
      }
    }
  }

  closeDeclaration();

  node->parameter_declaration_clause = parameter_declaration_clause_backup;
}

ForwardDeclaration * DeclarationBuilder::openForwardDeclaration(NameAST * name, AST * range)
{
  return openDeclaration<ForwardDeclaration>(name, range);
}

template<class Type>
Type hasTemplateContext( const QList<Type>& contexts ) {
  foreach( const Type& context, contexts )
    if( context&& context->type() == KDevelop::DUContext::Template )
      return context;
  return Type(0);
}

template<class Type>
Type hasTemplateContext( const QVector<Type>& contexts, TopDUContext* top ) {
  foreach( const Type& context, contexts )
    if( context.context(top) && context.context(top)->type() == KDevelop::DUContext::Template )
      return context;

  return Type(0);
}

//Check whether the given context is a template-context by checking whether it imports a template-parameter context
KDevelop::DUContext* isTemplateContext( KDevelop::DUContext* context ) {
  return hasTemplateContext( context->importedParentContexts(), context->topContext() ).context(context->topContext());
}

template<class T>
T* DeclarationBuilder::openDeclaration(NameAST* name, AST* rangeNode, const Identifier& customName, bool collapseRangeAtStart, bool collapseRangeAtEnd)
{
  DUChainWriteLocker lock(DUChain::lock());

  KDevelop::DUContext* templateCtx = hasTemplateContext(m_importedParentContexts);

  ///We always need to create a template declaration when we're within a template, so the declaration can be accessed
  ///by specialize(..) and its indirect DeclarationId
  if( templateCtx || m_templateDeclarationDepth ) {
    Cpp::SpecialTemplateDeclaration<T>* ret = openDeclarationReal<Cpp::SpecialTemplateDeclaration<T> >( name, rangeNode, customName, collapseRangeAtStart, collapseRangeAtEnd );
    ret->setTemplateParameterContext(templateCtx);
    return ret;
  } else{
    return openDeclarationReal<T>( name, rangeNode, customName, collapseRangeAtStart, collapseRangeAtEnd );
  }
}

template<class T>
T* DeclarationBuilder::openDeclarationReal(NameAST* name, AST* rangeNode, const Identifier& customName, bool collapseRangeAtStart, bool collapseRangeAtEnd)
{
  SimpleRange newRange;
  if(name) {
    std::size_t start = name->unqualified_name->start_token;
    std::size_t end = name->unqualified_name->end_token;

    //We must exclude the tilde. Else we may get totally messed up ranges when the name of a destructor is renamed in a macro
    if(name->unqualified_name->tilde)
      start = name->unqualified_name->tilde+1;

    newRange = editor()->findRange(start, end);
  }else{
    newRange = editor()->findRange(rangeNode);
  }

  if(collapseRangeAtStart)
    newRange.end = newRange.start;
  else if(collapseRangeAtEnd)
    newRange.start = newRange.end;

  Identifier localId = customName;

  if (name) {
    //If this is an operator thing, build the type first. Since it's part of the name, the type-builder doesn't catch it normally
    if(name->unqualified_name && name->unqualified_name->operator_id)
      visit(name->unqualified_name->operator_id);
    
    QualifiedIdentifier id = identifierForNode(name);

    if(localId.isEmpty())
      localId = id.last();
  }

  T* declaration = 0;

  if (recompiling()) {
    // Seek a matching declaration

    // Translate cursor to take into account any changes the user may have made since the text was retrieved
    LockedSmartInterface iface = editor()->smart();
    SimpleRange translated = editor()->translate(iface, newRange);

#ifdef DEBUG_UPDATE_MATCHING
    kDebug() << "checking" << localId.toString() << "range" << translated.textRange();
#endif

    ///@todo maybe order the declarations within ducontext and change here back to walking the indices, because that's easier to debug and faster
    QList<Declaration*> decls = currentContext()->allLocalDeclarations(localId);
    foreach( Declaration* dec, decls ) {

      if( wasEncountered(dec) )
        continue;

#ifdef DEBUG_UPDATE_MATCHING
      if( !(typeid(*dec) == typeid(T)) )
        kDebug() << "typeid mismatch:" << typeid(*dec).name() << typeid(T).name();

      if (!(dec->range() == translated))
        kDebug() << "range mismatch" << dec->range().textRange() << translated.textRange();

      if(!(localId == dec->identifier()))
        kDebug() << "id mismatch" << dec->identifier().toString() << localId.toString();
#endif

        //This works because dec->textRange() is taken from a smart-range. This means that now both ranges are translated to the current document-revision.
      if (dec->range() == translated &&
          (localId == dec->identifier() || (localId.isUnique() && dec->identifier().isUnique())) &&
          typeid(*dec) == typeid(T)
         )
      {
        // Match
        declaration = dynamic_cast<T*>(dec);
        break;
      }
    }

    if(!declaration) {
      ///Second run of the above, this time ignoring the ranges.
      foreach( Declaration* dec, decls ) {
        if( wasEncountered(dec) )
          continue;
        
        if ((localId == dec->identifier() || (localId.isUnique() && dec->identifier().isUnique())) &&
            typeid(*dec) == typeid(T)
          )
        {
          // Match
          declaration = dynamic_cast<T*>(dec);
          declaration->setRange(translated);
          break;
        }
      }
    }
  }
#ifdef DEBUG_UPDATE_MATCHING
  if(declaration)
    kDebug() << "found match for" << localId.toString();
  else
    kDebug() << "nothing found for" << localId.toString();
#endif

  if (!declaration) {
    if(currentContext()->inSymbolTable())
      m_changeWasSignificant = true; //We are adding a declaration that comes into the symbol table, so mark the change significant
/*    if( recompiling() )
      kDebug(9007) << "creating new declaration while recompiling: " << localId << "(" << newRange.textRange() << ")";*/
    LockedSmartInterface iface = editor()->smart();

    SmartRange* prior = editor()->currentRange(iface);

    ///We don't want to move the parent range around if the context is collapsed, so we find a parent range that can hold this range.
    KDevVarLengthArray<SmartRange*, 5> backup;
    while(editor()->currentRange(iface) && !editor()->currentRange(iface)->contains(newRange.textRange())) {
      backup.append(editor()->currentRange(iface));
      editor()->exitCurrentRange(iface);
    }

    if(prior && !editor()->currentRange(iface)) {
      editor()->setCurrentRange(iface, backup[backup.count()-1]);
      backup.resize(backup.size()-1);
    }

    SmartRange* range = editor()->currentRange(iface) ? editor()->createRange(iface, newRange.textRange()) : 0;

    editor()->exitCurrentRange(iface);

    for(int a = backup.size()-1; a >= 0; --a)
      editor()->setCurrentRange(iface, backup[a]);

    Q_ASSERT(editor()->currentRange(iface) == prior);

    declaration = new T(newRange, currentContext());
    declaration->setSmartRange(range);
    declaration->setIdentifier(localId);
  }

  //Clear some settings
  AbstractFunctionDeclaration* funDecl = dynamic_cast<AbstractFunctionDeclaration*>(declaration);
  if(funDecl)
    funDecl->clearDefaultParameters();

  declaration->setDeclarationIsDefinition(false); //May be set later

  declaration->setIsTypeAlias(m_inTypedef);

  if( localId.templateIdentifiersCount() ) {
    TemplateDeclaration* templateDecl = dynamic_cast<TemplateDeclaration*>(declaration);
    if( declaration && templateDecl ) {
      ///This is a template-specialization. Find the class it is specialized from.
      localId.clearTemplateIdentifiers();

      ///@todo Make sure the searched class is in the same namespace
      QList<Declaration*> decls = currentContext()->findDeclarations(QualifiedIdentifier(localId), editor()->findPosition(name->start_token, KDevelop::EditorIntegrator::FrontEdge) );

      if( !decls.isEmpty() )
      {
        foreach( Declaration* decl, decls )
          if( TemplateDeclaration* baseTemplateDecl = dynamic_cast<TemplateDeclaration*>(decl) ) {
            templateDecl->setSpecializedFrom(baseTemplateDecl);
            break;
          }

        if( !templateDecl->specializedFrom() )
          kDebug(9007) << "Could not find valid specialization-base" << localId.toString() << "for" << declaration->toString();
      }
    } else {
      kDebug(9007) << "Specialization of non-template class" << declaration->toString();
    }

  }

  declaration->setComment(comment());
  clearComment();

  setEncountered(declaration);

  openDeclarationInternal(declaration);

  return declaration;
}

Cpp::ClassDeclaration* DeclarationBuilder::openClassDefinition(NameAST* name, AST* range, bool collapseRange, ClassDeclarationData::ClassType classType) {
  Identifier id;

  if(!name) {
    //Unnamed class/struct, use a unique id
    static QAtomicInt& uniqueClassNumber( KDevelop::globalItemRepositoryRegistry().getCustomCounter("Unnamed Class Ids", 1) );
    id = Identifier::unique( uniqueClassNumber.fetchAndAddRelaxed(1) );
  }

  Cpp::ClassDeclaration* ret = openDeclaration<Cpp::ClassDeclaration>(name, range, id, collapseRange);
  DUChainWriteLocker lock(DUChain::lock());
  ret->setDeclarationIsDefinition(true);
  ret->clearBaseClasses();
  ret->setClassType(classType);
  return ret;
}

Declaration* DeclarationBuilder::openDefinition(NameAST* name, AST* rangeNode, bool collapseRange)
{
  Declaration* ret = openNormalDeclaration(name, rangeNode, KDevelop::Identifier(), collapseRange);

  DUChainWriteLocker lock(DUChain::lock());
  ret->setDeclarationIsDefinition(true);
  return ret;
}

Declaration* DeclarationBuilder::openNormalDeclaration(NameAST* name, AST* rangeNode, const Identifier& customName, bool collapseRange) {
  if(currentContext()->type() == DUContext::Class) {
    ClassMemberDeclaration* mem = openDeclaration<ClassMemberDeclaration>(name, rangeNode, customName, collapseRange);

    DUChainWriteLocker lock(DUChain::lock());
    mem->setAccessPolicy(currentAccessPolicy());
    return mem;
  } else if(currentContext()->type() == DUContext::Template) {
    return openDeclaration<TemplateParameterDeclaration>(name, rangeNode, customName, collapseRange);
  } else {
    return openDeclaration<Declaration>(name, rangeNode, customName, collapseRange);
  }
}

Declaration* DeclarationBuilder::openFunctionDeclaration(NameAST* name, AST* rangeNode) {

   QualifiedIdentifier id = identifierForNode(name);
   Identifier localId = id.last(); //This also copies the template arguments
   if(id.count() > 1) {
     //Merge the scope of the declaration, and put them tog. Add semicolons instead of the ::, so you can see it's not a qualified identifier.
     //Else the declarations could be confused with global functions.
     //This is done before the actual search, so there are no name-clashes while searching the class for a constructor.

     QString newId = id.last().identifier().str();
     for(int a = id.count()-2; a >= 0; --a)
       newId = id.at(a).identifier().str() + "::" + newId;

     localId.setIdentifier(newId);

     FunctionDefinition* ret = openDeclaration<FunctionDefinition>(name, rangeNode, localId);
     DUChainWriteLocker lock(DUChain::lock());
     ret->setDeclaration(0);
     return ret;
   }

  if(currentContext()->type() == DUContext::Class) {
    if(!m_collectQtFunctionSignature) {
      ClassFunctionDeclaration* fun = openDeclaration<ClassFunctionDeclaration>(name, rangeNode, localId);
      DUChainWriteLocker lock(DUChain::lock());
      fun->setAccessPolicy(currentAccessPolicy());
      fun->setIsAbstract(m_declarationHasInitializer);
      return fun;
    }else{
      QtFunctionDeclaration* fun = openDeclaration<QtFunctionDeclaration>(name, rangeNode, localId);
      DUChainWriteLocker lock(DUChain::lock());
      fun->setAccessPolicy(currentAccessPolicy());
      fun->setIsAbstract(m_declarationHasInitializer);
      fun->setIsSlot(m_accessPolicyStack.top() & FunctionIsSlot);
      fun->setIsSignal(m_accessPolicyStack.top() & FunctionIsSignal);
      QByteArray temp(QMetaObject::normalizedSignature("(" + m_qtFunctionSignature + ")"));
      IndexedString signature(temp.mid(1, temp.length()-2));
//       kDebug() << "normalized signature:" << signature.str() << "from:" << QString::fromUtf8(m_qtFunctionSignature);
      fun->setNormalizedSignature(signature);
      return fun;
    }
  } else if(m_inFunctionDefinition && (currentContext()->type() == DUContext::Namespace || currentContext()->type() == DUContext::Global)) {
    //May be a definition
     FunctionDefinition* ret = openDeclaration<FunctionDefinition>(name, rangeNode, localId);
     DUChainWriteLocker lock(DUChain::lock());
     ret->setDeclaration(0);
     return ret;
  }else{
    return openDeclaration<FunctionDeclaration>(name, rangeNode, localId);
  }
}

void DeclarationBuilder::classTypeOpened(AbstractType::Ptr type) {
  //We override this so we can get the class-declaration into a usable state(with filled type) earlier
    DUChainWriteLocker lock(DUChain::lock());

    IdentifiedType* idType = dynamic_cast<IdentifiedType*>(type.unsafeData());

    if( idType && !idType->declarationId().isValid() ) //When the given type has no declaration yet, assume we are declaring it now
        idType->setDeclaration( currentDeclaration() );

    currentDeclaration()->setType(type);
}

void DeclarationBuilder::closeDeclaration(bool forceInstance)
{
  if (lastType()) {
    DUChainWriteLocker lock(DUChain::lock());

    AbstractType::Ptr type = lastType();
    IdentifiedType* idType = dynamic_cast<IdentifiedType*>(type.unsafeData());
    DelayedType::Ptr delayed = type.cast<DelayedType>();

    //When the given type has no declaration yet, assume we are declaring it now.
    //If the type is a delayed type, it is a searched type, and not a declared one, so don't set the declaration then.
    if( !forceInstance && idType && !idType->declarationId().isValid() && !delayed ) {
        idType->setDeclaration( currentDeclaration() );
        //Q_ASSERT(idType->declaration() == currentDeclaration());
    }

    if(currentDeclaration()->kind() != Declaration::NamespaceAlias && currentDeclaration()->kind() != Declaration::Alias) {
      //If the type is not identified, it is an instance-declaration too, because those types have no type-declarations.
      if( (((!idType) || (idType && idType->declarationId() != currentDeclaration()->id())) && !currentDeclaration()->isTypeAlias() && !currentDeclaration()->isForwardDeclaration() ) || dynamic_cast<AbstractFunctionDeclaration*>(currentDeclaration()) || forceInstance )
        currentDeclaration()->setKind(Declaration::Instance);
      else
        currentDeclaration()->setKind(Declaration::Type);
    }

    currentDeclaration()->setType(lastType());
  }else{
    DUChainWriteLocker lock(DUChain::lock());
    currentDeclaration()->setAbstractType(AbstractType::Ptr());
  }

  eventuallyAssignInternalContext();

  ifDebugCurrentFile( DUChainReadLocker lock(DUChain::lock()); kDebug() << "closing declaration" << currentDeclaration()->toString() << "type" << (currentDeclaration()->abstractType() ? currentDeclaration()->abstractType()->toString() : QString("notype")) << "last:" << (lastType() ? lastType()->toString() : QString("(notype)")); )

  DeclarationBuilderBase::closeDeclaration();
}

void DeclarationBuilder::visitTypedef(TypedefAST *def)
{
  parseComments(def->comments);

  PushValue<bool> setInTypedef(m_inTypedef, true);

  DeclarationBuilderBase::visitTypedef(def);
}

void DeclarationBuilder::visitEnumSpecifier(EnumSpecifierAST* node)
{
  openDefinition(node->name, node, node->name == 0);

  DeclarationBuilderBase::visitEnumSpecifier(node);

  closeDeclaration();
}

void DeclarationBuilder::visitEnumerator(EnumeratorAST* node)
{
  //Ugly hack: Since we want the identifier only to have the range of the id(not
  //the assigned expression), we change the range of the node temporarily
  size_t oldEndToken = node->end_token;
  node->end_token = node->id + 1;

  Identifier id(editor()->parseSession()->token_stream->token(node->id).symbol());
  Declaration* decl = openNormalDeclaration(0, node, id);

  node->end_token = oldEndToken;

  DeclarationBuilderBase::visitEnumerator(node);

  EnumeratorType::Ptr enumeratorType = lastType().cast<EnumeratorType>();

  if(ClassMemberDeclaration* classMember = dynamic_cast<ClassMemberDeclaration*>(currentDeclaration())) {
    DUChainWriteLocker lock(DUChain::lock());
    classMember->setStatic(true);
  }

  closeDeclaration(true);

  if(enumeratorType) { ///@todo Move this into closeDeclaration in a logical way
    DUChainWriteLocker lock(DUChain::lock());
    enumeratorType->setDeclaration(decl);
    decl->setAbstractType(enumeratorType.cast<AbstractType>());
  }else if(!lastType().cast<DelayedType>()){ //If it's in a template, it may be DelayedType
    AbstractType::Ptr type = lastType();
    kWarning() << "not assigned enumerator type:" << typeid(*type.unsafeData()).name() << type->toString();
  }
}

void DeclarationBuilder::visitClassSpecifier(ClassSpecifierAST *node)
{
  PushValue<bool> setNotInTypedef(m_inTypedef, false);

  /**Open helper contexts around the class, so the qualified identifier matches.
   * Example: "class MyClass::RealClass{}"
   * Will create one helper-context named "MyClass" around RealClass
   * */

  SimpleCursor pos = editor()->findPosition(node->start_token, KDevelop::EditorIntegrator::FrontEdge);

  QualifiedIdentifier id;
  if( node->name ) {
    id = identifierForNode(node->name);
    openPrefixContext(node, id, pos);
  }

  int kind = editor()->parseSession()->token_stream->kind(node->class_key);
  
  openClassDefinition(node->name, node, node->name == 0, classTypeFromTokenKind(kind));

  if (kind == Token_struct || kind == Token_union)
    m_accessPolicyStack.push(Declaration::Public);
  else
    m_accessPolicyStack.push(Declaration::Private);

  DeclarationBuilderBase::visitClassSpecifier(node);

  eventuallyAssignInternalContext();

  if( node->name ) {
    ///Copy template default-parameters from the forward-declaration to the real declaration if possible
    DUChainWriteLocker lock(DUChain::lock());

    QList<Declaration*> declarations = Cpp::findDeclarationsSameLevel(currentContext(), id.last(), pos);

    foreach( Declaration* decl, declarations ) {
      if( decl->abstractType()) {
        ForwardDeclaration* forward =  dynamic_cast<ForwardDeclaration*>(decl);
        if( forward ) {
          {
            KDevelop::DUContext* forwardTemplateContext = forward->internalContext();
            if( forwardTemplateContext && forwardTemplateContext->type() == DUContext::Template ) {

              KDevelop::DUContext* currentTemplateContext = getTemplateContext(currentDeclaration());
              if( (bool)forwardTemplateContext != (bool)currentTemplateContext ) {
                kDebug(9007) << "Template-contexts of forward- and real declaration do not match: " << currentTemplateContext << getTemplateContext(currentDeclaration()) << currentDeclaration()->internalContext() << forwardTemplateContext << currentDeclaration()->internalContext()->importedParentContexts().count();
              } else if( forwardTemplateContext && currentTemplateContext ) {
                if( forwardTemplateContext->localDeclarations().count() != currentTemplateContext->localDeclarations().count() ) {
                } else {

                  const QVector<Declaration*>& forwardList = forwardTemplateContext->localDeclarations();
                  const QVector<Declaration*>& realList = currentTemplateContext->localDeclarations();

                  QVector<Declaration*>::const_iterator forwardIt = forwardList.begin();
                  QVector<Declaration*>::const_iterator realIt = realList.begin();

                  for( ; forwardIt != forwardList.end(); ++forwardIt, ++realIt ) {
                    TemplateParameterDeclaration* forwardParamDecl = dynamic_cast<TemplateParameterDeclaration*>(*forwardIt);
                    TemplateParameterDeclaration* realParamDecl = dynamic_cast<TemplateParameterDeclaration*>(*realIt);
                    if( forwardParamDecl && realParamDecl ) {
                      if( !forwardParamDecl->defaultParameter().isEmpty() )
                        realParamDecl->setDefaultParameter(forwardParamDecl->defaultParameter());
                    }
                  }
                }
              }
            }
          }

          //Update instantiations in case of template forward-declarations
//           SpecialTemplateDeclaration<ForwardDeclaration>* templateForward = dynamic_cast<SpecialTemplateDeclaration<ForwardDeclaration>* > (decl);
//           SpecialTemplateDeclaration<Declaration>* currentTemplate = dynamic_cast<SpecialTemplateDeclaration<Declaration>* >  (currentDeclaration());
//
//           if( templateForward && currentTemplate )
//           {
//             //Change the types of all the forward-template instantiations
//             TemplateDeclaration::InstantiationsHash instantiations = templateForward->instantiations();
//
//             for( TemplateDeclaration::InstantiationsHash::iterator it = instantiations.begin(); it != instantiations.end(); ++it )
//             {
//               Declaration* realInstance = currentTemplate->instantiate(it.key().args, ImportTrace());
//               Declaration* forwardInstance = dynamic_cast<Declaration*>(*it);
//               //Now change the type of forwardInstance so it matches the type of realInstance
//               CppClassType::Ptr realClass = realInstance->type<CppClassType>();
//               CppClassType::Ptr forwardClass = forwardInstance->type<CppClassType>();
//
//               if( realClass && forwardClass ) {
//                 //Copy the class from real into the forward-declaration's instance
//                 copyCppClass(realClass.data(), forwardClass.data());
//               } else {
//                 kDebug(9007) << "Bad types involved in formward-declaration";
//               }
//             }
//           }//templateForward && currentTemplate
        }
      }
    }//foreach

  }//node-name

  closeDeclaration();

  if(node->name)
    closePrefixContext(id);

  m_accessPolicyStack.pop();
}

void DeclarationBuilder::visitBaseSpecifier(BaseSpecifierAST *node) {
  DeclarationBuilderBase::visitBaseSpecifier(node);

  BaseClassInstance instance;
  {
    DUChainWriteLocker lock(DUChain::lock());

    instance.virtualInheritance = (bool)node->virt;
    instance.baseClass = lastType()->indexed();

    int tk = 0;
    if( node->access_specifier )
      tk = editor()->parseSession()->token_stream->token(node->access_specifier).kind;

    switch( tk ) {
      default:
      case Token_private:
        instance.access = KDevelop::Declaration::Private;
        break;
      case Token_public:
        instance.access = KDevelop::Declaration::Public;
        break;
      case Token_protected:
        instance.access = KDevelop::Declaration::Protected;
        break;
    }

    ClassDeclaration* currentClass = dynamic_cast<ClassDeclaration*>(currentDeclaration());
    if(currentClass) {
      currentClass->addBaseClass(instance);
    }else{
      kWarning() << "base-specifier without class declaration";
    }
  }
  addBaseType(instance);
}

QualifiedIdentifier DeclarationBuilder::resolveNamespaceIdentifier(const QualifiedIdentifier& identifier, const SimpleCursor& position)
{
  QList<DUContext*> contexts = currentContext()->findContexts(DUContext::Namespace, identifier, position);
  if( contexts.isEmpty() ) {
    //Failed to resolve namespace
    kDebug(9007) << "Failed to resolve namespace \"" << identifier << "\"";
    QualifiedIdentifier ret = identifier;
    ret.setExplicitlyGlobal(true);
    Q_ASSERT(ret.count());
    return ret;
  } else {
    QualifiedIdentifier ret = contexts.first()->scopeIdentifier(true);
    Q_ASSERT(ret.count());
    ret.setExplicitlyGlobal(true);
    return ret;
  }
}

void DeclarationBuilder::visitUsing(UsingAST * node)
{
  DeclarationBuilderBase::visitUsing(node);

  QualifiedIdentifier id = identifierForNode(node->name);

  ///@todo only use the last name component as range
  AliasDeclaration* decl = openDeclaration<AliasDeclaration>(0, node->name ? (AST*)node->name : (AST*)node, id.last());
  {
    DUChainWriteLocker lock(DUChain::lock());

    SimpleCursor pos = editor()->findPosition(node->start_token, KDevelop::EditorIntegrator::FrontEdge);
    QList<Declaration*> declarations = currentContext()->findDeclarations(id, pos);
    if(!declarations.isEmpty()) {
      decl->setAliasedDeclaration(declarations[0]);
    }else{
      kDebug() << "Aliased declaration not found:" << id.toString();
    }
  }

  closeDeclaration();
}

void DeclarationBuilder::visitUsingDirective(UsingDirectiveAST * node)
{
  DeclarationBuilderBase::visitUsingDirective(node);

  {
    DUChainReadLocker lock(DUChain::lock());
    if( currentContext()->type() != DUContext::Namespace && currentContext()->type() != DUContext::Global ) {
      ///@todo report problem
      kDebug(9007) << "Namespace-import used in non-global scope";
      return;
    }
  }

  if( compilingContexts() ) {
    NamespaceAliasDeclaration* decl = openDeclaration<NamespaceAliasDeclaration>(0, node, globalImportIdentifier);
    {
      DUChainWriteLocker lock(DUChain::lock());
      decl->setImportIdentifier( resolveNamespaceIdentifier(identifierForNode(node->name), currentDeclaration()->range().start) );
    }
    closeDeclaration();
  }
}

void DeclarationBuilder::visitTypeId(TypeIdAST * typeId)
{
  //TypeIdAST contains a declarator, but that one does not declare anything
  PushValue<bool> disableDeclarators(m_ignoreDeclarators, true);
  
  DeclarationBuilderBase::visitTypeId(typeId);
}

void DeclarationBuilder::visitNamespaceAliasDefinition(NamespaceAliasDefinitionAST* node)
{
  DeclarationBuilderBase::visitNamespaceAliasDefinition(node);

  {
    DUChainReadLocker lock(DUChain::lock());
    if( currentContext()->type() != DUContext::Namespace && currentContext()->type() != DUContext::Global ) {
      ///@todo report problem
      kDebug(9007) << "Namespace-alias used in non-global scope";
    }
  }

  if( compilingContexts() ) {
    NamespaceAliasDeclaration* decl = openDeclaration<NamespaceAliasDeclaration>(0, node, Identifier(editor()->parseSession()->token_stream->token(node->namespace_name).symbol()));
    {
      DUChainWriteLocker lock(DUChain::lock());
      decl->setImportIdentifier( resolveNamespaceIdentifier(identifierForNode(node->alias_name), currentDeclaration()->range().start) );
    }
    closeDeclaration();
  }
}

void DeclarationBuilder::visitElaboratedTypeSpecifier(ElaboratedTypeSpecifierAST* node)
{
  int kind = editor()->parseSession()->token_stream->kind(node->type);

  if( kind == Token_typename ) {
    //typename is completely handled by the type-builder
    DeclarationBuilderBase::visitElaboratedTypeSpecifier(node);
    return;
  }

  //For now completely ignore friend-class specifiers, because those currently are wrongly parsed as forward-declarations.
  if( !m_storageSpecifiers.isEmpty() && (m_storageSpecifiers.top() & ClassMemberDeclaration::FriendSpecifier) )
    return;

  bool openedDeclaration = false;

  if (node->name) {
    QualifiedIdentifier id = identifierForNode(node->name);

    bool forwardDeclarationGlobal = false;

    if(m_declarationHasInitDeclarators) {
      /**This is an elaborated type-specifier
       *
       * See iso c++ draft 3.3.4 for details.
       * Said shortly it means:
       * - Search for an existing declaration of the type. If it is found,
       *   it will be used, and we don't need to create a declaration.
       * - If it is not found, create a forward-declaration in the global/namespace scope.
       * - @todo While searching for the existing declarations, non-fitting overloaded names should be ignored.
       * */

      ///@todo think how this interacts with re-using duchains. In some cases a forward-declaration should still be created.
      QList<Declaration*> declarations;
      SimpleCursor pos = editor()->findPosition(node->start_token, KDevelop::EditorIntegrator::FrontEdge);

      {
        DUChainReadLocker lock(DUChain::lock());

        declarations = currentContext()->findDeclarations( id, pos);

        if(declarations.isEmpty()) {
          //We haven't found a declaration, so insert a forward-declaration in the global scope
          forwardDeclarationGlobal = true;
        }else{
          //We have found a declaration. Do not create a new one, instead use the declarations type.
          if(declarations.first()->abstractType()) {
            //This belongs into the type-builder, but it's much easier to do here, since we already have all the information
            ///@todo See above, only search for fitting declarations(of structure/enum/class/union type)
            injectType(declarations.first()->abstractType());
            return;
          }else{
            kDebug(9007) << "Error: Bad declaration";
          }
        }
      }
    }

    // Create forward declaration
    switch (kind) {
      case Token_class:
      case Token_struct:
      case Token_union:
      case Token_enum:

        if(forwardDeclarationGlobal) {
          //Open the global context, so it is currentContext() and we can insert the forward-declaration there

          DUContext* globalCtx;
          {
            DUChainReadLocker lock(DUChain::lock());
            globalCtx = currentContext();
            while(globalCtx && globalCtx->type() != DUContext::Global && globalCtx->type() != DUContext::Namespace)
              globalCtx = globalCtx->parentContext();
            Q_ASSERT(globalCtx);
          }

          //Just temporarily insert the new context
          LockedSmartInterface iface = editor()->smart();
          injectContext( iface, globalCtx, currentContext()->smartRange() );
        }

        openForwardDeclaration(node->name, node);

        if(forwardDeclarationGlobal) {
          LockedSmartInterface iface = editor()->smart();
          closeInjectedContext(iface);
        }

        openedDeclaration = true;
        break;
    }
  }

  DeclarationBuilderBase::visitElaboratedTypeSpecifier(node);

  if (openedDeclaration) {
/*    DUChainWriteLocker lock(DUChain::lock());
    //Resolve forward-declarations that are declared after the real type was already declared
    Q_ASSERT(dynamic_cast<ForwardDeclaration*>(currentDeclaration()));
    IdentifiedType* idType = dynamic_cast<IdentifiedType*>(lastType().data());
    if(idType && idType->declaration())
      static_cast<ForwardDeclaration*>(currentDeclaration())->setResolved(idType->declaration());*/
    closeDeclaration();
  }
}


void DeclarationBuilder::visitAccessSpecifier(AccessSpecifierAST* node)
{
  bool isSlot = false;
  bool isSignal = false;
  if (node->specs) {
    const ListNode<std::size_t> *it = node->specs->toFront();
    const ListNode<std::size_t> *end = it;
    do {
      int kind = editor()->parseSession()->token_stream->kind(it->element);
      switch (kind) {
        case Token_slots:
        case Token_k_dcop:
          isSlot = true;
          break;
        case Token_public:
          setAccessPolicy(Declaration::Public);
          break;
        case Token_k_dcop_signals:
        case Token_signals:
          isSignal = true;
        case Token_protected:
          setAccessPolicy(Declaration::Protected);
          break;
        case Token_private:
          setAccessPolicy(Declaration::Private);
          break;
      }

      it = it->next;
    } while (it != end);
  }
  
  if(isSignal)
    setAccessPolicy((KDevelop::Declaration::AccessPolicy)(currentAccessPolicy() | FunctionIsSignal));

  if(isSlot)
    setAccessPolicy((KDevelop::Declaration::AccessPolicy)(currentAccessPolicy() | FunctionIsSlot));
  

  DeclarationBuilderBase::visitAccessSpecifier(node);
}

void DeclarationBuilder::parseStorageSpecifiers(const ListNode<std::size_t>* storage_specifiers)
{
  ClassMemberDeclaration::StorageSpecifiers specs = 0;

  if (storage_specifiers) {
    const ListNode<std::size_t> *it = storage_specifiers->toFront();
    const ListNode<std::size_t> *end = it;
    do {
      int kind = editor()->parseSession()->token_stream->kind(it->element);
      switch (kind) {
        case Token_friend:
          specs |= ClassMemberDeclaration::FriendSpecifier;
          break;
        case Token_auto:
          specs |= ClassMemberDeclaration::AutoSpecifier;
          break;
        case Token_register:
          specs |= ClassMemberDeclaration::RegisterSpecifier;
          break;
        case Token_static:
          specs |= ClassMemberDeclaration::StaticSpecifier;
          break;
        case Token_extern:
          specs |= ClassMemberDeclaration::ExternSpecifier;
          break;
        case Token_mutable:
          specs |= ClassMemberDeclaration::MutableSpecifier;
          break;
      }

      it = it->next;
    } while (it != end);
  }

  m_storageSpecifiers.push(specs);
}

void DeclarationBuilder::parseFunctionSpecifiers(const ListNode<std::size_t>* function_specifiers)
{
  AbstractFunctionDeclaration::FunctionSpecifiers specs = 0;

  if (function_specifiers) {
    const ListNode<std::size_t> *it = function_specifiers->toFront();
    const ListNode<std::size_t> *end = it;
    do {
      int kind = editor()->parseSession()->token_stream->kind(it->element);
      switch (kind) {
        case Token_inline:
          specs |= AbstractFunctionDeclaration::InlineSpecifier;
          break;
        case Token_virtual:
          specs |= AbstractFunctionDeclaration::VirtualSpecifier;
          break;
        case Token_explicit:
          specs |= AbstractFunctionDeclaration::ExplicitSpecifier;
          break;
      }

      it = it->next;
    } while (it != end);
  }

  m_functionSpecifiers.push(specs);
}

void DeclarationBuilder::visitParameterDeclaration(ParameterDeclarationAST* node) {
  DeclarationBuilderBase::visitParameterDeclaration(node);
  AbstractFunctionDeclaration* function = currentDeclaration<AbstractFunctionDeclaration>();

  if( function ) {
    if( node->expression ) {
      //Fill default-parameters
      QString defaultParam = stringFromSessionTokens( editor()->parseSession(), node->expression->start_token, node->expression->end_token ).trimmed();

      function->addDefaultParameter(IndexedString(defaultParam));
    }
  }
}


void DeclarationBuilder::popSpecifiers()
{
  m_functionSpecifiers.pop();
  m_storageSpecifiers.pop();
}

void DeclarationBuilder::applyStorageSpecifiers()
{
  if (!m_storageSpecifiers.isEmpty() && m_storageSpecifiers.top() != 0)
    if (ClassMemberDeclaration* member = dynamic_cast<ClassMemberDeclaration*>(currentDeclaration())) {
      DUChainWriteLocker lock(DUChain::lock());

      member->setStorageSpecifiers(m_storageSpecifiers.top());
    }
}

void DeclarationBuilder::applyFunctionSpecifiers()
{
  DUChainWriteLocker lock(DUChain::lock());
  AbstractFunctionDeclaration* function = dynamic_cast<AbstractFunctionDeclaration*>(currentDeclaration());
  if(!function)
    return;
  
  if (!m_functionSpecifiers.isEmpty() && m_functionSpecifiers.top() != 0) {

    function->setFunctionSpecifiers(m_functionSpecifiers.top());
  }else{
    function->setFunctionSpecifiers((AbstractFunctionDeclaration::FunctionSpecifiers)0);
  }
  
  ///Eventually inherit the "virtual" flag from overridden functions
  ClassFunctionDeclaration* classFunDecl = dynamic_cast<ClassFunctionDeclaration*>(function);
  if(classFunDecl && !classFunDecl->isVirtual()) {
    QList<Declaration*> overridden;
    foreach(const DUContext::Import &import, currentContext()->importedParentContexts())
      overridden += import.context(topContext())->findDeclarations(QualifiedIdentifier(classFunDecl->identifier()),
                                            SimpleCursor::invalid(), classFunDecl->abstractType(), classFunDecl->topContext(), DUContext::DontSearchInParent);
    if(!overridden.isEmpty()) {
      foreach(Declaration* decl, overridden) {
        if(AbstractFunctionDeclaration* fun = dynamic_cast<AbstractFunctionDeclaration*>(decl))
          if(fun->isVirtual())
            classFunDecl->setVirtual(true);
      }
    }
  }
}

bool DeclarationBuilder::checkParameterDeclarationClause(ParameterDeclarationClauseAST* clause)
{
    {
      DUChainReadLocker lock(DUChain::lock());
      if(currentContext()->type() == DUContext::Other) //Cannot declare a function in a code-context
        return false; ///@todo create warning/error
    }
    if(!clause || !clause->parameter_declarations)
      return true;
    AbstractType::Ptr oldLastType = lastType();

    const ListNode<ParameterDeclarationAST*> *start = clause->parameter_declarations->toFront();

    const ListNode<ParameterDeclarationAST*> *it = start;

    bool ret = false;

    do {
      ParameterDeclarationAST* ast = it->element;
      if(ast) {
        if(m_collectQtFunctionSignature) {
          size_t endToken = ast->end_token;
          if(ast->expression)
            endToken = ast->expression->start_token;
          if(ast->declarator && ast->declarator->id)
            endToken = ast->declarator->id->start_token;
          
          if(!m_qtFunctionSignature.isEmpty())
            m_qtFunctionSignature += ", ";
          
          m_qtFunctionSignature += editor()->tokensToByteArray(ast->start_token, endToken);
          ret = true;
        }else{
        if(ast->expression || ast->declarator) {
          ret = true; //If one parameter has a default argument or a parameter name, it is surely a parameter
          break;
        }

        visit(ast->type_specifier);
        if( lastType() ) {
          //Break on the first valid thing found
          if( lastTypeWasInstance() ) {
            ret = false;
            break;
          }else{
            ret = true;
            break;
          }
        }
        }
      }
      it = it->next;
    } while (it != start);

    setLastType(oldLastType);

    return ret;
}