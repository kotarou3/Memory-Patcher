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

#include <utility>
#include <algorithm>

#include <cassert>

#include "Search.h"
#include "Module.h"

namespace PatchData
{

// Search class

std::vector<uint8_t> Search::serialise() const
{
    std::vector<uint8_t> data;
    data.reserve(1024);

    serialiseIntegralTypeContinuousContainer(data, moduleName);
    serialiseIntegralTypeContinuousContainer(data, searchBytes);
    serialiseIntegralTypeContainer(data, ignoredSearchBytesRvas);
    serialiseSerialisableTypeContainer(data, specialSearches);

    return data;
}

void Search::deserialise(const std::vector<uint8_t>& data)
{
    auto iterator = data.cbegin();

    deserialiseIntegralTypeContinuousContainer(iterator, moduleName);
    deserialiseIntegralTypeContinuousContainer(iterator, searchBytes);
    deserialiseIntegralTypeContainer(iterator, ignoredSearchBytesRvas);
    deserialiseDeserialisableTypeContainer(iterator, specialSearches);
}

void Search::checkValid(const size_t minSearchBytes) const
{
    if (moduleName.empty())
        throw std::logic_error("The module name cannot be empty.");
    if (searchBytes.size() < minSearchBytes)
        throw std::logic_error("There must be at least " + itos(minSearchBytes) + " search byte(s).");
    size_t searchBytesSize = searchBytes.size();

    // Check that the largest rva in `ignoredSearchBytesRvas' is not larger than the search bytes
    for (const auto& ignoredSearchBytesRva : ignoredSearchBytesRvas)
        if (ignoredSearchBytesRva >= searchBytesSize)
            throw std::logic_error("All ignored search byte RVAs must be less than the search bytes length.");

    // Check the special searches
    std::set<size_t> usedSearchBytesRvas; // Make sure every special search has a unique search bytes RVA
    for (const auto& specialSearch : specialSearches)
    {
        if (usedSearchBytesRvas.count(specialSearch.searchBytesRva) > 0)
            throw std::logic_error("All special searches must have a unique search bytes RVA.");
        usedSearchBytesRvas.insert(specialSearch.searchBytesRva);

        // Call it's specific validation check
        specialSearch.checkValid(*this);
    }
}

std::set<uint8_t*> Search::doSearch() const
{
    checkValid(searchBytes.size());
    Module module;
    module.open(moduleName);
    return doSearch_(module.getSegments().front().start, (module.getSegments().back().start + module.getSegments().back().size) - module.getSegments().front().start);
}

std::set<uint8_t*> Search::doSearch_(const uint8_t* start, size_t size) const
{
    TRACE("Searching from 0x" << std::hex << (size_t)start << " to 0x" << size << std::dec);
    // Create a new search byte vector which marks any bytes with special treatment
    class Info
    {
        public:
            bool isIgnoredByte;
            bool isSpecialSearchByte;
            std::vector<SpecialSearch>::const_iterator specialSearch;
    };
    std::vector<std::pair<Info, uint8_t>> flaggedSearchBytes;
    flaggedSearchBytes.reserve(searchBytes.size());
    size_t currentSearchBytesRva = 0;
    for (const auto& searchByte : searchBytes)
    {
        Info info;
        info.isIgnoredByte = ignoredSearchBytesRvas.count(currentSearchBytesRva) > 0;
        info.isSpecialSearchByte = false;
        for (auto specialSearch = specialSearches.cbegin(); specialSearch != specialSearches.cend(); ++specialSearch)
            if (specialSearch->searchBytesRva == currentSearchBytesRva)
            {
                info.isSpecialSearchByte = true;
                info.specialSearch = specialSearch;
                break;
            }
        flaggedSearchBytes.push_back(std::make_pair(info, searchByte));
        ++currentSearchBytesRva;
    }

    // Search each segment individually
    auto segments = Memory::queryPage(start, size);
    std::set<uint8_t*> results;
    for (const auto& segment : segments)
    {
        // If the segment isn't readable, make it readable!
        bool isProtectionChanged = false;
        if (!segment.isReadable)
        {
            Memory::PageInfo newSegment = segment;
            newSegment.isReadable = true;
            Memory::changePageProtection(newSegment);
            isProtectionChanged = true;
        }

        // Actual search
        uint8_t* searchStart = segment.start;
        uint8_t* searchEnd = searchStart + segment.size;
        while (searchStart < searchEnd)
        {
            uint8_t* result = std::search(searchStart, searchEnd, flaggedSearchBytes.begin(), flaggedSearchBytes.end(),
                [](const uint8_t& byte, const std::pair<Info, uint8_t>& searchByte) -> bool
                {
                    if (searchByte.first.isSpecialSearchByte)
                        if (!searchByte.first.specialSearch->doSearch(&byte))
                            return false;
                    if (searchByte.first.isIgnoredByte)
                        return true;
                    else
                        return byte == searchByte.second;
                });
            if (result >= searchEnd)
                break;
            results.insert(result);
            searchStart = result + searchBytes.size();
        }

        // If we changed the page protection, change it back
        if (isProtectionChanged)
            Memory::changePageProtection(segment);
    }
    return results;
}

std::vector<uint8_t> NameSearch::serialise() const
{
    std::vector<uint8_t> data;
    data.reserve(1024);

    serialiseIntegralTypeContinuousContainer(data, Search::serialise());
    serialiseIntegralTypeContinuousContainer(data, functionName);
    serialiseIntegralType(data, functionRva);

    return data;
}

void NameSearch::deserialise(const std::vector<uint8_t>& data)
{
    auto iterator = data.cbegin();

    Search::deserialise(deserialiseIntegralTypeContinuousContainer<std::vector<uint8_t>>(iterator));
    deserialiseIntegralTypeContinuousContainer(iterator, functionName);
    deserialiseIntegralType(iterator, functionRva);
}

void NameSearch::checkValid(const size_t minSearchBytes) const
{
    Search::checkValid(minSearchBytes);
    if (functionName.empty())
        throw std::logic_error("The function name cannot be empty.");
}

void NameSearch::checkOverlapWith(const NameSearch& rvalue) const
{
    if (moduleName == rvalue.moduleName && // It has to be in the same module and
        functionName == rvalue.functionName) // function for any chance of overlapping!
    {
        const size_t& searchStart = functionRva;
        const size_t& searchEnd = searchStart + rvalue.searchBytes.size();
        const size_t& rvalueSearchStart = rvalue.functionRva;
        const size_t& rvalueSearchEnd = rvalueSearchStart + rvalue.searchBytes.size();
        if (searchStart <= rvalueSearchEnd && rvalueSearchStart <= searchEnd)
            throw std::logic_error("The name search overlaps with another name search.");
    }
}

std::set<uint8_t*> NameSearch::doSearch() const
{
    checkValid(searchBytes.size());
    Module module;
    module.open(moduleName);
    return doSearch_(module.getSymbol(functionName) + functionRva, searchBytes.size());
}

// SpecialSearch class

SpecialSearch::SpecialSearch():
    searchBytesRva(0),
    specialSearchType(Type::BLANK)
{
}

SpecialSearch::SpecialSearch(const SpecialSearch& rvalue):
    searchBytesRva(rvalue.searchBytesRva),
    specialSearchType(Type::BLANK)
{
    copyTypeFrom(rvalue);
}

SpecialSearch& SpecialSearch::operator=(const SpecialSearch& rvalue)
{
    if (this == &rvalue)
        return *this;
    searchBytesRva = rvalue.searchBytesRva;
    copyTypeFrom(rvalue);
    return *this;
}

SpecialSearch::~SpecialSearch()
{
    clearType();
}

std::vector<uint8_t> SpecialSearch::serialise() const
{
    std::vector<uint8_t> data;
    data.reserve(1024);

    serialiseIntegralType(data, searchBytesRva);
    serialiseIntegralType(data, specialSearchType);
    switch (specialSearchType)
    {
        case Type::NAMED_RELATIVE_FUNCTION_CALL :
            serialiseIntegralTypeContinuousContainer(data, getTypeData<NamedRelativeFunctionCallSpecialSearch>().serialise());
            break;

        case Type::UNNAMED_RELATIVE_FUNCTION_CALL :
            serialiseIntegralTypeContinuousContainer(data, getTypeData<UnnamedRelativeFunctionCallSpecialSearch>().serialise());
            break;

        case Type::NAMED_ABSOLUTE_INDIRECT_FUNCTION_CALL :
            serialiseIntegralTypeContinuousContainer(data, getTypeData<NamedAbsoluteIndirectFunctionCallSpecialSearch>().serialise());
            break;

        case Type::UNNAMED_ABSOLUTE_INDIRECT_FUNCTION_CALL :
            serialiseIntegralTypeContinuousContainer(data, getTypeData<UnnamedAbsoluteIndirectFunctionCallSpecialSearch>().serialise());
            break;

        case Type::DATA_POINTER :
            serialiseIntegralTypeContinuousContainer(data, getTypeData<DataPointerSpecialSearch>().serialise());
            break;

        case Type::BLANK :
            break;

        default:
            assert(false); // Should never get here
    }

    return data;
}

void SpecialSearch::deserialise(const std::vector<uint8_t>& data)
{
    auto iterator = data.cbegin();

    deserialiseIntegralType(iterator, searchBytesRva);
    switch (deserialiseIntegralType<Type>(iterator))
    {
        case Type::NAMED_RELATIVE_FUNCTION_CALL :
            setType<NamedRelativeFunctionCallSpecialSearch>().deserialise(deserialiseIntegralTypeContinuousContainer<std::vector<uint8_t>>(iterator));
            break;

        case Type::UNNAMED_RELATIVE_FUNCTION_CALL :
            setType<UnnamedRelativeFunctionCallSpecialSearch>().deserialise(deserialiseIntegralTypeContinuousContainer<std::vector<uint8_t>>(iterator));
            break;

        case Type::NAMED_ABSOLUTE_INDIRECT_FUNCTION_CALL :
            setType<NamedAbsoluteIndirectFunctionCallSpecialSearch>().deserialise(deserialiseIntegralTypeContinuousContainer<std::vector<uint8_t>>(iterator));
            break;

        case Type::UNNAMED_ABSOLUTE_INDIRECT_FUNCTION_CALL :
            setType<UnnamedAbsoluteIndirectFunctionCallSpecialSearch>().deserialise(deserialiseIntegralTypeContinuousContainer<std::vector<uint8_t>>(iterator));
            break;

        case Type::DATA_POINTER :
            setType<DataPointerSpecialSearch>().deserialise(deserialiseIntegralTypeContinuousContainer<std::vector<uint8_t>>(iterator));
            break;

        case Type::BLANK :
            clearType();
            break;

        default:
            assert(false); // Should never get here
    }
}

void SpecialSearch::copyTypeFrom(const SpecialSearch& rvalue)
{
    switch (rvalue.specialSearchType)
    {
        case Type::NAMED_RELATIVE_FUNCTION_CALL :
            setType<NamedRelativeFunctionCallSpecialSearch>(rvalue.getTypeData<NamedRelativeFunctionCallSpecialSearch>());
            break;

        case Type::UNNAMED_RELATIVE_FUNCTION_CALL :
            setType<UnnamedRelativeFunctionCallSpecialSearch>(rvalue.getTypeData<UnnamedRelativeFunctionCallSpecialSearch>());
            break;

        case Type::NAMED_ABSOLUTE_INDIRECT_FUNCTION_CALL :
            setType<NamedAbsoluteIndirectFunctionCallSpecialSearch>(rvalue.getTypeData<NamedAbsoluteIndirectFunctionCallSpecialSearch>());
            break;

        case Type::UNNAMED_ABSOLUTE_INDIRECT_FUNCTION_CALL :
            setType<UnnamedAbsoluteIndirectFunctionCallSpecialSearch>(rvalue.getTypeData<UnnamedAbsoluteIndirectFunctionCallSpecialSearch>());
            break;

        case Type::DATA_POINTER :
            setType<DataPointerSpecialSearch>(rvalue.getTypeData<DataPointerSpecialSearch>());
            break;

        case Type::BLANK :
            clearType();
            break;

        default:
            assert(false); // Should never get here
    }
}

void SpecialSearch::clearType()
{
    switch (specialSearchType)
    {
        case Type::NAMED_RELATIVE_FUNCTION_CALL :
            delete &getTypeData<NamedRelativeFunctionCallSpecialSearch>();
            break;

        case Type::UNNAMED_RELATIVE_FUNCTION_CALL :
            delete &getTypeData<UnnamedRelativeFunctionCallSpecialSearch>();
            break;

        case Type::NAMED_ABSOLUTE_INDIRECT_FUNCTION_CALL :
            delete &getTypeData<NamedAbsoluteIndirectFunctionCallSpecialSearch>();
            break;

        case Type::UNNAMED_ABSOLUTE_INDIRECT_FUNCTION_CALL :
            delete &getTypeData<UnnamedAbsoluteIndirectFunctionCallSpecialSearch>();
            break;

        case Type::DATA_POINTER :
            delete &getTypeData<DataPointerSpecialSearch>();
            break;

        case Type::BLANK :
            break;

        default:
            assert(false); // Should never get here
    }
    specialSearchType = Type::BLANK;
}

SpecialSearch::Type SpecialSearch::getType() const
{
    return specialSearchType;
}

void SpecialSearch::checkValid(const Search& parent) const
{
    switch (specialSearchType)
    {
        case Type::NAMED_RELATIVE_FUNCTION_CALL :
            getTypeData<NamedRelativeFunctionCallSpecialSearch>().checkValid(*this, parent);
            break;

        case Type::UNNAMED_RELATIVE_FUNCTION_CALL :
            getTypeData<UnnamedRelativeFunctionCallSpecialSearch>().checkValid(*this, parent);
            break;

        case Type::NAMED_ABSOLUTE_INDIRECT_FUNCTION_CALL :
            getTypeData<NamedAbsoluteIndirectFunctionCallSpecialSearch>().checkValid(*this, parent);
            break;

        case Type::UNNAMED_ABSOLUTE_INDIRECT_FUNCTION_CALL :
            getTypeData<UnnamedAbsoluteIndirectFunctionCallSpecialSearch>().checkValid(*this, parent);
            break;

        case Type::DATA_POINTER :
            getTypeData<DataPointerSpecialSearch>().checkValid(*this, parent);
            break;

        case Type::BLANK :
            throw std::logic_error("Special search cannot be blank.");

        default:
            assert(false); // Should never get here
    }
}

bool SpecialSearch::doSearch(const uint8_t* address) const
{
    switch (specialSearchType)
    {
        case Type::NAMED_RELATIVE_FUNCTION_CALL :
            return getTypeData<NamedRelativeFunctionCallSpecialSearch>().doSearch(address);

        case Type::UNNAMED_RELATIVE_FUNCTION_CALL :
            return getTypeData<UnnamedRelativeFunctionCallSpecialSearch>().doSearch(address);

        case Type::NAMED_ABSOLUTE_INDIRECT_FUNCTION_CALL :
            return getTypeData<NamedAbsoluteIndirectFunctionCallSpecialSearch>().doSearch(address);

        case Type::UNNAMED_ABSOLUTE_INDIRECT_FUNCTION_CALL :
            return getTypeData<UnnamedAbsoluteIndirectFunctionCallSpecialSearch>().doSearch(address);

        case Type::DATA_POINTER :
            return getTypeData<DataPointerSpecialSearch>().doSearch(address);
    }
    assert(false); // Should never get here
}

// SpecialSearch data classes

std::vector<uint8_t> NamedRelativeFunctionCallSpecialSearch::serialise() const
{
    std::vector<uint8_t> data;
    data.reserve(1024);

    serialiseIntegralTypeContinuousContainer(data, functionName);

    return data;
}

void NamedRelativeFunctionCallSpecialSearch::deserialise(const std::vector<uint8_t>& data)
{
    auto iterator = data.cbegin();

    deserialiseIntegralTypeContinuousContainer(iterator, functionName);
}

void NamedRelativeFunctionCallSpecialSearch::checkValid(const SpecialSearch& parent, const Search& parentParent) const
{
    if (parent.searchBytesRva + 5 >= parentParent.searchBytes.size())
        throw std::logic_error("Named relative function call special searches require at least 5 bytes after the RVA.");
}

bool NamedRelativeFunctionCallSpecialSearch::doSearch(const uint8_t* address) const
{
    class
    {
        public:
            uint8_t opCode; // 0xe8
            size_t functionRva;
    } instruction = *(decltype(instruction)*)address;
    if (instruction.opCode != 0xe8)
        return false;
    try
    {
        Module module;
        module.open(moduleName);
        if (address + sizeof(decltype(instruction)) + instruction.functionRva != module.getSymbol(functionName))
            return false;
    }
    catch (const std::exception& e)
    {
        return false;
    }
    return true;
}

std::vector<uint8_t> UnnamedRelativeFunctionCallSpecialSearch::serialise() const
{
    return Search::serialise();
}

void UnnamedRelativeFunctionCallSpecialSearch::deserialise(const std::vector<uint8_t>& data)
{
    Search::deserialise(data);
}

void UnnamedRelativeFunctionCallSpecialSearch::checkValid(const SpecialSearch& parent, const Search& parentParent) const
{
    if (parent.searchBytesRva + 5 >= parentParent.searchBytes.size())
        throw std::logic_error("Unnamed relative function call special searches require at least 5 bytes after the RVA.");
}

bool UnnamedRelativeFunctionCallSpecialSearch::doSearch(const uint8_t* address) const
{
    class
    {
        public:
            uint8_t opCode; // 0xe8
            size_t functionRva;
    } instruction = *(decltype(instruction)*)address;
    if (instruction.opCode != 0xe8)
        return false;

    // Create a data pointer special search to do the actual checking
    DataPointerSpecialSearch dataPointerSpecialSearch;
    dataPointerSpecialSearch.searchBytes = searchBytes;
    dataPointerSpecialSearch.ignoredSearchBytesRvas = ignoredSearchBytesRvas;
    dataPointerSpecialSearch.specialSearches = specialSearches;
    return dataPointerSpecialSearch.doSearch(address + sizeof(decltype(instruction)) + instruction.functionRva);
}

std::vector<uint8_t> NamedAbsoluteIndirectFunctionCallSpecialSearch::serialise() const
{
    std::vector<uint8_t> data;
    data.reserve(1024);

    serialiseIntegralTypeContinuousContainer(data, moduleName);
    serialiseIntegralTypeContinuousContainer(data, functionName);

    return data;
}

void NamedAbsoluteIndirectFunctionCallSpecialSearch::deserialise(const std::vector<uint8_t>& data)
{
    auto iterator = data.cbegin();

    deserialiseIntegralTypeContinuousContainer(iterator, moduleName);
    deserialiseIntegralTypeContinuousContainer(iterator, functionName);
}

void NamedAbsoluteIndirectFunctionCallSpecialSearch::checkValid(const SpecialSearch& parent, const Search& parentParent) const
{
    if (parent.searchBytesRva + 6 >= parentParent.searchBytes.size())
        throw std::logic_error("Named absolute indirect function call special searches require at least 6 bytes after the RVA.");
}

bool NamedAbsoluteIndirectFunctionCallSpecialSearch::doSearch(const uint8_t* address) const
{
    class
    {
        public:
            uint8_t opCode1; // 0xff
            uint8_t opCode2; // 0x15
            uint8_t** functionAddressAddress; // Points to an address which contains an address to the function
    } instruction = *(decltype(instruction)*)address;
    if (instruction.opCode1 != 0xff && instruction.opCode2 != 0x15)
        return false;
    try
    {
        // Get the address of the function
        Module module;
        module.open(moduleName);
        uint8_t* function = module.getSymbol(functionName);

        // Create a data pointer special search to do the actual checking
        DataPointerSpecialSearch dataPointerSpecialSearch;
        serialiseIntegralType(dataPointerSpecialSearch.searchBytes, function);
        return dataPointerSpecialSearch.doSearch((uint8_t*)instruction.functionAddressAddress);
    }
    catch (const std::exception& e)
    {
        return false;
    }
}

std::vector<uint8_t> UnnamedAbsoluteIndirectFunctionCallSpecialSearch::serialise() const
{
    return Search::serialise();
}

void UnnamedAbsoluteIndirectFunctionCallSpecialSearch::deserialise(const std::vector<uint8_t>& data)
{
    Search::deserialise(data);
}

void UnnamedAbsoluteIndirectFunctionCallSpecialSearch::checkValid(const SpecialSearch& parent, const Search& parentParent) const
{
    if (parent.searchBytesRva + 6 >= parentParent.searchBytes.size())
        throw std::logic_error("Unnamed absolute indirect function call special searches require at least 6 bytes after the RVA.");
}

bool UnnamedAbsoluteIndirectFunctionCallSpecialSearch::doSearch(const uint8_t* address) const
{
    class
    {
        public:
            uint8_t opCode1; // 0xff
            uint8_t opCode2; // 0x15
            uint8_t** functionAddressAddress; // Points to an address which contains an address to the function
    } instruction = *(decltype(instruction)*)address;
    if (instruction.opCode1 != 0xff && instruction.opCode2 != 0x15)
        return false;

    // Create a data pointer special search to do the actual checking
    DataPointerSpecialSearch dataPointerSpecialSearch;
    dataPointerSpecialSearch.searchBytes = { -1, -1, -1, -1 };
    dataPointerSpecialSearch.ignoredSearchBytesRvas = { 0, 1, 2, 3 };

    // We need to put a data pointer special search inside the data pointer special search to check if the function matches
    SpecialSearch specialSearch2;
    DataPointerSpecialSearch& dataPointerSpecialSearch2 = specialSearch2.setType<DataPointerSpecialSearch>();
    dataPointerSpecialSearch2.searchBytes = searchBytes;
    dataPointerSpecialSearch2.ignoredSearchBytesRvas = ignoredSearchBytesRvas;
    dataPointerSpecialSearch2.specialSearches = specialSearches;
    dataPointerSpecialSearch.specialSearches.push_back(specialSearch2);

    return dataPointerSpecialSearch.doSearch((uint8_t*)instruction.functionAddressAddress);
}

std::vector<uint8_t> DataPointerSpecialSearch::serialise() const
{
    return Search::serialise();
}

void DataPointerSpecialSearch::deserialise(const std::vector<uint8_t>& data)
{
    Search::deserialise(data);
}

void DataPointerSpecialSearch::checkValid(const SpecialSearch& parent, const Search& parentParent) const
{
    if (parent.searchBytesRva + 4 >= parentParent.searchBytes.size())
        throw std::logic_error("Data pointer special searches require at least 4 bytes after the RVA.");
}

bool DataPointerSpecialSearch::doSearch(const uint8_t* address) const
{
    return doSearch_(*(uint8_t**)address, searchBytes.size()).size() > 0;
}

}
