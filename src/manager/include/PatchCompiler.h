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
#ifndef PATCHCOMPILER_H
#define PATCHCOMPILER_H

#include <string>

#include "Hook.h"
#include "Patch.h"
#include "Misc.h"

namespace PatchCompiler
{
    MANAGER_EXPORT std::string compileHook(const PatchData::Hook& hook, bool& isSkipped, bool force = false);
    MANAGER_EXPORT std::string compilePatchPack(const PatchData::PatchPack& patchPack, bool& isSkipped, bool force = false);
    MANAGER_EXPORT std::string linkObjects(bool force = false);
}

#endif
