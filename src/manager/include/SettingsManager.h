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
#ifndef SETTINGSMANAGER_H
#define SETTINGSMANAGER_H

#include <string>
#include <map>

#include "Misc.h"

class MANAGER_EXPORT SettingsManager final
{
    public:
        std::string get(const std::string& name) const noexcept;
        void set(const std::string& name, const std::string& value);
        void setDefault(const std::string& name, const std::string& value);

        void save(const std::string& filename) const;
        void load(const std::string& filename, bool isSaveOnExit = true);

        static SettingsManager& getSingleton();

    private:
        SettingsManager();
        SettingsManager(const SettingsManager&) = delete;
        SettingsManager& operator=(const SettingsManager&) = delete;
        ~SettingsManager();

        class SettingsBranch
        {
            public:
                std::string value;
                std::map<std::string, SettingsBranch> children;
                std::map<std::string, SettingsBranch>::iterator parent;
        };

        std::map<std::string, SettingsBranch>::const_iterator get_(const std::string& name, std::map<std::string, SettingsBranch>::const_iterator rootBranch) const noexcept;
        std::map<std::string, SettingsBranch>::iterator get_(const std::string& name, std::map<std::string, SettingsBranch>::iterator rootBranch, bool isCreateIfNotExists = false);
        void clear_(const std::string& name, std::map<std::string, SettingsBranch>::iterator rootBranch);

        std::map<std::string, SettingsBranch> settingsTree_;

        std::string saveOnExitFilename_;
};

#endif
