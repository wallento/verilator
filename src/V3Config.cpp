// -*- mode: C++; c-file-style: "cc-mode" -*-
//*************************************************************************
// DESCRIPTION: Verilator: Configuration Files
//
// Code available from: https://verilator.org
//
//*************************************************************************
//
// Copyright 2010-2019 by Wilson Snyder.  This program is free software; you can
// redistribute it and/or modify it under the terms of either the GNU
// Lesser General Public License Version 3 or the Perl Artistic License
// Version 2.0.
//
// Verilator is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
//*************************************************************************

#include "config_build.h"
#include "verilatedos.h"

#include "V3Global.h"
#include "V3String.h"
#include "V3Config.h"

#include <map>
#include <set>
#include <string>

//######################################################################
// lint/coverage/tracing on/off

class V3ConfigIgnoresLine {
public:
    int         m_lineno;       // Line number to make change at
    V3ErrorCode m_code;         // Error code
    bool        m_on;           // True to enable message
    V3ConfigIgnoresLine(V3ErrorCode code, int lineno, bool on)
        : m_lineno(lineno), m_code(code), m_on(on) {}
    ~V3ConfigIgnoresLine() {}
    inline bool operator< (const V3ConfigIgnoresLine& rh) const {
        if (m_lineno<rh.m_lineno) return true;
        if (m_lineno>rh.m_lineno) return false;
        if (m_code<rh.m_code) return true;
        if (m_code>rh.m_code) return false;
        // Always turn "on" before "off" so that overlapping lines will end
        // up finally with the error "off"
        return (m_on>rh.m_on);
    }
};
std::ostream& operator<<(std::ostream& os, V3ConfigIgnoresLine rhs) {
    return os<<rhs.m_lineno<<", "<<rhs.m_code<<", "<<rhs.m_on; }

class V3ConfigIgnores {
    typedef std::multiset<V3ConfigIgnoresLine> IgnLines;  // list of {line,code,on}
    typedef std::map<string,IgnLines> IgnFiles;  // {filename} => list of {line,code,on}

    // MEMBERS
    string              m_lastFilename;  // Last filename looked up
    int                 m_lastLineno;    // Last linenumber looked up

    IgnLines::const_iterator m_lastIt;   // Point with next linenumber > current line number
    IgnLines::const_iterator m_lastEnd;  // Point with end()

    IgnFiles            m_ignWilds;      // Ignores for each wildcarded filename
    IgnFiles            m_ignFiles;      // Ignores for each non-wildcarded filename

    static V3ConfigIgnores s_singleton;  // Singleton (not via local static, as that's slow)

    V3ConfigIgnores() { m_lastLineno = -1; }
    ~V3ConfigIgnores() {}

    // METHODS
    inline IgnLines* findWilds(const string& wildname) {
        IgnFiles::iterator it = m_ignWilds.find(wildname);
        if (it != m_ignWilds.end()) {
            return &(it->second);
        } else {
            m_ignWilds.insert(make_pair(wildname, IgnLines()));
            it = m_ignWilds.find(wildname);
            return &(it->second);
        }
    }
    inline void absBuild(const string& filename) {
        // Given a filename, find all wildcard matches against it and build
        // hash with the specific filename.  This avoids having to wildmatch
        // more than once against any filename.
        IgnFiles::iterator it = m_ignFiles.find(filename);
        if (it == m_ignFiles.end()) {
            // Haven't seen this filename before
            m_ignFiles.insert(make_pair(filename, IgnLines()));
            it = m_ignFiles.find(filename);
            // Make new list for this file of all matches
            for (IgnFiles::iterator fnit = m_ignWilds.begin(); fnit != m_ignWilds.end(); ++fnit) {
                if (VString::wildmatch(filename, fnit->first)) {
                    for (IgnLines::iterator lit = fnit->second.begin();
                         lit != fnit->second.end(); ++lit) {
                        it->second.insert(*lit);
                    }
                }
            }
        }
        m_lastIt = it->second.begin();
        m_lastEnd = it->second.end();
    }

public:
    inline static V3ConfigIgnores& singleton() { return s_singleton; }

    void addIgnore(V3ErrorCode code, const string& wildname, int lineno, bool on) {
        // Insert
        IgnLines* linesp = findWilds(wildname);
        UINFO(9,"config addIgnore "<<wildname<<":"<<lineno<<", "<<code<<", "<<on<<endl);
        linesp->insert(V3ConfigIgnoresLine(code, lineno, on));
        // Flush the match cache, due to a change in the rules.
        m_ignFiles.clear();
        m_lastFilename = " ";
    }
    inline void applyIgnores(FileLine* filelinep) {
        // HOT routine, called each parsed token line
        if (m_lastLineno != filelinep->lastLineno()
            || m_lastFilename != filelinep->filename()) {
            //UINFO(9,"   ApplyIgnores for "<<filelinep->ascii()<<endl);
            if (VL_UNLIKELY(m_lastFilename != filelinep->filename())) {
                absBuild(filelinep->filename());
                m_lastFilename = filelinep->filename();
            }
            // Process all on/offs for lines up to and including the current line
            int curlineno = filelinep->lastLineno();
            for (; m_lastIt != m_lastEnd; ++m_lastIt) {
                if (m_lastIt->m_lineno > curlineno) break;
                //UINFO(9,"     Hit "<<*m_lastIt<<endl);
                filelinep->warnOn(m_lastIt->m_code, m_lastIt->m_on);
            }
            if (0 && debug() >= 9) {
                for (IgnLines::const_iterator it=m_lastIt; it != m_lastEnd; ++it) {
                    UINFO(9,"     NXT "<<*it<<endl);
                }
            }
            m_lastLineno = filelinep->lastLineno();
        }
    }
};

V3ConfigIgnores V3ConfigIgnores::s_singleton;

//######################################################################
// variable attributes (public, clock, isolate_assignments, ...)

class V3ConfigVarAttr {
    struct AttrEntry {
        AstAttrType type;
        AstSenTree* sentreep;
    };
    typedef std::vector<AttrEntry> AttrEntryVector; //< Vector of the attributes, as one var can have many
    typedef std::map<string,AttrEntryVector> VarAttrMap; //< Map from var (wcard) to list of attributes
    typedef std::map<string,VarAttrMap> VarAttrMapFTask; //< Map from ftask (wcard) to map of vars
    typedef std::map<string,VarAttrMapFTask> AttrMap; //< Map from module (wcard) to map of ftask and below
    typedef std::unordered_set<std::string> FunctionList; //< List of functions for isolation
    typedef std::map<std::string,FunctionList> FunctionMap; //< Map modules to function lists

    AttrMap m_varAttrs; //< Configured attributes
    FunctionMap m_functionIsolate; //< Configured function isolations

    static V3ConfigVarAttr s_singleton;  // Singleton (not via local static, as that's slow)
    V3ConfigVarAttr() {}
    ~V3ConfigVarAttr() {}

    void applyVarAttr(AstVar* varp, AttrEntry entry) {
        AstNode* newp = new AstAttrOf(varp->fileline(), entry.type);
        varp->addAttrsp(newp);
        if (entry.type == AstAttrType::VAR_PUBLIC_FLAT_RW) {
            newp->addNext(new AstAlwaysPublic(varp->fileline(), entry.sentreep, NULL));
        }
    }
    void applyVarAttrForTask(const VarAttrMap& map, AstVar* varp) {
        const string var = varp->name();
        for (VarAttrMap::const_iterator it = map.begin(); it != map.end(); ++it) {
            if (VString::wildmatch(var, it->first)) {
                for (AttrEntryVector::const_iterator eit = it->second.begin();
                     eit != it->second.end(); ++eit) {
                    applyVarAttr(varp, *eit);
                }
            }
        }
    }
    void applyVarAttrForModule(const VarAttrMapFTask& map, const string& ftask, AstVar* varp) {
        for (VarAttrMapFTask::const_iterator it = map.begin(); it != map.end(); ++it) {
            if (VString::wildmatch(ftask, it->first)) {
                applyVarAttrForTask(it->second, varp);
            }
        }
    }
    void applyFunctionIsolate(const FunctionList& list, AstNodeFTask* funcp) {
        const string& func = funcp->name();
        for (FunctionList::const_iterator it = list.begin(); it != list.end(); ++it) {
            if (VString::wildmatch(func, *it)) {
                funcp->attrIsolateAssign(true);
            }
        }
    }
public:
    inline static V3ConfigVarAttr& singleton() { return s_singleton; }

    void addVarAttr(FileLine* fl, const string& module, const string& ftask, const string& var,
                    AstAttrType type, AstSenTree* sensep) {
        m_varAttrs[module][ftask][var].push_back({type, sensep});
    }
    void addFunctionIsolate(const string& module, const string& function) {
        m_functionIsolate[module].insert(function);
    }

    void applyVarAttr(const AstNodeModule* modulep, const AstNodeFTask* ftaskp, AstVar* varp) {
        const string& module = modulep->name();
        const string& ftask = (ftaskp ? ftaskp->name() :  "");

        for (AttrMap::const_iterator it = m_varAttrs.begin(); it != m_varAttrs.end(); ++it) {
            if (VString::wildmatch(module, it->first)) {
                applyVarAttrForModule(it->second, ftask, varp);
            }
        }
    }
    void applyFunctionIsolate(const AstNodeModule* modulep, AstNodeFTask* funcp) {
        FunctionMap::const_iterator it;
        const string &module = modulep->name();

        for (it = m_functionIsolate.begin(); it != m_functionIsolate.end(); ++it) {
            if (VString::wildmatch(module, it->first)) {
                applyFunctionIsolate(it->second, funcp);
            }
        }
    }
};

V3ConfigVarAttr V3ConfigVarAttr::s_singleton;

//######################################################################
// coverage_block

class V3ConfigCoverageBlock {
    typedef std::vector<std::pair<string, int> > FileLineMap;
    typedef std::unordered_set<std::string> NameList; //< Names in a module to match
    typedef std::map<std::string, NameList> NameMap; //< Map modules to names

    FileLineMap m_lines;
    NameMap m_named;

    static V3ConfigCoverageBlock s_singleton;
    V3ConfigCoverageBlock() {}
    ~V3ConfigCoverageBlock() {}

    void applyNamed(const AstNodeModule* modulep, AstBegin* nodep) {
        const string& module = modulep->name();
        const string& block = nodep->name();
        for (NameMap::const_iterator mit = m_named.begin(); mit != m_named.end(); ++mit) {
            if (VString::wildmatch(module, mit->first)) {
                for (NameList::const_iterator it = mit->second.begin();
                        it != mit->second.end(); ++it) {
                    if (VString::wildmatch(block, *it)) {
                        nodep->addStmtsp(new AstPragma(nodep->fileline(),
                                                        AstPragmaType::COVERAGE_BLOCK_OFF));
                    }
                }
            }
        }
    }

public:
    inline static V3ConfigCoverageBlock& singleton() { return s_singleton; }

    void addFileline(const string &file, int line) {
        m_lines.push_back(make_pair(file, line));
    }
    void addNamed(const string& module, const string& blockname) {
        m_named[module].insert(blockname);
    }

    void applyCoverageBlock(AstNodeModule* modulep, AstBegin* nodep) {
        if (!nodep->unnamed()) {
            applyNamed(modulep, nodep);
        } else {
            FileLine *fl = nodep->fileline();
            for (FileLineMap::const_iterator it = m_lines.begin(); it != m_lines.end(); ++it) {
                if ((it->first == fl->filename()) && (it->second == fl->lineno())) {
                    nodep->addStmtsp(new AstPragma(nodep->fileline(),
                                     AstPragmaType::COVERAGE_BLOCK_OFF));
                }
            }
        }
    }
};

V3ConfigCoverageBlock V3ConfigCoverageBlock::s_singleton;

//######################################################################
// module inline and public

class V3ConfigModule {
    typedef std::map<string, bool> Inlines;
    typedef std::unordered_set<string> Publics;

    Inlines m_inlines;
    Publics m_publics;

    static V3ConfigModule s_singleton;
    V3ConfigModule() {}
    ~V3ConfigModule() {}
public:
    inline static V3ConfigModule& singleton() { return s_singleton; }

    void addInline(const string& module, bool on) {
        m_inlines[module] = on;
    }
    void addPublic(const string& module) {
        m_publics.insert(module);
    }

    void applyModule(AstNodeModule* modp) {
        const string& module = modp->name();

        for (Inlines::const_iterator it = m_inlines.begin(); it != m_inlines.end(); ++it) {
            if (VString::wildmatch(module, it->first)) {
                AstPragmaType type = it->second ? AstPragmaType::INLINE_MODULE
                                                : AstPragmaType::NO_INLINE_MODULE;
                AstNode* nodep = new AstPragma(modp->fileline(), type);
                modp->addStmtp(nodep);
            }
        }

        for (Publics::const_iterator it = m_publics.begin(); it != m_publics.end(); ++it) {
            if (VString::wildmatch(module, *it)) {
                AstNode* nodep = new AstPragma(modp->fileline(), AstPragmaType::PUBLIC_MODULE);
                modp->addStmtp(nodep);
            }
        }
    }
};

V3ConfigModule V3ConfigModule::s_singleton;

//######################################################################
// function/task inline, public

class V3ConfigFTask {
    typedef enum { INLINE, PUBLIC } ConfigType;
    typedef std::vector<std::pair<string, ConfigType> > FTaskList;
    typedef std::map<string, FTaskList> FTaskMap;

    FTaskMap m_configs;

    static V3ConfigFTask s_singleton;
    V3ConfigFTask() {}
    ~V3ConfigFTask() {}
public:
    inline static V3ConfigFTask& singleton() { return s_singleton; }

    void addFTaskInline(const string& module, const string& task) {
        m_configs[module].push_back(make_pair(task, INLINE));
    }
    void addFTaskPublic(const string& module, const string& task) {
        m_configs[module].push_back(make_pair(task, PUBLIC));
    }

    void applyFTask(const AstNodeModule* modulep, AstNodeFTask* ftaskp) {
        const string& module = modulep->name();
        const string& ftask = ftaskp->name();
        FTaskMap::const_iterator mit;
        for (mit = m_configs.begin(); mit != m_configs.end(); ++mit) {
            if (VString::wildmatch(module, mit->first)) {
                FTaskList::const_iterator fit;
                for (fit = mit->second.begin(); fit != mit->second.end(); ++fit) {
                    if (VString::wildmatch(ftask, fit->first)) {
                        AstPragmaType type;
                        if (fit->second == INLINE) type = AstPragmaType::NO_INLINE_TASK;
                        else if (fit->second == PUBLIC) type = AstPragmaType::PUBLIC_TASK;
                        ftaskp->addStmtsp(new AstPragma(ftaskp->fileline(), type));
                    }
                }
            }
        }
    }
};

V3ConfigFTask V3ConfigFTask::s_singleton;

//######################################################################
// full/parallel case

class V3ConfigCase {
    typedef std::map<string, std::unordered_set<int>> CaseList;

    CaseList m_parallels;
    CaseList m_fulls;

    static V3ConfigCase s_singleton;
    V3ConfigCase() {}
    ~V3ConfigCase() {}
public:
    inline static V3ConfigCase& singleton() { return s_singleton; }

    void addCaseFull(const string& file, int line) {
        m_fulls[file].insert(line);
    }
    void addCaseParallel(const string& file, int line) {
        m_parallels[file].insert(line);
    }

    void applyCase(AstCase* nodep) {
        const string& file = nodep->fileline()->filename();
        int lineno = nodep->fileline()->lineno();

        for (CaseList::const_iterator it = m_parallels.begin();
             it != m_parallels.end(); ++it) {
            if (VString::wildmatch(file, it->first)) {
                if ((m_parallels[file].find(0) != m_parallels[file].end())
                    || (m_parallels[file].find(lineno) != m_parallels[file].end())) {
                    nodep->parallelPragma(true);
                }
            }
        }
        for (CaseList::const_iterator it = m_fulls.begin();
             it != m_fulls.end(); ++it) {
            if (VString::wildmatch(file, it->first)) {
                if ((m_fulls[file].find(0) != m_fulls[file].end()) ||
                    (m_fulls[file].find(lineno) != m_fulls[file].end())) {
                    nodep->fullPragma(true);
                }
            }
        }
    }
};

V3ConfigCase V3ConfigCase::s_singleton;

//######################################################################
// V3Config

void V3Config::addCaseFull(const string& file, int lineno) {
    V3ConfigCase::singleton().addCaseFull(file, lineno);
}

void V3Config::addCaseParallel(const string& file, int lineno) {
    V3ConfigCase::singleton().addCaseParallel(file, lineno);
}

void V3Config::addCoverageBlockOff(const string& file, int line) {
    V3ConfigCoverageBlock::singleton().addFileline(file, line);
}

void V3Config::addCoverageBlockOff(const string& entity, const string& blockname) {
    V3ConfigCoverageBlock::singleton().addNamed(entity, blockname);
}

void V3Config::addIgnore(V3ErrorCode code, bool on, const string& filename, int min, int max) {
    if (filename=="*") {
        FileLine::globalWarnOff(code,!on);
    } else {
        V3ConfigIgnores::singleton().addIgnore(code, filename, min, on);
        if (max) V3ConfigIgnores::singleton().addIgnore(code, filename, max, !on);
    }
}

void V3Config::addInline(FileLine* fl, const string& module, const string& ftask, bool on) {
    if (ftask.empty()) {
        V3ConfigModule::singleton().addInline(module, on);
    } else {
        if (!on) {
            fl->v3error("no_inline not supported for tasks"<<endl);
        } else {
            V3ConfigFTask::singleton().addFTaskInline(module, ftask);
        }
    }
}

void V3Config::addVarAttr(FileLine* fl, const string& module, const string& ftask,
                          const string& signal, AstAttrType attr, AstSenTree* sensep) {
    // Semantics: sensep iff public_flat_rw
    if ((attr == AstAttrType::VAR_PUBLIC_FLAT_RW) && !sensep) {
        fl->v3error("public_flat_rw needs sensitivity"<<endl);
        return;
    } else if ((attr != AstAttrType::VAR_PUBLIC_FLAT_RW) && sensep) {
        sensep->v3error("sensitivity not expected for attribute"<<endl);
        return;
    }
    // Semantics: Most of the attributes operate on signals
    if (signal.empty()) {
        if (attr == AstAttrType::VAR_ISOLATE_ASSIGNMENTS) {
            if (ftask.empty()) {
                fl->v3error("isolate_assignments only applies to signals or functions/tasks"<<endl);
            } else {
                V3ConfigVarAttr::singleton().addFunctionIsolate(module, ftask);
            }
        } else if (attr == AstAttrType::VAR_PUBLIC) {
            if (ftask.empty()) {
                // public module, this is the only exception from var here
                V3ConfigModule::singleton().addPublic(module);
            } else {
                V3ConfigFTask::singleton().addFTaskPublic(module, ftask);
            }
        } else {
            fl->v3error("missing -signal"<<endl);
        }
    } else {
        V3ConfigVarAttr::singleton().addVarAttr(fl, module, ftask, signal, attr, sensep);
    }
}

void V3Config::applyCase(AstCase* nodep) {
    V3ConfigCase::singleton().applyCase(nodep);
}

void V3Config::applyCoverageBlock(AstNodeModule* modulep, AstBegin* nodep) {
    V3ConfigCoverageBlock::singleton().applyCoverageBlock(modulep, nodep);
}

void V3Config::applyIgnores(FileLine* filelinep) {
    V3ConfigIgnores::singleton().applyIgnores(filelinep);
}

void V3Config::applyModule(AstNodeModule* modp) {
    V3ConfigModule::singleton().applyModule(modp);
}

void V3Config::applyFTask(AstNodeModule* modulep, AstNodeFTask* ftaskp) {
    V3ConfigFTask::singleton().applyFTask(modulep, ftaskp);
    if (VN_IS(ftaskp, Func)) {
        V3ConfigVarAttr::singleton().applyFunctionIsolate(modulep, ftaskp);
    }
}

void V3Config::applyVarAttr(AstNodeModule* modulep, AstNodeFTask* ftaskp,
                            AstVar* varp) {
    V3ConfigVarAttr::singleton().applyVarAttr(modulep, ftaskp, varp);
}
