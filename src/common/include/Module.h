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
#ifndef MODULE_H
#define MODULE_H

#include <string>
#include <vector>

#include <stdint.h>

#include "Memory.h"
#include "Misc.h"

class COMMON_EXPORT Module final
{
    public:
        Module();
        Module(const Module&) = delete;
        Module(Module&& rvalue);
        Module& operator=(const Module&) = delete;
        Module& operator=(Module&& rvalue);
        ~Module();

        void load(const std::string& pathfile);
        void open(const std::string& pathfile);
        void openByAddress(const uint8_t* address); // Not implemented!
        void unload(bool force = false);
        bool unloadNoThrow(bool force = false) noexcept;
        void detach() noexcept;

        bool getIsModuleOpen();
        void updateInfo();

        uint8_t* getSymbol(const std::string& symbol) const;
        void* getHandle() const;
        std::string getFile() const;
        std::string getPath() const;
        const std::vector<Memory::PageInfo>& getSegments() const;
        const std::vector<Memory::PageInfo>& getOriginalSegments() const;

    private:
        static bool isPathfileMatch_(const std::string& a, const std::string& b);

        void* handle;
        uint8_t* base;
        std::string file;
        std::string path;
        std::vector<Memory::PageInfo> segments;
        std::vector<Memory::PageInfo> originalSegments;

        bool isLoaded;
};

#endif
