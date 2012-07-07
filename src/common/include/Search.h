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
#ifndef SEARCH_H
#define SEARCH_H

#include <string>
#include <vector>
#include <set>
#include <stdexcept>

#include <stdint.h>

#include "Misc.h"

namespace PatchData
{

// Search Data

class SpecialSearch;
class COMMON_EXPORT Search
{
    public:
        virtual ~Search(){}

        std::vector<uint8_t> serialise() const;
        void deserialise(const std::vector<uint8_t>& data);

        virtual void checkValid(const size_t minSearchBytes) const;

        virtual std::set<uint8_t*> doSearch() const;

        std::string moduleName;
        std::vector<uint8_t> searchBytes;
        std::set<size_t> ignoredSearchBytesRvas;
        std::vector<SpecialSearch> specialSearches; // Special searches take priority over ignored search bytes

    protected:
        virtual std::set<uint8_t*> doSearch_(const uint8_t* start, size_t size) const final;
};

class COMMON_EXPORT NameSearch : public Search
{
    public:
        virtual ~NameSearch() = default;

        std::vector<uint8_t> serialise() const;
        void deserialise(const std::vector<uint8_t>& data);

        virtual void checkValid(const size_t minSearchBytes) const override;
        void checkOverlapWith(const NameSearch& rvalue) const;

        virtual std::set<uint8_t*> doSearch() const override;

        std::string functionName;
        size_t functionRva;
};

// Long class names ftw :D
class NamedRelativeFunctionCallSpecialSearch;
class UnnamedRelativeFunctionCallSpecialSearch;
class NamedAbsoluteIndirectFunctionCallSpecialSearch;
class UnnamedAbsoluteIndirectFunctionCallSpecialSearch;
class DataPointerSpecialSearch;
class COMMON_EXPORT SpecialSearch final
{
    public:
        SpecialSearch();
        SpecialSearch(const SpecialSearch& rvalue);
        SpecialSearch& operator=(const SpecialSearch& rvalue);
        ~SpecialSearch();

        std::vector<uint8_t> serialise() const;
        void deserialise(const std::vector<uint8_t>& data);

        enum class Type
        {
            BLANK,
            NAMED_RELATIVE_FUNCTION_CALL,
            UNNAMED_RELATIVE_FUNCTION_CALL,
            NAMED_ABSOLUTE_INDIRECT_FUNCTION_CALL,
            UNNAMED_ABSOLUTE_INDIRECT_FUNCTION_CALL,
            DATA_POINTER
        };

        void copyTypeFrom(const SpecialSearch& rvalue);
        template <class S>
            S& setType(const S& s = S())
        {
            static_assert(
                SameType<S, NamedRelativeFunctionCallSpecialSearch>::result ||
                SameType<S, UnnamedRelativeFunctionCallSpecialSearch>::result ||
                SameType<S, NamedAbsoluteIndirectFunctionCallSpecialSearch>::result ||
                SameType<S, UnnamedAbsoluteIndirectFunctionCallSpecialSearch>::result ||
                SameType<S, DataPointerSpecialSearch>::result,
                "Invalid type passed to SpecialSearch::setType().");
            clearType();
            if (SameType<S, NamedRelativeFunctionCallSpecialSearch>::result)
                specialSearchType = Type::NAMED_RELATIVE_FUNCTION_CALL;
            else if (SameType<S, UnnamedRelativeFunctionCallSpecialSearch>::result)
                specialSearchType = Type::UNNAMED_RELATIVE_FUNCTION_CALL;
            else if (SameType<S, NamedAbsoluteIndirectFunctionCallSpecialSearch>::result)
                specialSearchType = Type::NAMED_ABSOLUTE_INDIRECT_FUNCTION_CALL;
            else if (SameType<S, UnnamedAbsoluteIndirectFunctionCallSpecialSearch>::result)
                specialSearchType = Type::UNNAMED_ABSOLUTE_INDIRECT_FUNCTION_CALL;
            else if (SameType<S, DataPointerSpecialSearch>::result)
                specialSearchType = Type::DATA_POINTER;
            return *(S*)(specialSearchData = new S(s));
        }
        template <class S>
            S& getTypeData() const
        {
            static_assert(
                SameType<S, NamedRelativeFunctionCallSpecialSearch>::result ||
                SameType<S, UnnamedRelativeFunctionCallSpecialSearch>::result ||
                SameType<S, NamedAbsoluteIndirectFunctionCallSpecialSearch>::result ||
                SameType<S, UnnamedAbsoluteIndirectFunctionCallSpecialSearch>::result ||
                SameType<S, DataPointerSpecialSearch>::result,
                "Invalid type passed to SpecialSearch::getTypeData().");
            if (specialSearchType == Type::BLANK)
                throw std::logic_error("No type set.");
            if ((SameType<S, NamedRelativeFunctionCallSpecialSearch>::result && specialSearchType == Type::NAMED_RELATIVE_FUNCTION_CALL) ||
                (SameType<S, UnnamedRelativeFunctionCallSpecialSearch>::result && specialSearchType == Type::UNNAMED_RELATIVE_FUNCTION_CALL) ||
                (SameType<S, NamedAbsoluteIndirectFunctionCallSpecialSearch>::result && specialSearchType == Type::NAMED_ABSOLUTE_INDIRECT_FUNCTION_CALL) ||
                (SameType<S, UnnamedAbsoluteIndirectFunctionCallSpecialSearch>::result && specialSearchType == Type::UNNAMED_ABSOLUTE_INDIRECT_FUNCTION_CALL) ||
                (SameType<S, DataPointerSpecialSearch>::result && specialSearchType == Type::DATA_POINTER))
                return *(S*)specialSearchData;
            throw std::logic_error("Incorrect type passed to SpecialSearch::getTypeData().");
        }
        void clearType();
        Type getType() const;

        void checkValid(const Search& parent) const;
        bool doSearch(const uint8_t* address) const;

        size_t searchBytesRva;

    private:
        Type specialSearchType;
        void* specialSearchData;
};

class MANAGER_EXPORT NamedRelativeFunctionCallSpecialSearch final
{
    public:
        std::vector<uint8_t> serialise() const;
        void deserialise(const std::vector<uint8_t>& data);

        void checkValid(const SpecialSearch& parent, const Search& parentParent) const;
        bool doSearch(const uint8_t* address) const;

        std::string moduleName;
        std::string functionName;
};

class MANAGER_EXPORT UnnamedRelativeFunctionCallSpecialSearch final : public Search
{
    public:
        std::vector<uint8_t> serialise() const;
        void deserialise(const std::vector<uint8_t>& data);

        void checkValid(const SpecialSearch& parent, const Search& parentParent) const;
        bool doSearch(const uint8_t* address) const; // moduleName member is ignored
};

class MANAGER_EXPORT NamedAbsoluteIndirectFunctionCallSpecialSearch final
{
    public:
        std::vector<uint8_t> serialise() const;
        void deserialise(const std::vector<uint8_t>& data);

        void checkValid(const SpecialSearch& parent, const Search& parentParent) const;
        bool doSearch(const uint8_t* address) const;

        std::string moduleName;
        std::string functionName;
};

class MANAGER_EXPORT UnnamedAbsoluteIndirectFunctionCallSpecialSearch final : public Search
{
    public:
        std::vector<uint8_t> serialise() const;
        void deserialise(const std::vector<uint8_t>& data);

        void checkValid(const SpecialSearch& parent, const Search& parentParent) const;
        bool doSearch(const uint8_t* address) const; // moduleName member is ignored
};

class MANAGER_EXPORT DataPointerSpecialSearch final : public Search
{
    public:
        std::vector<uint8_t> serialise() const;
        void deserialise(const std::vector<uint8_t>& data);

        void checkValid(const SpecialSearch& parent, const Search& parentParent) const;
        bool doSearch(const uint8_t* address) const; // moduleName member is ignored
};

}

#endif
