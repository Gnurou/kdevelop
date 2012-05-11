/* This file is part of KDevelop
   Copyright 2012 Alexandre Courbot <gnurou@gmail.com>

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
#ifndef CPP_LANGUAGE_FEATURES_H
#define CPP_LANGUAGE_FEATURES_H

typedef unsigned long CPPLanguageFeatures;
static const CPPLanguageFeatures CPP_FEAT_C99   (1 << 0);
static const CPPLanguageFeatures CPP_FEAT_CPP   (1 << 1);
static const CPPLanguageFeatures CPP_FEAT_CPP11 (1 << 2);

static const CPPLanguageFeatures DEFAULT_CPP_LANGUAGE_FEATURES(CPP_FEAT_CPP | CPP_FEAT_CPP11);

#endif
