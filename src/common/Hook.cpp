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

#include <cassert>

#include "Hook.h"

namespace PatchData
{

// Hook class

Hook::Hook():
    hookRva(0),
    returnRva(0),
    extraStackSpace(0),
    stackSpaceToPopAfterReturn(0),
    hookType(Type::BLANK)
{
}

Hook::Hook(const Hook& rvalue):
    name(rvalue.name),
    hookRva(rvalue.hookRva),
    returnRva(rvalue.returnRva),
    extraStackSpace(rvalue.extraStackSpace),
    stackSpaceToPopAfterReturn(rvalue.stackSpaceToPopAfterReturn),
    prologueFunction(rvalue.prologueFunction),
    epilogueFunction(rvalue.epilogueFunction),
    prologueInstructionsBytes(rvalue.prologueInstructionsBytes),
    epilogueInstructionsBytes(rvalue.epilogueInstructionsBytes),
    headerIncludes(rvalue.headerIncludes),
    hookType(Type::BLANK)
{
    copyTypeFrom(rvalue);
}

Hook& Hook::operator=(const Hook& rvalue)
{
    if (this == &rvalue)
        return *this;
    name = rvalue.name;
    hookRva = rvalue.hookRva;
    returnRva = rvalue.returnRva;
    extraStackSpace = rvalue.extraStackSpace;
    stackSpaceToPopAfterReturn = rvalue.stackSpaceToPopAfterReturn;
    prologueFunction = rvalue.prologueFunction;
    epilogueFunction = rvalue.epilogueFunction;
    prologueInstructionsBytes = rvalue.prologueInstructionsBytes;
    epilogueInstructionsBytes = rvalue.epilogueInstructionsBytes;
    headerIncludes = rvalue.headerIncludes;
    copyTypeFrom(rvalue);
    return *this;
}

Hook::~Hook()
{
    clearType();
}

std::vector<uint8_t> Hook::serialise() const
{
    std::vector<uint8_t> data;
    data.reserve(1024);

    serialiseIntegralTypeContinuousContainer(data, name);
    serialiseIntegralType(data, hookRva);
    serialiseIntegralType(data, returnRva);
    serialiseIntegralType(data, extraStackSpace);
    serialiseIntegralType(data, stackSpaceToPopAfterReturn);
    serialiseIntegralTypeContinuousContainer(data, prologueFunction);
    serialiseIntegralTypeContinuousContainer(data, epilogueFunction);
    serialiseIntegralTypeContinuousContainer(data, prologueInstructionsBytes);
    serialiseIntegralTypeContinuousContainer(data, epilogueInstructionsBytes);
    // headerIncludes
    serialiseIntegralType(data, headerIncludes.size());
    for (const auto& headerInclude : headerIncludes)
        serialiseIntegralTypeContinuousContainer(data, headerInclude);

    // Serialise specialised hooks
    serialiseIntegralType(data, hookType);
    switch (hookType)
    {
        case Type::NAME :
            serialiseIntegralTypeContinuousContainer(data, getTypeData<NameHook>().serialise());
            break;

        case Type::SEARCH :
            serialiseIntegralTypeContinuousContainer(data, getTypeData<SearchHook>().serialise());
            break;

        case Type::BLANK :
            break;

        default:
            assert(false); // Should never get here
    }

    return data;
}

void Hook::deserialise(const std::vector<uint8_t>& data)
{
    auto iterator = data.cbegin();

    deserialiseIntegralTypeContinuousContainer(iterator, name);
    deserialiseIntegralType(iterator, hookRva);
    deserialiseIntegralType(iterator, returnRva);
    deserialiseIntegralType(iterator, extraStackSpace);
    deserialiseIntegralType(iterator, stackSpaceToPopAfterReturn);
    deserialiseIntegralTypeContinuousContainer(iterator, prologueFunction);
    deserialiseIntegralTypeContinuousContainer(iterator, epilogueFunction);
    deserialiseIntegralTypeContinuousContainer(iterator, prologueInstructionsBytes);
    deserialiseIntegralTypeContinuousContainer(iterator, epilogueInstructionsBytes);
    // headerIncludes
    std::vector<std::string>::size_type headerIncludesSize = deserialiseIntegralType<std::vector<std::string>::size_type>(iterator);
    headerIncludes.clear();
    headerIncludes.reserve(headerIncludesSize);
    for (std::vector<std::string>::size_type s = 0; s < headerIncludesSize; ++s)
        headerIncludes.push_back(deserialiseIntegralTypeContinuousContainer<std::vector<std::string>::value_type>(iterator));

    // Deserialise specialised hooks
    switch (deserialiseIntegralType<Type>(iterator))
    {
        case Type::NAME :
            setType<NameHook>().deserialise(deserialiseIntegralTypeContinuousContainer<std::vector<uint8_t>>(iterator));
            break;

        case Type::SEARCH :
            setType<SearchHook>().deserialise(deserialiseIntegralTypeContinuousContainer<std::vector<uint8_t>>(iterator));
            break;

        case Type::BLANK :
            clearType();
            break;

        default:
            assert(false); // Should never get here
    }
}

void Hook::copyTypeFrom(const Hook& rvalue)
{
    switch (rvalue.hookType)
    {
        case Type::NAME :
            setType<NameHook>(rvalue.getTypeData<NameHook>());
            break;

        case Type::SEARCH :
            setType<SearchHook>(rvalue.getTypeData<SearchHook>());
            break;

        case Type::BLANK :
            clearType();
            break;

        default:
            assert(false); // Should never get here
    }
}

void Hook::clearType()
{
    switch (hookType)
    {
        case Type::NAME :
            delete &getTypeData<NameHook>();
            break;

        case Type::SEARCH :
            delete &getTypeData<SearchHook>();
            break;

        case Type::BLANK :
            break;

        default:
            assert(false); // Should never get here
    }
    hookType = Type::BLANK;
}

Hook::Type Hook::getType() const
{
    return hookType;
}

void Hook::checkValid() const
{
    switch (hookType)
    {
        case Type::NAME :
            getTypeData<NameHook>().checkValid(*this);
            break;

        case Type::SEARCH :
            getTypeData<SearchHook>().checkValid(*this);
            break;

        case Type::BLANK :
            throw std::logic_error("Hook cannot be blank.");

        default:
            assert(false); // Should never get here
    }
}

// Hook data classes

std::vector<uint8_t> NameHook::serialise() const
{
    std::vector<uint8_t> data;
    data.reserve(1024);

    serialiseIntegralTypeContinuousContainer(data, NameSearch::serialise());

    return data;
}

void NameHook::deserialise(const std::vector<uint8_t>& data)
{
    auto iterator = data.cbegin();

    NameSearch::deserialise(deserialiseIntegralTypeContinuousContainer<std::vector<uint8_t>>(iterator));
}

void NameHook::checkValid(const Hook& parent) const
{
    NameSearch::checkValid(parent.hookRva + 5 + parent.returnRva);
}

std::vector<uint8_t> SearchHook::serialise() const
{
    std::vector<uint8_t> data;
    data.reserve(1024);

    serialiseIntegralTypeContinuousContainer(data, Search::serialise());

    return data;
}

void SearchHook::deserialise(const std::vector<uint8_t>& data)
{
    auto iterator = data.cbegin();

    Search::deserialise(deserialiseIntegralTypeContinuousContainer<std::vector<uint8_t>>(iterator));
}

void SearchHook::checkValid(const Hook& parent) const
{
    Search::checkValid(parent.hookRva + 5 + parent.returnRva);
}

}
