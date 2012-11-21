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
#ifndef CLANGDIAGNOSTICS_H
#define CLANGDIAGNOSTICS_H

#include <clang/Basic/Diagnostic.h>
#include <clang/Basic/LangOptions.h>

#include <language/duchain/indexedstring.h>
#include <language/interfaces/iproblem.h>

class KDevDiagnosticConsumer : public clang::DiagnosticConsumer
{
public:
    KDevDiagnosticConsumer(clang::LangOptions _lo, KDevelop::IndexedString url) : lo(_lo), _url(url) {}
    virtual void HandleDiagnostic(clang::DiagnosticsEngine::Level DiagLevel, const clang::Diagnostic &Info);
    virtual DiagnosticConsumer *clone(clang::DiagnosticsEngine &Diags) const;

    clang::LangOptions lo;

private:
    KDevelop::IndexedString _url;
};

#endif
