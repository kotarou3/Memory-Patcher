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
#ifndef HOOK_H
#define HOOK_H

#include <string>
#include <vector>
#include <stdexcept>

#include <stdint.h>

#include "Search.h"
#include "Misc.h"

namespace PatchData
{

class NameHook;
class SearchHook;
class COMMON_EXPORT Hook final
{
    public:
        Hook();
        Hook(const Hook& rvalue);
        Hook& operator=(const Hook& rvalue);
        ~Hook();

        std::vector<uint8_t> serialise() const;
        void deserialise(const std::vector<uint8_t>& data);

        enum class Type { BLANK, NAME, SEARCH };

        void copyTypeFrom(const Hook& rvalue);
        template <class H>
            H& setType(const H& h = H())
        {
            static_assert(
                SameType<H, NameHook>::result ||
                SameType<H, SearchHook>::result,
                "Invalid type passed to Hook::setType().");
            clearType();
            if (SameType<H, NameHook>::result)
                hookType = Type::NAME;
            else if (SameType<H, SearchHook>::result)
                hookType = Type::SEARCH;
            return *(H*)(hookData = new H(h));
        }
        template <class H>
            H& getTypeData() const
        {
            static_assert(
                SameType<H, NameHook>::result ||
                SameType<H, SearchHook>::result,
                "Invalid type passed to Hook::getTypeData().");
            if (hookType == Type::BLANK)
                throw std::logic_error("No type set.");
            if ((SameType<H, NameHook>::result && hookType == Type::NAME) ||
                (SameType<H, SearchHook>::result && hookType == Type::SEARCH))
                return *(H*)hookData;
            throw std::logic_error("Incorrect type passed to Hook::getTypeData().");
        }
        void clearType();
        Type getType() const;

        void checkValid() const;

        std::string name;
        size_t hookRva;
        size_t returnRva;
        size_t extraStackSpace;
        size_t stackSpaceToPopAfterReturn;
        std::string prologueFunction;
        std::string epilogueFunction;
        std::vector<uint8_t> prologueInstructionsBytes; // These two must not have any instructions that modifies esp, because it will change the return address!
        std::vector<uint8_t> epilogueInstructionsBytes; // As will writing to what esp points to with a negative offset. Reading is fine though.
        std::vector<std::string> headerIncludes;

    private:
        Type hookType;
        void* hookData;
};

class COMMON_EXPORT NameHook final : public NameSearch
{
    public:
        std::vector<uint8_t> serialise() const;
        void deserialise(const std::vector<uint8_t>& data);

        void checkValid(const Hook& parent) const;
};

class COMMON_EXPORT SearchHook final : public Search
{
    public:
        std::vector<uint8_t> serialise() const;
        void deserialise(const std::vector<uint8_t>& data);

        void checkValid(const Hook& parent) const;
};

}

#endif
