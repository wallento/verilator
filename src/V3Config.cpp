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

/**
 * Template for a class that serves as a map for entities that can be specified
 * as wildcards and are accessed by a resolved name. It rebuilds a name lookup
 * cache of resolved entities. Entities stored in this container need an update
 * function that takes a reference of this type to join multiple entities into one.
 */
template <typename T> class WildcardResolver {
    typedef std::map<string,T > Map;
    Map m_mapWildcard, m_mapResolved;
    typename Map::iterator m_last; // Last access, will probably hit again
public:
    WildcardResolver() { m_last = m_mapResolved.end(); }

    /** Update into maps from other */
    void update(const WildcardResolver& other) {
        typename Map::const_iterator it;
        for (it = other.m_mapResolved.begin(); it != other.m_mapResolved.end(); ++it) {
            m_mapResolved[it->first].update(it->second);
        }
        for (it = other.m_mapWildcard.begin(); it != other.m_mapWildcard.end(); ++it) {
            m_mapWildcard[it->first].update(it->second);
        }
    }

    /** Access and create a (wildcard) entity */
    T& at(const string& name) {
        // Don't store into wildcards if the name is not a wildcard string
        return (VString::isWildcard(name) ? m_mapWildcard[name] : m_mapResolved[name]);
    }
    /** Access an entity and resolve wildcards that match it */
    T* resolve(const string& name) {
        // Lookup if recently accessed matches
        if (VL_LIKELY(m_last != m_mapResolved.end()) && VL_LIKELY(m_last->first == name)) {
            return &m_last->second;
        }
        // Lookup if it was resolved before, typically not
        typename Map::iterator it = m_mapResolved.find(name);
        if (VL_UNLIKELY(it != m_mapResolved.end())) {
            return &it->second;
        }

        T* newp = NULL;
        // Cannot be resolved, create if matched

        // Update this entity with all matches in the wildcards
        for (it = m_mapWildcard.begin(); it != m_mapWildcard.end(); ++it) {
            if (VString::wildmatch(name, it->first)) {
                if (!newp) {
                    newp = &m_mapResolved[name]; // Emplace and get pointer
                }
                newp->update(it->second);
            }
        }
        return newp;
    }
};

// Only public_flat_rw has the sensitity tree
struct ConfigVarAttr {
    AstAttrType type;
    AstSenTree* sentreep;
};

/** Overload vector with the required update function and to apply all entries */
class ConfigVar : public std::vector<ConfigVarAttr> {
public:
    /** Update from other by copying all attributes */
    void update(const ConfigVar &node) {
        reserve(size() + node.size());
        insert(end(), node.begin(), node.end());
    }
    /** Apply all attributes to the variable */
    void apply(AstVar* varp) {
        for (const_iterator it = begin(); it != end(); ++it) {
            AstNode* newp = new AstAttrOf(varp->fileline(), it->type);
            varp->addAttrsp(newp);
            if (it->type == AstAttrType::VAR_PUBLIC_FLAT_RW) {
                newp->addNext(new AstAlwaysPublic(varp->fileline(), it->sentreep, NULL));
            }
        }
    }
};

typedef WildcardResolver<ConfigVar> VarResolver;

class ConfigFTask {
    VarResolver m_vars;
    bool m_isolate, m_noinline, m_public;
public:
    ConfigFTask() : m_isolate(false), m_noinline(false), m_public(false) {}
    void update(const ConfigFTask& f) {
        // Don't overwrite true with false
        if (f.m_isolate) m_isolate = true;
        if (f.m_noinline) m_noinline = true;
        if (f.m_public) m_public = true;
        m_vars.update(f.m_vars);
    }

    VarResolver& vars() { return m_vars; }

    void setIsolate(bool set) { m_isolate = set; }
    void setNoInline(bool set) { m_noinline = set; }
    void setPublic(bool set) { m_public = set; }

    void apply(AstNodeFTask* ftaskp) {
        if (m_noinline) ftaskp->addStmtsp(new AstPragma(ftaskp->fileline(), AstPragmaType::NO_INLINE_TASK));
        if (m_public) ftaskp->addStmtsp(new AstPragma(ftaskp->fileline(), AstPragmaType::PUBLIC_TASK));
        if (VN_IS(ftaskp, Func)) ftaskp->attrIsolateAssign(m_isolate);
    }
};

typedef WildcardResolver<ConfigFTask> FTaskResolver;

class ConfigModule {
    typedef std::unordered_set<string> StringSet;
    FTaskResolver m_tasks;
    VarResolver m_vars;
    StringSet m_coverageOffBlocks; // List of block names for coverage_off
    bool m_inline, m_inlineValue; // Whether to force the inline and its value
    bool m_public;
public:
    ConfigModule() : m_inline(false), m_inlineValue(false), m_public(false) {}

    void update(const ConfigModule& m) {
        m_tasks.update(m.m_tasks);
        m_vars.update(m.m_vars);
        for (StringSet::const_iterator it = m.m_coverageOffBlocks.begin();
             it != m.m_coverageOffBlocks.end(); ++it) {
            m_coverageOffBlocks.insert(*it);
        }
        if (!m_inline) {
            m_inline = m.m_inline;
            m_inlineValue = m.m_inlineValue;
        }
        if (!m_public) m_public = m.m_public;
    }

    FTaskResolver& ftasks() { return m_tasks; }
    VarResolver& vars() { return m_vars; }

    void addCoverageBlockOff(const string &name) {
        m_coverageOffBlocks.insert(name);
    }
    void setInline(bool set) { m_inline = true; m_inlineValue = set; }
    void setPublic(bool set) { m_public = set; }

    void apply(AstNodeModule* modp) {
        if (m_inline) {
            AstPragmaType type = m_inlineValue ? AstPragmaType::INLINE_MODULE
                                    : AstPragmaType::NO_INLINE_MODULE;
            AstNode* nodep = new AstPragma(modp->fileline(), type);
            modp->addStmtp(nodep);
        }
        if (m_public) {
            AstNode* nodep = new AstPragma(modp->fileline(), AstPragmaType::PUBLIC_MODULE);
            modp->addStmtp(nodep);
        }
    }

    void applyBlock(AstBegin* nodep) {
        AstPragmaType pragma = AstPragmaType::COVERAGE_BLOCK_OFF;
        if (!nodep->unnamed()) {
            for (StringSet::const_iterator it = m_coverageOffBlocks.begin();
                 it != m_coverageOffBlocks.end(); ++it) {
                if (VString::wildmatch(nodep->name(), *it)) {
                    nodep->addStmtsp(new AstPragma(nodep->fileline(), pragma));
                }
            }
        }
    }

};

typedef WildcardResolver<ConfigModule> ModuleResolver;

// Some attributes are attached to entities of the occur on a fileline
typedef enum {
    COVERAGE_BLOCK_OFF,
    FULL_CASE,
    PARALLEL_CASE,
    ENUM_SIZE
} V3LineOccurenceType;

typedef std::bitset<V3LineOccurenceType::ENUM_SIZE> V3LineOccurence;

class ConfigFile {
    typedef std::map<int, V3LineOccurence> LineMap;

    LineMap m_lines;

    bool lineMatch(int lineno, V3LineOccurenceType type) {
        if (m_lines.find(0) != m_lines.end() && m_lines[0][type]) return true;
        if (m_lines.find(lineno) == m_lines.end()) return false;
        return m_lines[lineno][type];
    }
public:
    void update(const ConfigFile &file) {
        for (LineMap::const_iterator it = file.m_lines.begin();
             it != file.m_lines.end(); ++it) {
            m_lines[it->first] |= it->second;
        }
    }
    void addLineOccurence(int lineno, V3LineOccurenceType attr) {
        m_lines[lineno].set(attr);
    }

    void applyBlock(AstBegin* nodep) {
        AstPragmaType pragma = AstPragmaType::COVERAGE_BLOCK_OFF;
        if (lineMatch(nodep->fileline()->lineno(), COVERAGE_BLOCK_OFF)) {
            nodep->addStmtsp(new AstPragma(nodep->fileline(), pragma));
        }
    }
    void applyCase(AstCase* nodep) {
        int lineno = nodep->fileline()->lineno();
        if (lineMatch(lineno, FULL_CASE)) nodep->fullPragma(true);
        if (lineMatch(lineno, PARALLEL_CASE)) nodep->parallelPragma(true);
    }
};

typedef WildcardResolver<ConfigFile> FileResolver;

class V3ConfigAttributes {
    ModuleResolver m_modules;
    FileResolver m_files;

    static V3ConfigAttributes s_singleton;  // Singleton (not via local static, as that's slow)
    V3ConfigAttributes() {}
    ~V3ConfigAttributes() {}
public:
    inline static V3ConfigAttributes& singleton() { return s_singleton; }

    ModuleResolver& modules() { return m_modules; }
    FileResolver& files() { return m_files; }
};

V3ConfigAttributes V3ConfigAttributes::s_singleton;

//######################################################################
// V3Config

void V3Config::addCaseFull(const string& file, int lineno) {
    V3ConfigAttributes::singleton().files().at(file).addLineOccurence(lineno, FULL_CASE);
}

void V3Config::addCaseParallel(const string& file, int lineno) {
    V3ConfigAttributes::singleton().files().at(file).addLineOccurence(lineno, PARALLEL_CASE);
}

void V3Config::addCoverageBlockOff(const string& file, int lineno) {
    V3ConfigAttributes::singleton().files().at(file).addLineOccurence(lineno, COVERAGE_BLOCK_OFF);
}

void V3Config::addCoverageBlockOff(const string& module, const string& blockname) {
    V3ConfigAttributes::singleton().modules().at(module).addCoverageBlockOff(blockname);
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
        V3ConfigAttributes::singleton().modules().at(module).setInline(on);
    } else {
        if (!on) {
            fl->v3error("no_inline not supported for tasks"<<endl);
        } else {
            V3ConfigAttributes::singleton().modules().at(module).ftasks().at(ftask).setNoInline(on);
        }
    }
}

void V3Config::addVarAttr(FileLine* fl, const string& module, const string& ftask,
                          const string& var, AstAttrType attr, AstSenTree* sensep) {
    // Semantics: sensep iff public_flat_rw
    if ((attr == AstAttrType::VAR_PUBLIC_FLAT_RW) && !sensep) {
        fl->v3error("public_flat_rw needs sensitivity"<<endl);
        return;
    } else if ((attr != AstAttrType::VAR_PUBLIC_FLAT_RW) && sensep) {
        sensep->v3error("sensitivity not expected for attribute"<<endl);
        return;
    }
    // Semantics: Most of the attributes operate on signals
    if (var.empty()) {
        if (attr == AstAttrType::VAR_ISOLATE_ASSIGNMENTS) {
            if (ftask.empty()) {
                fl->v3error("isolate_assignments only applies to signals or functions/tasks"<<endl);
            } else {
                V3ConfigAttributes::singleton().modules().at(module).ftasks().at(ftask).setIsolate(true);
            }
        } else if (attr == AstAttrType::VAR_PUBLIC) {
            if (ftask.empty()) {
                // public module, this is the only exception from var here
                V3ConfigAttributes::singleton().modules().at(module).setPublic(true);
            } else {
                V3ConfigAttributes::singleton().modules().at(module).ftasks().at(ftask).setPublic(true);
            }
        } else {
            fl->v3error("missing -signal"<<endl);
        }
    } else {
        ConfigModule& mod = V3ConfigAttributes::singleton().modules().at(module);
        if (ftask.empty()) {
            mod.vars().at(var).push_back({attr, sensep});
        } else {
            mod.ftasks().at(ftask).vars().at(var).push_back({attr, sensep});
        }
    }
}

void V3Config::applyCase(AstCase* nodep) {
    const string& filename = nodep->fileline()->filename();
    ConfigFile* file = V3ConfigAttributes::singleton().files().resolve(filename);
    if (file) file->applyCase(nodep);
}

void V3Config::applyCoverageBlock(AstNodeModule* modulep, AstBegin* nodep) {
    const string& filename = nodep->fileline()->filename();
    ConfigFile* file = V3ConfigAttributes::singleton().files().resolve(filename);
    if (file) file->applyBlock(nodep);
    const string& modname = modulep->name();
    ConfigModule* module = V3ConfigAttributes::singleton().modules().resolve(modname);
    if (module) module->applyBlock(nodep);
}

void V3Config::applyIgnores(FileLine* filelinep) {
    V3ConfigIgnores::singleton().applyIgnores(filelinep);
}

void V3Config::applyModule(AstNodeModule* modulep) {
    const string& modname = modulep->name();
    ConfigModule* module = V3ConfigAttributes::singleton().modules().resolve(modname);
    if (module) module->apply(modulep);
}

void V3Config::applyFTask(AstNodeModule* modulep, AstNodeFTask* ftaskp) {
    const string& modname = modulep->name();
    ConfigModule* modp = V3ConfigAttributes::singleton().modules().resolve(modname);
    if (!modp) return;
    ConfigFTask* ftp = modp->ftasks().resolve(ftaskp->name());
    if (ftp) ftp->apply(ftaskp);
}

void V3Config::applyVarAttr(AstNodeModule* modulep, AstNodeFTask* ftaskp,
                            AstVar* varp) {
    ConfigVar* vp;
    ConfigModule* modp = V3ConfigAttributes::singleton().modules().resolve(modulep->name());
    if (!modp) return;
    if (ftaskp) {
        ConfigFTask* ftp = modp->ftasks().resolve(ftaskp->name());
        if (!ftp) return;
        vp = ftp->vars().resolve(varp->name());
    } else {
        vp = modp->vars().resolve(varp->name());
    }
    if (vp) vp->apply(varp);
}
