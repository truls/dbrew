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

#include "dbrew.h"
#include "introspect.h"
#include "common.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <assert.h>
#include <libgen.h>

#include <string.h>

#ifdef HAVE_LIBDW

#include <elf.h>
#include <elfutils/libdwfl.h>

/*
 * ELF introspection support module for DBrew
 *
 * Provides optional support for ELF introspection by directly parsing
 * /proc/self/exe in order to leverage, for example, DWARF debugging information
 * and symbol tables. This is used to enhance the debugging output produced by
 * DBrew making it easier to follow rewriting progress. It is also used for
 * symbol table lookup in order to enable discovery of, e.g., the boundaries of
 * functions and statically allocated data.
 */

int getSelfFd(char** fileName)
{
#define FILE_BUF_SIZE 100
    char buf[FILE_BUF_SIZE];
    ssize_t count;

    count = readlink("/proc/self/exe", buf, FILE_BUF_SIZE);
    buf[count] = '\0';
    if (fileName) {
        // FIXME: we need to free this
        *fileName = strdup(buf);
    }
    return open(buf, O_RDONLY);
}

int initElfData(Rewriter* r, char* fileName, int fd)
{
    int ret;
    ElfContext* d;

    assert(!r->elf);
    r->elf = calloc(sizeof(ElfContext), 1);
    d = r->elf;

    d->callbacks = malloc(sizeof(Dwfl_Callbacks));
    memset(d->callbacks, 0, sizeof(Dwfl_Callbacks));

    d->dwfl = dwfl_begin(d->callbacks);

    dwfl_report_begin(d->dwfl);
    d->this = dwfl_report_elf(d->dwfl, "This_module", fileName, fd, 0, 0);
    ret = dwfl_report_end(d->dwfl, NULL, NULL);

    // cleanup
    free(fileName);
    return 0;
}

char* addrToSym(Rewriter* r, uint64_t addr)
{
    return 0;
}

// If successful, returns 0 and poplates ret_info with the requested information
// otherwise, return -1 and leave retInfo untouched
int addrToLine(Rewriter* r, uint64_t addr, ElfAddrInfo* retInfo)
{
    assert(retInfo);

    Dwarf_Die* die;
    Dwarf_Addr bias;
    Dwfl_Line* line;

    die = dwfl_module_addrdie(r->elf->this, addr, &bias);
    line = dwfl_module_getsrc(r->elf->this, addr);

    const char* name;
    Dwarf_Addr addr_out;
    int linep, colp;
    Dwarf_Word mtime, length;
    name = dwfl_lineinfo(line, &addr_out, &linep, &colp, &mtime, &length);

    //printf("Source file %s has line %d,%d at addr %#08x (original: %#08x). mtime: %lu, length: %lu",
    //      name, linep, colp, addr_out, addr, mtime, length);

    if (name) {
        retInfo->lineno = linep;
        strncpy(retInfo->filePath, name, ELF_MAX_NAMELEN);
        retInfo->filePath[ELF_MAX_NAMELEN - 1] = '\0';
        retInfo->fileName = basename((char*) name);
    } else {
        retInfo->lineno = linep;
        strcpy(retInfo->filePath, "<unknown>");
        retInfo->fileName = retInfo->filePath;
    }

    // TODO: Figure out return value
    return 0;
}

static
SourceFile* allocSourceFile(void)
{
    SourceFile* new = malloc(sizeof(SourceFile));
    memset(new, 0, sizeof(SourceFile));
    return new;
}

SourceFile* initSourceFile(Rewriter* r, const char* fileName)
{
    int fd;
    if ((fd = open(fileName, O_RDONLY)) < 0) {
        return 0;
    }

    struct stat buf;
    fstat(fd, &buf);
    char* srcFile = mmap(0, buf.st_size, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);

    SourceFile* new = allocSourceFile();
    new->maxLines = 1000;
    new->linePtr = malloc(new->maxLines * sizeof(char*));
    new->filePath = strdup(fileName);
    new->basePtr = srcFile;
    new->curPtr = srcFile;
    new->fileLen = buf.st_size;

    if (!r->elf->sf) {
        r->elf->sf = new;
    } else {
        SourceFile* next;
        for (next = r->elf->sf; next->next != 0; next = next->next);
        next->next = new;
    }

    return new;
}

static
SourceFile* findSourceFile(SourceFile* s, uint64_t a)
{
    while(s) {
        if (a >= s->start && a <= s->start + s->len) {
            return s;
            break;
        }
        s = s->next;
    }
    return 0;
}

static
void scanLines(SourceFile* s, int lineno)
{
    char* buf = s->curPtr;
    char* start = s->curPtr;
    int curLine = s->curLine;
    while(curLine < lineno) {
        while(*buf != '\n' && !(buf > s->basePtr + s->fileLen)) {
            buf++;
        }
        *buf = '\0';
        curLine++;
        s->linePtr[lineno] = start;
        buf++;
        start = buf;
    }
    s->curLine = lineno;
}

static
char* getLine(SourceFile* s, int lineno)
{
    if (s->curLine < lineno) {
        scanLines(s, lineno);
    }
    if (s->curLine < lineno) {
        return 0;
    }
    return s->linePtr[lineno];
}

char* getSourceLine(Rewriter* r, uint64_t addr, int lineno)
{
    SourceFile* s = findSourceFile(r->elf->sf, addr);
    if (s) {
        return getLine(s, lineno);
    } else {
        return 0;
    }
}

void freeSourceFile(SourceFile* s) {
    free(s->filePath);
    free(s->linePtr);
    if (s->next) {
        freeSourceFile(s->next);
    }
    free(s);
}

void freeElfData(Rewriter* r)
{
    free(r->elf->callbacks);
    free(r->elf);
}

#endif // HAVE_LIBDW
