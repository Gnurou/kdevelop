#include <clang-c/Index.h>

#define TESTSRC "/home/gnurou/Work/KDE/kdevelop/test.c"

// Follow blocks using clang_getCursorExtent?

struct clientData {
    int indent;
};

enum CXChildVisitResult visitCursor(CXCursor cursor, CXCursor parent, CXClientData client_data)
{
    struct clientData *cData = (struct clientData *)client_data;
    CXChildVisitResult res = CXChildVisit_Recurse;

    CXCursorKind kind = clang_getCursorKind(cursor);
    CXString kindSpelling = clang_getCursorKindSpelling(kind);
    CXString spelling = clang_getCursorSpelling(cursor);
    CXSourceLocation sl = clang_getCursorLocation(cursor);
    unsigned int line, col;
    clang_getSpellingLocation(sl, NULL, &line, &col, NULL);
    printf("%3d,%3d: ", line, col);
    for (int i = 0 ; i < cData->indent; i++)
        printf(" ");
    printf("%s %s", clang_getCString(kindSpelling), clang_getCString(spelling));

    CXType ctype = clang_getCursorType(cursor);
    CXString ctkind = clang_getTypeKindSpelling(ctype.kind);
    CXCursor ctdecl = clang_getTypeDeclaration(ctype);
    CXCursorKind ctrkind = clang_getCursorKind(ctdecl);
    CXString ctrname = clang_getCursorSpelling(ctdecl);
    CXString ctrkindSpelling = clang_getCursorKindSpelling(ctrkind);

    if (ctype.kind != CXType_Invalid) {
        printf(" type %s", clang_getCString(ctkind));
    }

    if (ctrkind != CXCursor_NoDeclFound) {
        printf(" declared by %s %s", clang_getCString(ctrkindSpelling), clang_getCString(ctrname));
    }

    clang_disposeString(ctrname);
    clang_disposeString(ctrkindSpelling);
    clang_disposeString(ctkind);

    if (clang_isDeclaration(kind)) {
    } else if (clang_isReference(kind)) {
        CXCursor ref = clang_getCursorReferenced(cursor);
        CXString refName = clang_getCursorSpelling(ref);
        sl = clang_getCursorLocation(ref);
        clang_getExpansionLocation(sl, NULL, &line, &col, NULL);
        printf(" references %s (%d %d)", clang_getCString(refName), line, col);
        clang_disposeString(refName);
    }
    clang_disposeString(spelling);
    clang_disposeString(kindSpelling);
    printf("\n");
    cData->indent += 4;
    clang_visitChildren(cursor, visitCursor, client_data);
    cData->indent -= 4;
    res = CXChildVisit_Continue;
    return res;
}

int main(int argc, const char *argv[])
{
    CXIndex index = clang_createIndex(0, 0);
    CXTranslationUnit tu = clang_parseTranslationUnit(index, 0, argv, argc, 0, 0, CXTranslationUnit_DetailedPreprocessingRecord);
    struct clientData cData = { 0 };

    for (unsigned i = 0, n = clang_getNumDiagnostics(tu); i < n; i++) {
        CXDiagnostic diag = clang_getDiagnostic(tu, i);
        CXString str = clang_getDiagnosticSpelling(diag);
        CXSourceLocation loc = clang_getDiagnosticLocation(diag);
        unsigned line, column;
        clang_getExpansionLocation(loc, NULL, &line, &column, NULL);
        fprintf(stderr, "%d %d (%d %d): %s\n", line, column, clang_getDiagnosticNumRanges(diag), clang_getDiagnosticNumFixIts(diag), clang_getCString(str));
        clang_disposeString(str);

        for (unsigned j = 0, n = clang_getDiagnosticNumFixIts(diag); j < n; j++) {
            CXString str = clang_getDiagnosticFixIt(diag, j, NULL);
            fprintf(stderr, "   %s\n", clang_getCString(str));
            clang_disposeString(str);
        }
        clang_disposeDiagnostic(diag);
    }

    clang_visitChildren(clang_getTranslationUnitCursor(tu), visitCursor, &cData);
    
    clang_disposeTranslationUnit(tu);
    clang_disposeIndex(index);
    return 0;
}
