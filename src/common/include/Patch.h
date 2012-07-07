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
#ifndef PATCH_H
#define PATCH_H

#include <string>
#include <vector>
#include <set>
#include <map>
#include <utility>
#include <stdexcept>

#include <stdint.h>

#include "Misc.h"
#include "Search.h"
#include "Info.h"

namespace PatchData
{

class HookPatch;
class ReplaceNamePatch;
class ReplaceSearchPatch;
class COMMON_EXPORT Patch
{
    public:
        Patch();
        Patch(const Patch& rvalue);
        Patch& operator=(const Patch& rvalue);
        ~Patch();

        std::vector<uint8_t> serialise() const;
        void deserialise(const std::vector<uint8_t>& data);

        enum class Type { BLANK, HOOK, REPLACE_NAME, REPLACE_SEARCH };

        void copyTypeFrom(const Patch& rvalue);
        template <class P>
            P& setType(const P& p = P())
        {
            static_assert(
                SameType<P, HookPatch>::result ||
                SameType<P, ReplaceNamePatch>::result ||
                SameType<P, ReplaceSearchPatch>::result,
                "Invalid type passed to Patch::setType().");
            clearType();
            if (SameType<P, HookPatch>::result)
                patchType = Type::HOOK;
            else if (SameType<P, ReplaceNamePatch>::result)
                patchType = Type::REPLACE_NAME;
            else if (SameType<P, ReplaceSearchPatch>::result)
                patchType = Type::REPLACE_SEARCH;
            return *(P*)(patchData = new P(p));
        }
        template <class P>
            P& getTypeData() const
        {
            static_assert(
                SameType<P, HookPatch>::result ||
                SameType<P, ReplaceNamePatch>::result ||
                SameType<P, ReplaceSearchPatch>::result,
                "Invalid type passed to Patch::getTypeData().");
            if (patchType == Type::BLANK)
                throw std::logic_error("No type set.");
            if ((SameType<P, HookPatch>::result && patchType == Type::HOOK) ||
                (SameType<P, ReplaceNamePatch>::result && patchType == Type::REPLACE_NAME) ||
                (SameType<P, ReplaceSearchPatch>::result && patchType == Type::REPLACE_SEARCH))
                return *(P*)patchData;
            throw std::logic_error("Incorrect type passed to Patch::getTypeData().");
        }
        void clearType();
        Type getType() const;

        void checkValid() const;

    private:
        Type patchType;
        void* patchData;
};

class COMMON_EXPORT HookPatch final
{
    public:
        std::vector<uint8_t> serialise() const;
        void deserialise(const std::vector<uint8_t>& data);

        void checkValid(const Patch& parent) const;

        std::string hookName;
        std::string functionBody;
};

class COMMON_EXPORT ReplaceNamePatch final : public NameSearch
{
    public:
        std::vector<uint8_t> serialise() const;
        void deserialise(const std::vector<uint8_t>& data);

        void checkValid(const Patch& parent) const;

        std::vector<uint8_t> replaceBytes;
        std::set<size_t> ignoredReplaceBytesRvas;
};

class COMMON_EXPORT ReplaceSearchPatch final : public Search
{
    public:
        std::vector<uint8_t> serialise() const;
        void deserialise(const std::vector<uint8_t>& data);

        void checkValid(const Patch& parent) const;

        std::vector<uint8_t> replaceBytes;
        std::set<size_t> ignoredReplaceBytesRvas;
};

class COMMON_EXPORT PatchPack final
{
    public:
        std::vector<uint8_t> serialise() const;
        void deserialise(const std::vector<uint8_t>& data);

        Info info;
        std::vector<std::string> requiredPlugins;
        std::vector<Patch> patches;
        // These two are for use by hook patches only
        std::vector<std::string> headerIncludes;
        std::map<std::string, std::string> sharedVariables; // Name; Type
};

}

#endif
