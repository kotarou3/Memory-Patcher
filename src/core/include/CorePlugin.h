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
#ifndef COREPLUGIN_H
#define COREPLUGIN_H

#include <memory>

#include "PluginLoader.h"
#include "Info.h"
#include "Misc.h"

class CorePlugin
{
    protected:
        virtual void onEnable() {};
        virtual void onDisable() {};
        virtual void onSettingChange(const Info& /*info*/) {};

        friend class PluginLoader;
};

#if defined (BUILD_CORE)
    using createInstance_t = std::unique_ptr<CorePlugin> (*)();
#else
    extern "C" __attribute__ ((visibility ("default"))) std::unique_ptr<CorePlugin> createInstance();
#endif

#endif
