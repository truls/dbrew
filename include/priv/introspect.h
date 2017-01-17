/**
 * This file is part of DBrew, the dynamic binary rewriting library.
 *
 * (c) 2015-2016, Josef Weidendorfer <josef.weidendorfer@gmx.de>
 *
 * DBrew is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License (LGPL)
 * as published by the Free Software Foundation, either version 2.1 of
 * the License, or (at your option) any later version.
 *
 * DBrew is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with DBrew.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _DBREW_INTROSPECT_H
#define _DBREW_INTROSPECT_H

#include "config.h"

// FIXME: Configure properly

#include "dbrew.h"

#include <stddef.h>

// Always defined
typedef struct _ElfAddrInfo {
    #define ELF_MAX_NAMELEN 255
    int lineno;
    char filePath[ELF_MAX_NAMELEN];
    char* fileName;
    const char symbName[ELF_MAX_NAMELEN];
} ElfAddrInfo;

// Never build with dwarf support for testing target
#ifdef TEST_BUILD
#undef HAVE_LIBDW
#endif

// Conditional exports
#ifdef HAVE_LIBDW

#include <elf.h>
#include <elfutils/libdwfl.h>

typedef struct _LineMap {
    char* srcPtr;
    int lineno;
} LineMap;


typedef struct _SourceFile {
    uint64_t start; // File start addr
    uint64_t len; // File end addr
    char* basePtr; // Starting pointer of mmaped file
    char* curPtr; // Current location
    uint64_t fileLen; // Length of file
    char* filePath; // File path
    struct _SourceFile* next; // Chain of SourceFies
    int curLine; // 
    int maxLines;
    char** linePtr;
} SourceFile;

typedef struct _ElfContext {
    Dwfl* dwfl;
    // FIXME: Do we need this here?
    Dwfl_Callbacks* callbacks;
    Dwfl_Module* this;
    SourceFile* sf;
} ElfContext;

// Return a file descriptor to actual current path
int getSelfFd(char** filename);
int initElfData(Rewriter* r, char* filename, int fd);
void freeElfData(Rewriter* r);
int addrToLine(Rewriter* r, uint64_t addr, ElfAddrInfo* retInfo);
int addrToFun(Rewriter* r, uint64_t addr, char* buf, size_t len);
char* addrToSym(Rewriter* r, uint64_t addr);

// Line handling functions
SourceFile* initSourceFile(Rewriter* r, const char* filename);
char* getSourceLine(Rewriter* r, uint64_t addr, int lineno);
void freeSourceFile(SourceFile* s);

#else

typedef struct {} ElfContext;
typedef struct {} LineMap;

#define getSelfFd(a) 0
static inline int initElfData(Rewriter* r __attribute__((unused)),
                         char* filename __attribute__((unused)),
                         int fd __attribute__((unused))) {
    return 1;
}

static inline void freeElfData(Rewriter* r __attribute__((unused)))
{}


static inline int addrToLine(Rewriter* r __attribute__((unused)),
                      uint64_t addr __attribute__((unused)),
                      ElfAddrInfo* retInfo __attribute__((unused))) {
    return 1;
}

static inline int addrToFun(Rewriter* r __attribute__((unused)),
                     uint64_t addr __attribute__((unused)),
                     char* buf __attribute__((unused)),
                     size_t len __attribute__((unused))) {
    return 1;
}

static inline char* addrToSym(Rewriter* r __attribute__((unused)),
                              uint64_t addr __attribute__((unused))) {
    return 0;
}
#endif // HAS_LIBDW

#endif // _DBREW_INTROSPECT_H
