
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
#ifndef MISC_H
#define MISC_H

// Only allow compiling on 32-bit x86 systems
#ifndef i386
    #error Only the x86 architechture is supported (Not including x86_64)
#endif

#include <vector>
#include <memory>

#include <cstdio>
#include <cstring>

#include <stdint.h>

// Export defines
// TODO: Clean up
#ifdef _WIN32
    #if defined (BUILD_MANAGER) || defined (BUILD_CORE) || defined (BUILD_COMMON)
        #define MANAGER_EXPORT __declspec(dllexport)
        #define CORE_EXPORT __declspec(dllexport)
        #define COMMON_EXPORT __declspec(dllexport)
    #else
        #define MANAGER_EXPORT __declspec(dllimport)
        #define CORE_EXPORT __declspec(dllimport)
        #define COMMON_EXPORT __declspec(dllimport)
    #endif
#else
    #if defined (BUILD_MANAGER) || defined (BUILD_CORE) || defined (BUILD_COMMON)
    	#define MANAGER_EXPORT __attribute__ ((visibility ("default")))
    	#define CORE_EXPORT __attribute__ ((visibility ("default")))
    	#define COMMON_EXPORT __attribute__ ((visibility ("default")))
	#else
	    #define MANAGER_EXPORT
	    #define CORE_EXPORT
	    #define COMMON_EXPORT
    #endif
#endif

// Define the TRACE macro for debugging use
#ifdef DEBUG
    #include <sstream>
    #include "Logger.h"
    #define TRACE(msg) { std::stringstream TRACESs; TRACESs << __FILE__ ":" << __LINE__ << ": " << msg; Logger::getSingleton().write(Logger::Severity::DEBUG_MESSAGE, TRACESs.str()); TRACESs.clear(); } void()
#else
    #define TRACE(msg)
#endif

// Templates to check if a type matches another type
template <typename A, typename B>
    class SameType
{ public:enum { result = 0 }; };
template <typename T>
    class SameType<T, T>
{ public:enum { result = 1 }; };

// Convert integral types to string
#ifdef _GLIBCXX_HAVE_BROKEN_VSWPRINTF
    #include <sstream>
#endif
template <typename T>
    inline std::string itos(const T t)
{
#ifdef _GLIBCXX_HAVE_BROKEN_VSWPRINTF
    std::stringstream ss;
    ss << t;
    return ss.str();
#else
    return std::to_string(t);
#endif
}
// Need a specialisation for character types so the character isn't just copied
template <> inline std::string itos<char>(const char t) { return itos((int16_t)t); }
template <> inline std::string itos<unsigned char>(const unsigned char t) { return itos((uint16_t)t); }

// Split a string in to tokens
std::vector<std::string> split(const std::string& string, const std::string& delims);

// Convert binary data to hex string
template <class C>
    inline std::string btos(const C& c)
{
    std::string result;
    result.reserve(2 * c.size() * sizeof(typename C::value_type) + 1);
    char buffer[3];
    for (uint8_t* b = (uint8_t*)&*c.begin(); b != (uint8_t*)&*c.end(); ++b)
    {
        std::sprintf(buffer, "%02x", *b);
        result += buffer;
    }
    return result;
}

// Bunch of functions for serialising data
template <typename T>
    inline void serialiseIntegralType(std::vector<uint8_t>& data, const T t)
{
    data.insert(data.end(), (const uint8_t*)&t, (const uint8_t*)&t + sizeof(T));
}
template <class C>
    inline void serialiseIntegralTypeContainer(std::vector<uint8_t>& data, const C& c)
{
    serialiseIntegralType(data, c.size() * sizeof(typename C::value_type)); // Size in bytes
    for (const auto& s : c)
        serialiseIntegralType(data, s);
}
template <class C>
    inline void serialiseIntegralTypeContinuousContainer(std::vector<uint8_t>& data, const C& c)
{
    serialiseIntegralType(data, c.size() * sizeof(typename C::value_type)); // Size in bytes
    data.insert(data.end(), (const uint8_t*)c.data(), (const uint8_t*)(c.data() + c.size()));
}
template <class C>
    inline void serialiseSerialisableTypeContainer(std::vector<uint8_t>& data, const C& c)
{
    serialiseIntegralType(data, c.size()); // Size in C::value_types
    for (const auto& s : c)
        serialiseIntegralTypeContinuousContainer(data, s.serialise());
}

// ...and for deserialising
template <typename T>
    inline T& deserialiseIntegralType(std::vector<uint8_t>::const_iterator& data, T& t)
{
    t = *(T*)&*data;
    data += sizeof(T);
    return t;
}
template <typename T>
    inline T deserialiseIntegralType(std::vector<uint8_t>::const_iterator& data)
{
    T t;
    return deserialiseIntegralType(data, t);
}
template <class C>
    inline C& deserialiseIntegralTypeContainer(std::vector<uint8_t>::const_iterator& data, C& c)
{
    c.clear();
    typename C::size_type size = deserialiseIntegralType<typename C::size_type>(data) / sizeof(typename C::value_type); // Size in C::value_type's
    for (typename C::size_type s = 0; s < size; ++s)
        c.insert(c.end(), deserialiseIntegralType<typename C::value_type>(data));
    return c;
}
template <class C>
    inline C deserialiseIntegralTypeContainer(std::vector<uint8_t>::const_iterator& data)
{
    C c;
    return deserialiseIntegralTypeContainer(data, c);
}
template <class C>
    inline C& deserialiseIntegralTypeContinuousContainer(std::vector<uint8_t>::const_iterator& data, C& c)
{
    typename C::size_type size = deserialiseIntegralType<typename C::size_type>(data) / sizeof(typename C::value_type); // Size in C::value_type's
    c.resize(size);
    std::memcpy(&c[0], &*data, size * sizeof(typename C::value_type));
    data += size * sizeof(typename C::value_type);
    return c;
}
template <class C>
    inline C deserialiseIntegralTypeContinuousContainer(std::vector<uint8_t>::const_iterator& data)
{
    C c;
    return deserialiseIntegralTypeContinuousContainer(data, c);
}
template <class C>
    inline C& deserialiseDeserialisableTypeContainer(std::vector<uint8_t>::const_iterator& data, C& c)
{
    c.clear();
    typename C::size_type size = deserialiseIntegralType<typename C::size_type>(data); // Size in C::value_type's
    for (typename C::size_type s = 0; s < size; ++s)
    {
        typename C::value_type t;
        t.deserialise(deserialiseIntegralTypeContinuousContainer<std::vector<uint8_t>>(data));
        c.insert(c.end(), t);
    }
    return c;
}
template <class C>
    inline C deserialiseDeserialisableTypeContainer(std::vector<uint8_t>::const_iterator& data)
{
    C c;
    return deserialiseDeserialisableTypeContainer(data, c);
}

// Convert error codes to strings
#ifdef _WIN32
COMMON_EXPORT std::string strErrorWin32(int error);
#endif
COMMON_EXPORT std::string strError(int error);

// Get inode and device id of a file. Returns false if file doesn't exist, otherwise true
COMMON_EXPORT bool getInodeAndDeviceId(const std::string& pathfile, uint64_t& inode, uint64_t& deviceId) noexcept;

// Calculate the CRC-32 checksum of an array of bytes.
// Warning: Don't compare the checksum generated with a checksum generated by another
// CRC-32 algorithm because they aren't compatible!
COMMON_EXPORT uint32_t calculateCrc32Checksum(const std::vector<uint8_t>& data) noexcept;

#endif
