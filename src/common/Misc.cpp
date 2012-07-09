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

#include <array>
#include <algorithm>
#include <stdexcept>

#include <cstring>
#include <cstdlib>
#include <csignal>

#include "Misc.h"

#ifdef _WIN32
namespace win32
{
    #include <windows.h>
    #include <winternl.h>
    #include <psapi.h>

    #ifdef INVALID_HANDLE_VALUE
        #undef INVALID_HANDLE_VALUE // The original INVALID_HANDLE_VALUE has a cast, so we fix the cast to use the namespace
        #define INVALID_HANDLE_VALUE ((win32::HMODULE)-1)
    #endif

    // winternl.h varies across different compilers, so we make sure some things are defined
    #ifndef NT_SUCCESS
        #define NT_SUCCESS(status) (status >= 0)
    #endif
    #ifndef FILE_OPEN
        #define FILE_OPEN (1)
    #endif
}
#else
namespace posix
{
    #include <dlfcn.h>
    #include <link.h>
    #include <elf.h>
    #include <libgen.h>
    #include <sys/types.h>
    #include <sys/stat.h>
}
#endif

std::vector<std::string> split(const std::string& string, const std::string& delims)
{
    std::vector<std::string> result;
    std::string::size_type match = -1;
    do
    {
        std::string::size_type prevMatch = ++match;
        match = string.find_first_of(delims, match);
        result.push_back(string.substr(prevMatch, match - prevMatch));
    } while (match != std::string::npos);
    return result;
}

#ifdef _WIN32
std::string strErrorWin32(int error)
{
    char* message;
    win32::FormatMessage(
        FORMAT_MESSAGE_ALLOCATE_BUFFER |
        FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL,
        error,
        0,
        (char*)&message,
        0, NULL);
    std::string result(message);
    win32::LocalFree(message);
    return result;
}
#endif

std::string strError(int error)
{
    return std::strerror(error);
}

bool getInodeAndDeviceId(const std::string& pathfile, uint64_t& inode, uint64_t& deviceId) noexcept
{
#ifdef _WIN32
    win32::HANDLE file;
    if (pathfile.find("\\Device\\") == 0) // Is the path a native pathfile?
    {
        // Yes, so we use NtCreateFile to open the file

        // Ready a UNICODE_STRING to pass to NtCreateFile
        wchar_t pathfileWide[pathfile.size() + 1];
        std::mbstowcs(pathfileWide, pathfile.c_str(), pathfile.size() + 1);
        win32::UNICODE_STRING pathfileUnicode;
        pathfileUnicode.Length = pathfile.size() * sizeof(std::wstring::value_type);
        pathfileUnicode.MaximumLength = (pathfile.size() + 1) * sizeof(std::wstring::value_type);
        pathfileUnicode.Buffer = pathfileWide;

        // Ready a OBJECT_ATTRIBUTES
        win32::OBJECT_ATTRIBUTES objectAttributes = {0};
        objectAttributes.Length = sizeof(win32::OBJECT_ATTRIBUTES);
        objectAttributes.ObjectName = &pathfileUnicode;

        // Actual NtCreateFile call
        win32::IO_STATUS_BLOCK ioStatus;
        win32::NTSTATUS status = win32::NtCreateFile(&file, FILE_READ_ATTRIBUTES | FILE_READ_EA, &objectAttributes, &ioStatus, 0, FILE_ATTRIBUTE_NORMAL, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, FILE_OPEN, 0, nullptr, 0);
        if (!NT_SUCCESS(status))
        {
            win32::SetLastError(win32::RtlNtStatusToDosError(status)); // Set the last error just in case anyone wants to check
            return false;
        }
    }
    else
    {
        // Otherwise, use CreateFile
        win32::HANDLE file = win32::CreateFile(pathfile.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
            return false;
    }
    win32::BY_HANDLE_FILE_INFORMATION fileinfo;
    if (!win32::GetFileInformationByHandle(file, &fileinfo))
        return false;
    inode = ((uint64_t)fileinfo.nFileIndexHigh << 32) + fileinfo.nFileIndexLow;
    deviceId = fileinfo.dwVolumeSerialNumber;
    win32::CloseHandle(file);
#else
    struct posix::stat fileinfo;
    if (posix::stat(pathfile.c_str(), &fileinfo) < 0)
        return false;
    inode = fileinfo.st_ino;
    deviceId = fileinfo.st_dev;
#endif
    return true;
}

uint32_t calculateCrc32Checksum(const std::vector<uint8_t>& data) noexcept
{
    static bool isCrc32TableInitialised = false;
    static std::array<uint32_t, 256> crc32Table;
    if (!isCrc32TableInitialised)
    {
        for (size_t c = 0; c < crc32Table.size(); ++c)
        {
            uint32_t crc32TableEntry = c;
            for (size_t b = 0; b < 8; ++b)
                if (crc32TableEntry & 1)
                    crc32TableEntry = (crc32TableEntry >> 1) ^ 0xefb88320;
                else
                    crc32TableEntry = crc32TableEntry >> 1;
            crc32Table[c] = crc32TableEntry;
        }
        isCrc32TableInitialised = true;
    }

    uint32_t result = ~0;
    for (const auto& d : data)
        result = crc32Table[(result ^ d) & 0xff] ^ (result >> 8);
    return ~result;
}
