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
#include "config.h"

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
#include <elfutils/libdw.h>
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

int initElfData(Rewriter* r, int pid)
{
    int ret = 0;
    ElfContext* d;

    assert(pid > 0);
    assert(!r->elf);

    r->elf = calloc(sizeof(ElfContext), 1);
    d = r->elf;

    d->callbacks = malloc(sizeof(Dwfl_Callbacks));
    d->callbacks->find_debuginfo = dwfl_standard_find_debuginfo;
    d->callbacks->debuginfo_path = &d->debuginfo_path;
    d->callbacks->find_elf = dwfl_linux_proc_find_elf;

    d->dwfl = dwfl_begin(d->callbacks);

    dwfl_report_begin(d->dwfl);
    ret = dwfl_linux_proc_report(d->dwfl, pid);
    assert (ret == 0);
    ret = dwfl_report_end(d->dwfl, NULL, NULL);

    return ret;
}

bool addrToSym(Rewriter* r, uint64_t addr, AddrSymInfo* retInfo)
{
    Dwfl_Module* mod;
    Elf64_Sym syminfo;
    GElf_Off offset;

    if (!addr) {
        return false;
    }

    mod = dwfl_addrmodule(r->elf->dwfl, addr);
                                            NULL, NULL, NULL);
    if (!name) {
        return false;
    }

    strncpy(retInfo->name, name, ELF_MAX_NAMELEN);
    retInfo->offset = offset;
    retInfo->size = syminfo.st_size;

    return true;
}

static
int symFromModuleCB(Dwfl_Module* mod,
                    void** userdata __attribute__((unused)),
                    const char* str __attribute__((unused)),
                    Dwarf_Addr addr __attribute__((unused)),
                    void* arg)
{
    char* buf = (char*) arg;
    GElf_Addr* saddr = (GElf_Addr* ) buf;
    const char* needle = buf + sizeof(uint64_t);


    int symbs = dwfl_module_getsymtab(mod);
    for (int i = 0; i < symbs; i++){
        GElf_Sym sym;
        const char* name = dwfl_module_getsym_info(mod, i, &sym, saddr,
                                             NULL, NULL, NULL);
        //printf("Checking name %s == %s\n", name, needle);
        if (name && (strncmp(name, needle, ELF_MAX_NAMELEN) == 0)) {
            return DWARF_CB_ABORT;
        }
    }
    return DWARF_CB_OK;
}

uint64_t symToAddr(Rewriter* r, const char* symName)
{
    char buf[sizeof(uint64_t) + ELF_MAX_NAMELEN];
    uint64_t* addr = (uint64_t*) buf;
    char* name = buf + 8;
    strncpy(name, symName, ELF_MAX_NAMELEN);
    dwfl_getmodules(r->elf->dwfl, &symFromModuleCB, buf, 0);

    return *addr;
}

static
SourceFile* allocSourceFile(void)
{
    SourceFile* new = calloc(sizeof(SourceFile), 1);
    return new;
}

static
SourceFile* initSourceFile(const char* fileName,
                           const char* compdir)
{
    int fd;
    if (fileName[0] == '/') {
        // Path is absolute
        if ((fd = open(fileName, O_RDONLY)) < 0) {
            return 0;
        }
    } else {
        char buf[ELF_MAX_NAMELEN];
        snprintf(buf, ELF_MAX_NAMELEN, "%s/%s", compdir, fileName);
        if ((fd = open(buf, O_RDONLY)) < 0) {
            return 0;
        }
    }

    struct stat buf;
    fstat(fd, &buf);
    char* srcFile = mmap(0, buf.st_size, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
    // mmap(2) states: "On the other hand, closing the file descriptor does not
    // unmap the region."
    close(fd);

    SourceFile* new = allocSourceFile();
    new->maxLines = 1000;
    new->linePtr = malloc(new->maxLines * sizeof(char*));
    strncpy(new->filePath, fileName, ELF_MAX_NAMELEN);
    new->basePtr = srcFile;
    new->curPtr = srcFile;
    new->fileLen = buf.st_size;
    new->next = NULL;

    return new;
}

static
SourceFile* findSourceFile(SourceFile* s, const char* fn)
{
    while(s) {
        // FIXME: Don't match based on filename
        if (strcmp(s->filePath, fn) == 0) {
            return s;
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
    while(curLine < lineno && !(buf >= s->basePtr + s->fileLen)) {
        while(*buf != '\n' && !(buf >= s->basePtr + s->fileLen)) {
            buf++;
        }
        *buf = '\0';
        curLine++;
        if (curLine == s->maxLines) {
            s->maxLines *= 2;
            s->linePtr = realloc(s->linePtr, s->maxLines * sizeof(char*));
        }
        s->linePtr[curLine] = start;
        buf++;
        start = buf;
    }
    s->curPtr = start;
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

char* getSourceLine(Rewriter* r, const char* fp, int lineno)
{
    SourceFile* s = findSourceFile(r->elf->sf, fp);

    if (s) {
        return getLine(s, lineno);
    } else {
        return 0;
    }
}

// If successful, returns true and poplates ret_info with the requested information
// otherwise, return false and leave retInfo untouched
bool addrToLine(Rewriter* r, uint64_t addr, ElfAddrInfo* retInfo)
{
    assert(retInfo);

    bool ret;
    Dwfl_Line* line;
    Dwfl_Module* mod;

    mod = dwfl_addrmodule(r->elf->dwfl, addr);
    line = dwfl_module_getsrc(mod, addr);

    const char* name;
    const char* compdir;
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

        // Initialize source file lines
        SourceFile* sf;
        sf = findSourceFile(r->elf->sf, name);
        if (!sf) {
            compdir = dwfl_line_comp_dir(line);
            sf = initSourceFile(name, compdir);
            assert(sf);
            SourceFile* psf = r->elf->sf;
            //assert(psf);
            r->elf->sf = sf;
            sf->next = psf;
        }
        ret = true;
    } else {
        retInfo->lineno = linep;
        strcpy(retInfo->filePath, "<unknown>");
        retInfo->fileName = retInfo->filePath;

        ret = false;
    }

    return ret;
}

void freeSourceFile(SourceFile* s) {
    free(s->linePtr);
    munmap(s->basePtr, s->fileLen);
    if (s->next) {
        freeSourceFile(s->next);
    }
    free(s);
}

void freeElfData(Rewriter* r)
{
    freeSourceFile(r->elf->sf);
    dwfl_end(r->elf->dwfl);
    free(r->elf->callbacks);
    free(r->elf);
}

#endif // HAVE_LIBDW
