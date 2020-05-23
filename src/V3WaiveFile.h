// -*- mode: C++; c-file-style: "cc-mode" -*-
//*************************************************************************
// DESCRIPTION: Verilator: Emit XML code
//
// Code available from: https://verilator.org
//
//*************************************************************************
//
// Copyright 2003-2020 by Wilson Snyder. This program is free software; you
// can redistribute it and/or modify it under the terms of either the GNU
// Lesser General Public License Version 3 or the Perl Artistic License
// Version 2.0.
// SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0
//
//*************************************************************************

#ifndef _V3WAIVEFILE_H_
#define _V3WAIVEFILE_H_ 1

#include "V3Error.h"

#include <vector>
#include <string>

class V3WaiveFile {
    // TYPES
    typedef std::vector<std::string> WaiveList;
    static WaiveList s_waiveList;

public:
    static void addEntry(V3ErrorCode errorCode, std::string const filename,
                         std::string const& str);
    static void write(std::string filename);
};

#endif  // Guard
