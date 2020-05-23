// -*- mode: C++; c-file-style: "cc-mode" -*-
//*************************************************************************
// DESCRIPTION: Verilator: Emit Verilog from tree
//
// Code available from: https://verilator.org
//
//*************************************************************************
//
// Copyright 2020 by Wilson Snyder. This program is free software; you can
// redistribute it and/or modify it under the terms of either the GNU Lesser
// General Public License Version 3 or the Perl Artistic License Version 2.0.
// SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0
//
//*************************************************************************

#include "verilatedos.h"

#include "V3File.h"
#include "V3WaiveFile.h"

#include <memory>
#include <fstream>
#include <sstream>

void V3WaiveFile::addEntry(V3ErrorCode errorCode, const std::string filename,
                           const std::string& str) {
    std::stringstream entry;
    entry << "lint_off -rule " << errorCode.ascii() << " -file \"*" << filename << "\" -match \""
          << str << "\"";
    s_waiveList.push_back(entry.str());
}

void V3WaiveFile::write(const std::string filename) {
    const vl_unique_ptr<std::ofstream> ofp(V3File::new_ofstream(filename));
    if (ofp->fail()) v3fatal("Can't write " << filename);

    *ofp << "`verilator_config" << std::endl << std::endl;

    for (V3WaiveFile::WaiveList::const_iterator it = s_waiveList.begin(); it != s_waiveList.end();
         ++it) {
        *ofp << "// T"  // this word is detected as actual thing to do by some IDEs, split..
                "ODO: Fix or keep to ignore?"
             << std::endl;
        *ofp << *it << std::endl << std::endl;
    }
}

V3WaiveFile::WaiveList V3WaiveFile::s_waiveList;
