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

#include "Patch.h"

namespace PatchData
{

// Patch class

Patch::Patch():
    patchType(Type::BLANK)
{
}

Patch::Patch(const Patch& rvalue):
    patchType(Type::BLANK)
{
    copyTypeFrom(rvalue);
}

Patch& Patch::operator=(const Patch& rvalue)
{
    if (this == &rvalue)
        return *this;
    copyTypeFrom(rvalue);
    return *this;
}

Patch::~Patch()
{
    clearType();
}

std::vector<uint8_t> Patch::serialise() const
{
    std::vector<uint8_t> data;
    data.reserve(1024);

    serialiseIntegralType(data, patchType);
    switch (patchType)
    {
        case Type::HOOK :
            serialiseIntegralTypeContinuousContainer(data, getTypeData<HookPatch>().serialise());
            break;

        case Type::REPLACE_NAME :
            serialiseIntegralTypeContinuousContainer(data, getTypeData<ReplaceNamePatch>().serialise());
            break;

        case Type::REPLACE_SEARCH :
            serialiseIntegralTypeContinuousContainer(data, getTypeData<ReplaceSearchPatch>().serialise());
            break;

        case Type::BLANK :
            break;

        default:
            assert(false); // Should never get here
    }

    return data;
}

void Patch::deserialise(const std::vector<uint8_t>& data)
{
    auto iterator = data.cbegin();

    switch (deserialiseIntegralType<Type>(iterator))
    {
        case Type::HOOK :
            setType<HookPatch>().deserialise(deserialiseIntegralTypeContinuousContainer<std::vector<uint8_t>>(iterator));
            break;

        case Type::REPLACE_NAME :
            setType<ReplaceNamePatch>().deserialise(deserialiseIntegralTypeContinuousContainer<std::vector<uint8_t>>(iterator));
            break;

        case Type::REPLACE_SEARCH :
            setType<ReplaceSearchPatch>().deserialise(deserialiseIntegralTypeContinuousContainer<std::vector<uint8_t>>(iterator));
            break;

        case Type::BLANK :
            clearType();
            break;

        default:
            assert(false); // Should never get here
    }
}

void Patch::copyTypeFrom(const Patch& rvalue)
{
    switch (rvalue.patchType)
    {
        case Type::HOOK :
            setType<HookPatch>(rvalue.getTypeData<HookPatch>());
            break;

        case Type::REPLACE_NAME :
            setType<ReplaceNamePatch>(rvalue.getTypeData<ReplaceNamePatch>());
            break;

        case Type::REPLACE_SEARCH :
            setType<ReplaceSearchPatch>(rvalue.getTypeData<ReplaceSearchPatch>());
            break;

        case Type::BLANK :
            clearType();
            break;

        default:
            assert(false); // Should never get here
    }
}

void Patch::clearType()
{
    switch (patchType)
    {
        case Type::HOOK :
            delete &getTypeData<HookPatch>();
            break;

        case Type::REPLACE_NAME :
            delete &getTypeData<ReplaceNamePatch>();
            break;

        case Type::REPLACE_SEARCH :
            delete &getTypeData<ReplaceSearchPatch>();
            break;

        case Type::BLANK :
            break;

        default:
            assert(false); // Should never get here
    }
    patchType = Type::BLANK;
}

Patch::Type Patch::getType() const
{
    return patchType;
}

void Patch::checkValid() const
{
    switch (patchType)
    {
        case Type::HOOK :
            getTypeData<HookPatch>().checkValid(*this);
            break;

        case Type::REPLACE_NAME :
            getTypeData<ReplaceNamePatch>().checkValid(*this);
            break;

        case Type::REPLACE_SEARCH :
            getTypeData<ReplaceSearchPatch>().checkValid(*this);
            break;

        case Type::BLANK :
            throw std::logic_error("Patch cannot be blank.");

        default:
            assert(false); // Should never get here
    }
}

// Patch data classes

std::vector<uint8_t> HookPatch::serialise() const
{
    std::vector<uint8_t> data;
    data.reserve(1024);

    serialiseIntegralTypeContinuousContainer(data, hookName);
    serialiseIntegralTypeContinuousContainer(data, functionBody);

    return data;
}

void HookPatch::deserialise(const std::vector<uint8_t>& data)
{
    auto iterator = data.cbegin();

    deserialiseIntegralTypeContinuousContainer(iterator, hookName);
    deserialiseIntegralTypeContinuousContainer(iterator, functionBody);
}

void HookPatch::checkValid(const Patch& /*parent*/) const
{
    if (hookName.empty())
        throw std::logic_error("The hook name cannot be empty.");
    if (functionBody.empty())
        throw std::logic_error("The function body cannot be empty.");
}

std::vector<uint8_t> ReplaceNamePatch::serialise() const
{
    std::vector<uint8_t> data;
    data.reserve(1024);

    serialiseIntegralTypeContinuousContainer(data, replaceBytes);
    serialiseIntegralTypeContainer(data, ignoredReplaceBytesRvas);
    serialiseIntegralTypeContinuousContainer(data, NameSearch::serialise());

    return data;
}

void ReplaceNamePatch::deserialise(const std::vector<uint8_t>& data)
{
    auto iterator = data.cbegin();

    deserialiseIntegralTypeContinuousContainer(iterator, replaceBytes);
    deserialiseIntegralTypeContainer(iterator, ignoredReplaceBytesRvas);
    NameSearch::deserialise(deserialiseIntegralTypeContinuousContainer<std::vector<uint8_t>>(iterator));
}

void ReplaceNamePatch::checkValid(const Patch& /*patch*/) const
{
    // Check that the largest rva in `ignoredReplaceBytesRvas' is not larger than the replace bytes
    for (const auto& ignoredReplaceBytesRva : ignoredReplaceBytesRvas)
        if (ignoredReplaceBytesRva >= replaceBytes.size())
            throw std::logic_error("All ignored replace byte RVAs must be less than the replace bytes length.");

    // The replace bytes have to be the same size as the search bytes
    if (replaceBytes.size() != searchBytes.size())
        throw std::logic_error("Search bytes and replace bytes must be the same size.");
    NameSearch::checkValid(replaceBytes.size());
}

std::vector<uint8_t> ReplaceSearchPatch::serialise() const
{
    std::vector<uint8_t> data;
    data.reserve(1024);

    serialiseIntegralTypeContinuousContainer(data, replaceBytes);
    serialiseIntegralTypeContainer(data, ignoredReplaceBytesRvas);
    serialiseIntegralTypeContinuousContainer(data, Search::serialise());

    return data;
}

void ReplaceSearchPatch::deserialise(const std::vector<uint8_t>& data)
{
    auto iterator = data.cbegin();

    deserialiseIntegralTypeContinuousContainer(iterator, replaceBytes);
    deserialiseIntegralTypeContainer(iterator, ignoredReplaceBytesRvas);
    Search::deserialise(deserialiseIntegralTypeContinuousContainer<std::vector<uint8_t>>(iterator));
}

void ReplaceSearchPatch::checkValid(const Patch& /*patch*/) const
{
    // Check that the largest rva in `ignoredReplaceBytesRvas' is not larger than the replace bytes
    for (const auto& ignoredReplaceBytesRva : ignoredReplaceBytesRvas)
        if (ignoredReplaceBytesRva >= replaceBytes.size())
            throw std::logic_error("All ignored replace byte RVAs must be less than the replace bytes length.");

    // The replace bytes have to be the same size as the search bytes
    if (replaceBytes.size() != searchBytes.size())
        throw std::logic_error("Search bytes and replace bytes must be the same size.");
    Search::checkValid(replaceBytes.size());
}

// PatchPack class

std::vector<uint8_t> PatchPack::serialise() const
{
    std::vector<uint8_t> data;
    data.reserve(1024);

    // info
    serialiseIntegralTypeContinuousContainer(data, info.serialise());
    // requiredPlugins
    serialiseIntegralType(data, requiredPlugins.size());
    for (const auto& requiredPlugin : requiredPlugins)
        serialiseIntegralTypeContinuousContainer(data, requiredPlugin);
    // patches
    serialiseSerialisableTypeContainer(data, patches);
    // headerIncludes
    serialiseIntegralType(data, headerIncludes.size());
    for (const auto& headerInclude : headerIncludes)
        serialiseIntegralTypeContinuousContainer(data, headerInclude);
    // sharedVariables
    serialiseIntegralType(data, sharedVariables.size());
    for (const auto& sharedVariable : sharedVariables)
    {
        serialiseIntegralTypeContinuousContainer(data, sharedVariable.first);
        serialiseIntegralTypeContinuousContainer(data, sharedVariable.second);
    }

    return data;
}

void PatchPack::deserialise(const std::vector<uint8_t>& data)
{
    auto iterator = data.cbegin();

    // info
    info.deserialise(deserialiseIntegralTypeContinuousContainer<std::vector<uint8_t>>(iterator));
    // requiredPlugins
    std::vector<std::string>::size_type requiredPluginsSize = deserialiseIntegralType<std::vector<std::string>::size_type>(iterator);
    requiredPlugins.clear();
    requiredPlugins.reserve(requiredPluginsSize);
    for (std::vector<std::string>::size_type s = 0; s < requiredPluginsSize; ++s)
        requiredPlugins.push_back(deserialiseIntegralTypeContinuousContainer<std::vector<std::string>::value_type>(iterator));
    // patches
    deserialiseDeserialisableTypeContainer(iterator, patches);
    // headerIncludes
    std::vector<std::string>::size_type headerIncludesSize = deserialiseIntegralType<std::vector<std::string>::size_type>(iterator);
    headerIncludes.clear();
    headerIncludes.reserve(headerIncludesSize);
    for (std::vector<std::string>::size_type s = 0; s < headerIncludesSize; ++s)
        headerIncludes.push_back(deserialiseIntegralTypeContinuousContainer<std::vector<std::string>::value_type>(iterator));
    // sharedVariables
    std::map<std::string, std::string>::size_type sharedVariablesSize = deserialiseIntegralType<std::map<std::string, std::string>::size_type>(iterator);
    sharedVariables.clear();
    for (std::map<std::string, std::string>::size_type s = 0; s < sharedVariablesSize; ++s)
        sharedVariables.insert(std::make_pair(deserialiseIntegralTypeContinuousContainer<std::string>(iterator),
                                              deserialiseIntegralTypeContinuousContainer<std::string>(iterator)));
}

}
