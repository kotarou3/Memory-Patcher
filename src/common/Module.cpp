/*
    This file is part of Memory Patcher.

    Memory Patcher is free software: you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    Memory Patcher is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
    GNU Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public License
    along with Memory Patcher. If not, see <http://www.gnu.org/licenses/>.
*/

#include <memory>
#include <stdexcept>

#include <cstring>
#include <cstdlib>
#include <cassert>

#include <stdint.h>

#ifdef _WIN32
namespace win32
{
    #include <windows.h>
    #include <dbghelp.h>
    #include <psapi.h>

    using ::_splitpath; // Windows stdlib.h function
}
#else
namespace posix
{
    #include <unistd.h>
    #include <limits.h>
    #include <dlfcn.h>
    #include <elf.h>
    #include <libgen.h>

    // Define our own link_map so we can access the some of the hidden members
    class LinkMap
    {
        public:
            Elf32_Addr relocationOffset; // http://stackoverflow.com/a/8876887/359653
            char* name;
            Elf32_Dyn* dynamicSection;
            LinkMap* next;
            LinkMap* prev;

            // The hidden members start here.
            // Read http://sourceware.org/git/?p=glibc.git;a=blob;f=include/link.h for documentation.
            // Member names might be different, but they will be in the same position
            LinkMap* realLinkMap;
            Lmid_t currentNamespace;
            void* libname; // Actually a libname_list*
            #ifndef DT_THISPROCNUM
                #define DT_THISPROCNUM 0
            #endif
            Elf32_Dyn* info[DT_NUM + DT_THISPROCNUM + DT_VERSIONTAGNUM + DT_EXTRANUM + DT_VALNUM + DT_ADDRNUM];
            Elf32_Phdr* programHeaders;
            Elf32_Addr entryPoint;
            Elf32_Half programHeaderCount;
            // More, but we don't need those
    };

    using ::realpath; // POSIX stdlib.h function
}
#endif

#include "Module.h"

Module::Module():
    handle(nullptr),
    isLoaded(false)
{
}

Module::Module(Module&& rvalue):
    handle(rvalue.handle),
    base(rvalue.base),
    file(std::move(rvalue.file)),
    path(std::move(rvalue.path)),
    segments(std::move(rvalue.segments)),
    originalSegments(std::move(rvalue.originalSegments)),
    isLoaded(rvalue.isLoaded)
{
    rvalue.handle = nullptr;
    rvalue.isLoaded = false;
}

Module& Module::operator=(Module&& rvalue)
{
    if (this == &rvalue)
        return *this;

    handle = rvalue.handle;
    base = rvalue.base;
    file = std::move(rvalue.file);
    path = std::move(rvalue.path);
    segments = std::move(rvalue.segments);
    originalSegments = std::move(rvalue.originalSegments);
    isLoaded = rvalue.isLoaded;

    rvalue.handle = nullptr;
    rvalue.isLoaded = false;

    return *this;
}

Module::~Module()
{
    unloadNoThrow();
}

void Module::load(const std::string& pathfile)
{
    unloadNoThrow();
#ifdef _WIN32
    if ((handle = win32::LoadLibrary(pathfile.c_str())) == nullptr)
        throw std::runtime_error(strErrorWin32(win32::GetLastError()));
#else
    if ((handle = posix::dlopen(pathfile.c_str(), RTLD_NOW | RTLD_LOCAL)) == nullptr)
        throw std::runtime_error(std::string(posix::dlerror()));
#endif
    isLoaded = true;
    updateInfo();
}

void Module::open(const std::string& pathfile)
{
    unloadNoThrow();
    TRACE(pathfile);
#ifdef _WIN32
    if ((handle = win32::GetModuleHandle(pathfile.empty() ? nullptr : pathfile.c_str())) == nullptr)
        throw std::runtime_error(strErrorWin32(win32::GetLastError()));
#else
    if ((handle = posix::dlopen(pathfile.empty() ? nullptr : pathfile.c_str(), RTLD_NOW | RTLD_NOLOAD)) == nullptr)
    {
        // dlopen() will not be able to find the main executable, if that was the argument in `pathfile'
        // so we need to check if `pathfile' is the main executable name, and if it does,
        // call dlopen(nullptr, ...) instead.
        char mainExecutablePathfile[1024];
        mainExecutablePathfile[posix::readlink("/proc/self/exe", mainExecutablePathfile, 1024)] = 0;
        if (!isPathfileMatch_(pathfile, mainExecutablePathfile))
            throw std::runtime_error("`" + pathfile + "' is not loaded.");
        handle = posix::dlopen(nullptr, RTLD_NOW | RTLD_NOLOAD);
    }
#endif
    updateInfo();
}

void Module::openByAddress(const uint8_t* address)
{
    assert(false);
}

void Module::unload(bool force)
{
    if (handle == nullptr)
        throw std::logic_error("No module loaded or opened.");
    if (!isLoaded && !force)
        throw std::logic_error("Not unloading an opened (not loaded) module.");

#ifdef _WIN32
    if (!win32::FreeLibrary((win32::HMODULE)handle))
        throw std::runtime_error(strErrorWin32(win32::GetLastError()));
#else
    if (posix::dlclose(handle))
        throw std::runtime_error(std::string(posix::dlerror()));
#endif
    handle = nullptr;
    isLoaded = false;
}

bool Module::unloadNoThrow(bool force) noexcept
{
    if (handle == nullptr)
        return false;
    if (!isLoaded && !force)
        return false;

#ifdef _WIN32
    if (!win32::FreeLibrary((win32::HMODULE)handle))
        return false;
#else
    if (posix::dlclose(handle))
        return false;
#endif
    handle = nullptr;
    isLoaded = false;
    return true;
}

void Module::detach() noexcept
{
    handle = nullptr;
    isLoaded = false;
}

bool Module::getIsModuleOpen()
{
    return handle != nullptr;
}

void Module::updateInfo()
{
    if (handle == nullptr)
        throw std::logic_error("No module loaded or opened.");
#ifdef _WIN32
    // Get the base
    win32::MODULEINFO info;
    win32::GetModuleInformation(win32::GetCurrentProcess(), (win32::HMODULE)handle, &info, sizeof(win32::MODULEINFO));
    base = (uint8_t*)info.lpBaseOfDll;

    // Get the filename and absolute path
    char pathfile[MAX_PATH];
    win32::GetModuleFileName((win32::HMODULE)handle, pathfile, MAX_PATH);
    char drive[_MAX_DRIVE];
    char path[_MAX_DIR];
    char file[_MAX_FNAME];
    char ext[_MAX_EXT];
    win32::_splitpath(pathfile, drive, path, file, ext);
    this->file = file;
    this->file += ext;
    this->path = drive;
    this->path += path;

    // Get the original segments
    originalSegments.clear();
    win32::IMAGE_NT_HEADERS* peHeader = win32::ImageNtHeader(base);

    // PE header segment is always loaded at the base address with read-only protection
    Memory::PageInfo peHeaderSegment;
    peHeaderSegment.start = base;
    peHeaderSegment.size = peHeader->OptionalHeader.SizeOfHeaders;
    Memory::alignPage((size_t&)peHeaderSegment.start, (size_t&)peHeaderSegment.size);
    peHeaderSegment.isReadable = true;
    peHeaderSegment.isWritable = false;
    peHeaderSegment.isExecutable = false;
    originalSegments.push_back(peHeaderSegment);

    // Get the other original segments
    win32::IMAGE_SECTION_HEADER* sectionHeaders = (win32::IMAGE_SECTION_HEADER*)(peHeader + 1);
    size_t sectionHeadersCount = peHeader->FileHeader.NumberOfSections;
    for (size_t s = 0; s < sectionHeadersCount; ++s, ++sectionHeaders)
    {
        Memory::PageInfo segment;

        // Read and align the segment's addresses
        segment.start = (uint8_t*)sectionHeaders->VirtualAddress + (size_t)base;
        uint8_t* segmentEnd = segment.start + sectionHeaders->Misc.VirtualSize;
        Memory::alignPage((size_t&)segment.start, (size_t&)segmentEnd);
        segment.size = segmentEnd - segment.start;

        // Read the segment protection values
        segment.isReadable = sectionHeaders->Characteristics & IMAGE_SCN_MEM_READ;
        segment.isWritable = sectionHeaders->Characteristics & IMAGE_SCN_MEM_WRITE;
        segment.isExecutable = sectionHeaders->Characteristics & IMAGE_SCN_MEM_EXECUTE;

    #if 0 // Not used so as to give more precise results
        // Check if the previous segment has exactly the same protection and if it ends on our
        // start address. If so, we can merge with it.
        if (originalSegments.back().start + originalSegments.back().size == segment.start &&
            originalSegments.back().isReadable == segment.isReadable &&
            originalSegments.back().isWritable == segment.isWritable &&
            originalSegments.back().isExecutable == segment.isExecutable)
            originalSegments.back().size += segment.size;
        else
    #endif
            originalSegments.push_back(segment);
    }
#else
    // Get the link map
    posix::LinkMap* map;
    posix::dlinfo(handle, posix::RTLD_DI_LINKMAP, &map);

    // Get the filename and absolute path
    char file[1024];
    char path[1024];
    if (map->name[0] == 0)
    {
        // If the name in the link map is empty, then it must be the main executable
        file[posix::readlink("/proc/self/exe", file, 1024)] = 0;
        std::strcpy(path, file);
    }
    else
    {
        std::strcpy(file, map->name);
        std::strcpy(path, map->name);
    }
    this->file = posix::basename(file);
    char realPath[PATH_MAX];
    posix::realpath(posix::dirname(path), realPath);
    this->path = realPath;
    this->path += "/";

    // Get the original segments and the base
    originalSegments.clear();
    posix::Elf32_Phdr* programHeaders = map->programHeaders;
    bool isBaseSet = false;
    for (size_t s = 0; s < map->programHeaderCount; ++s, ++programHeaders)
    {
        if (programHeaders->p_type != PT_LOAD)
            continue;

        Memory::PageInfo segment;

        // Read and align the segments addresses
        segment.start = (uint8_t*)programHeaders->p_vaddr + (size_t)map->relocationOffset;
        uint8_t* segmentEnd = segment.start + programHeaders->p_memsz;
        Memory::alignPage((size_t&)segment.start, (size_t&)segmentEnd);
        segment.size = segmentEnd - segment.start;

        // Read the segment protection values
        segment.isReadable = programHeaders->p_flags & PF_R;
        segment.isWritable = programHeaders->p_flags & PF_W;
        segment.isExecutable = programHeaders->p_flags & PF_X;

        originalSegments.push_back(segment);

        if (!isBaseSet)
        {
            base = segment.start;
            isBaseSet = true;
        }
    }
#endif

    // Get the current segments
    segments.clear();
    std::vector<Memory::PageInfo> currentSegments = Memory::enumerateSegments();
    for (const auto& segment : currentSegments)
        if (isPathfileMatch_(segment.pathfile, this->path + this->file))
            segments.push_back(segment);
}

uint8_t* Module::getSymbol(const std::string& symbol) const
{
    uint8_t* result;
#ifdef _WIN32
    if ((result = (uint8_t*)win32::GetProcAddress((win32::HMODULE)handle, symbol.c_str())) == nullptr)
        throw std::runtime_error(strErrorWin32(win32::GetLastError()));
#else
    if ((result = (uint8_t*)posix::dlsym(handle, symbol.c_str())) == nullptr)
        throw std::runtime_error(std::string(posix::dlerror()));
#endif
    return result;
}

void* Module::getHandle() const
{
    return handle;
}

std::string Module::getFile() const
{
    return file;
}

std::string Module::getPath() const
{
    return path;
}

const std::vector<Memory::PageInfo>& Module::getSegments() const
{
    return segments;
}

const std::vector<Memory::PageInfo>& Module::getOriginalSegments() const
{
    return originalSegments;
}

// Private members

bool Module::isPathfileMatch_(const std::string& a, const std::string& b)
{
#ifdef _WIN32
    // Does either `a' or `b' not contain a path?
    if ((a.find("/") == std::string::npos && a.find("\\") == std::string::npos) ||
        (b.find("/") == std::string::npos && b.find("\\") == std::string::npos))
    {
        // If so, then do the base names of both match?
        char aFile[_MAX_FNAME];
        char aExt[_MAX_EXT];
        char bFile[_MAX_FNAME];
        char bExt[_MAX_EXT];
        win32::_splitpath(a.c_str(), nullptr, nullptr, aFile, aExt);
        win32::_splitpath(b.c_str(), nullptr, nullptr, bFile, bExt);
        std::string aBasename = aFile;
        aBasename += aExt;
        std::string bBasename = bFile;
        bBasename += bExt;
        if (aBasename != bBasename)
            return false; // Nope
    }
#else
    // Does either `a' or `b' not contain a path?
    if (a.find("/") == std::string::npos || b.find("/") == std::string::npos)
    {
        std::string aCopy = a;
        std::string bCopy = b;
        // If so, then do the base names of both match?
        if (std::strcmp(posix::basename(&aCopy[0]), posix::basename(&bCopy[0])))
            return false; // Nope
    }
#endif
    else
    {
        // Yes, so check if their inode and device id matches
        uint64_t aInode;
        uint64_t aDeviceId;
        uint64_t bInode;
        uint64_t bDeviceId;
        if (!getInodeAndDeviceId(a, aInode, aDeviceId))
            return false;
        if (!getInodeAndDeviceId(b, bInode, bDeviceId))
            return false;

        if (aInode != bInode || aDeviceId != bDeviceId)
            return false;
    }
    return true;
}
