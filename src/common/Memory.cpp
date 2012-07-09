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

#include <algorithm>
#include <memory>
#include <stdexcept>

#include <cstdio>
#include <cstring>

#include "Memory.h"

#ifdef _WIN32
namespace win32
{
    #include <windows.h>
    #include <psapi.h>
}
#else
namespace posix
{
    #include <unistd.h>
    #include <sys/mman.h>
}
#endif

namespace Memory
{

std::vector<PageInfo> enumerateSegments()
{
    std::vector<PageInfo> result;
#ifdef _WIN32
    // Get the list of module handles
    win32::HMODULE* hmoduleList;
    size_t hmoduleListSize;
    win32::EnumProcessModules(win32::GetCurrentProcess(), (win32::HMODULE*)&hmoduleList, sizeof(win32::HMODULE), (win32::DWORD*)&hmoduleListSize);
    hmoduleList = new win32::HMODULE[hmoduleListSize / sizeof(win32::HMODULE)];
    win32::EnumProcessModules(win32::GetCurrentProcess(), hmoduleList, hmoduleListSize, (win32::DWORD*)&hmoduleListSize);
    hmoduleListSize /= sizeof(win32::HMODULE);

    // Convert the handles to something useful
    class ModuleInfo_
    {
        public:
            std::string name;
            uint8_t* base;
            size_t size;
    };
    std::vector<ModuleInfo_> moduleList;
    for (size_t h = 0; h < hmoduleListSize; ++h)
    {
        ModuleInfo_ moduleInfo;

        // Get the filename
        char pathfile[MAX_PATH];
        win32::GetModuleFileName(hmoduleList[h], pathfile, MAX_PATH);
        moduleInfo.name = pathfile;

        // Get the base and size
        win32::MODULEINFO info;
        win32::GetModuleInformation(win32::GetCurrentProcess(), hmoduleList[h], &info, sizeof(win32::MODULEINFO));
        moduleInfo.base = (uint8_t*)info.lpBaseOfDll;
        moduleInfo.size = info.SizeOfImage;

        moduleList.push_back(moduleInfo);
    }
    std::sort(moduleList.begin(), moduleList.end(), [](const ModuleInfo_& a, const ModuleInfo_&b) -> bool
    {
        return a.base < b.base;
    });

    // Enumerate the segments
    uint8_t* nextAddressToScan = nullptr;
    while (true)
    {
        win32::MEMORY_BASIC_INFORMATION memInfo;
        if (!win32::VirtualQuery(nextAddressToScan, &memInfo, sizeof(win32::MEMORY_BASIC_INFORMATION)))
            break;

        if (memInfo.State == MEM_COMMIT)
        {
            PageInfo segment;
            segment.start = (uint8_t*)memInfo.BaseAddress;
            segment.size = memInfo.RegionSize;
            segment.isExecutable = (memInfo.Protect >> 4) & 0xf;
            segment.isReadable = (memInfo.Protect >> (1 + (segment.isExecutable ? 4 : 0))) & 0x7;
            segment.isWritable = (memInfo.Protect >> (2 + (segment.isExecutable ? 4 : 0))) & 0x3;

            char pathfile[MAX_PATH];
            win32::GetMappedFileName(win32::GetCurrentProcess(), segment.start, pathfile, MAX_PATH);
            segment.pathfile = pathfile;
            if (segment.pathfile.find("?Device\\") == 0)
                segment.pathfile[0] = '\\'; // GetMappedFileName seems to sometimes glitch by giving out "?Device\" instead of "\Device\"

            result.push_back(segment);
        }

        nextAddressToScan = (uint8_t*)memInfo.BaseAddress + memInfo.RegionSize;
    }
#else
    std::FILE* maps = std::fopen("/proc/self/maps", "r");
    while (true)
    {
        char line[1024];
        if (std::fgets(line, 1024, maps) == 0)
            break;
        line[std::strlen(line) - 1] = 0;

        uint8_t* start;
        uint8_t* end;
        char readable;
        char writable;
        char executable;
        char sharedOrPrivate;
        uint8_t* fileOffset;
        uint8_t fileDeviceMajor;
        uint8_t fileDeviceMinor;
        uint64_t fileInode;
        char* filename;

        // Read everything
        std::sscanf(line, "%08x-%08x %c%c%c%c %08x %02hhx:%02hhx %llu %n",
                    (unsigned int*)&start, (unsigned int*)&end, &readable, &writable, &executable, &sharedOrPrivate, (unsigned int*)&fileOffset, &fileDeviceMajor, &fileDeviceMinor, &fileInode, (int*)&filename);
        filename += (size_t)line;

        PageInfo segment;
        segment.start = start;
        segment.size = end - start;
        segment.isReadable = readable == 'r';
        segment.isWritable = writable == 'w';
        segment.isExecutable = executable == 'x';
        segment.pathfile = filename;

        result.push_back(segment);
    }
    std::fclose(maps);
#endif
    return result;
}

size_t getPageAlignment()
{
#ifdef _WIN32
    win32::SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);
    return sysInfo.dwPageSize;
#else
    return posix::sysconf(posix::_SC_PAGESIZE);
#endif
}

void alignPage(size_t& down, size_t& up)
{
    const size_t pageAlignment = getPageAlignment();
    down = down & ~(pageAlignment - 1);
    up = ((up - 1) & ~(pageAlignment - 1)) + pageAlignment;
    // Equivilent to:
    //  down -= down % pageAlignment;
    //  up += pageAlignment - (up % pageAlignment);
}

std::vector<PageInfo> queryPage(const uint8_t* start, size_t size)
{
    alignPage((size_t&)start, size);

    if (size == 0)
        throw std::logic_error("Invalid page.");

    std::vector<PageInfo> result;
    std::vector<PageInfo> segments = enumerateSegments();
    for (const auto& segment : segments)
        if (start < segment.start)
            throw std::logic_error("Invalid page.");
        else if (start < segment.start + segment.size)
        {
            result.push_back(segment);
            if ((size_t)((segment.start + segment.size) - start) >= size)
            {
                size = 0;
                break;
            }
            size -= (segment.start + segment.size) - start;
            start = segment.start + segment.size;
        }
    if (size != 0)
        throw std::logic_error("Invalid page.");

    return result;
}

std::vector<PageInfo> changePageProtection(PageInfo page)
{
    alignPage((size_t&)page.start, page.size);

    // Get the current pages specified in `page'
    std::vector<PageInfo> oldPages = queryPage(page.start, page.size);

    // Trim the current page infos so they fit snugly inside `page'
    oldPages.front().size -= page.start - oldPages.front().start;
    oldPages.front().start = page.start;
    oldPages.back().size = page.size - (oldPages.back().start - page.start);

#ifdef _WIN32
    win32::DWORD newProtect = PAGE_NOACCESS;
    if (page.isWritable)
        newProtect = PAGE_READWRITE;
    else if (page.isReadable)
        newProtect = PAGE_READONLY;
    if (page.isExecutable)
        newProtect = newProtect << 4;
    win32::DWORD oldProtect;
    if (!win32::VirtualProtect(page.start, page.size - 1, newProtect, &oldProtect))
        throw std::runtime_error(strErrorWin32(win32::GetLastError()));
#else
    if (posix::mprotect(page.start, page.size, (!page.isReadable && !page.isWritable && !page.isExecutable) ? PROT_NONE :
        ((page.isReadable ? PROT_READ : 0) | (page.isWritable ? PROT_WRITE : 0) | (page.isExecutable ? PROT_EXEC : 0))) == -1)
        throw std::runtime_error(strError(errno));
#endif
    return oldPages;
}

void safeCopy(const std::vector<uint8_t> from, uint8_t* to)
{
    std::vector<PageInfo> oldPages = changePageProtection({ to, from.size(), true, true, true, "" });
    std::memcpy(to, from.data(), from.size());
    for (const auto& oldPage : oldPages)
        changePageProtection(oldPage);
}

}
