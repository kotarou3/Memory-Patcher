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

#pragma once
#ifndef MEMORY_H
#define MEMORY_H

#include <string>
#include <vector>

#include <stdint.h>

#include "Misc.h"

namespace Memory
{
    class COMMON_EXPORT PageInfo final
    {
        public:
            uint8_t* start; // Will always be aligned to the page size
            size_t size; // Will always be a multiple of the page size
            bool isReadable;
            bool isWritable;
            bool isExecutable;
            std::string pathfile; // Can be blank when not associated with any file
    };

    std::vector<PageInfo> enumerateSegments();
    size_t getPageAlignment();
    void alignPage(size_t& down, size_t& up);
    std::vector<PageInfo> queryPage(const uint8_t* start, size_t size);
    std::vector<PageInfo> changePageProtection(PageInfo page);

    void safeCopy(const std::vector<uint8_t> from, uint8_t* to);
}

#endif
