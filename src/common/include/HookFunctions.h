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
#ifndef HOOKFUNCTIONS_H
#define HOOKFUNCTIONS_H

// Header included by all generated hooks and patches

#include <vector>

#include <stdint.h>

#include "Info.h"
#include <mutex>

// Function to push on to a stack
template <typename T>
    void push(const T& t, uint8_t*& stack)
{
    *(T*)stack = t;
    stack -= sizeof(T);
}

class Registers
{
    public:
        uint32_t eax;
        uint32_t ebx;
        uint32_t ecx;
        uint32_t edx;
        uint32_t esp;
        uint32_t ebp;
        uint32_t esi;
        uint32_t edi;
};

using hookPatchFunction_t = void (*)(const Registers&, const uint32_t, const ExtraSettings&, std::vector<void*>&);

#endif
