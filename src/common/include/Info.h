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
#ifndef INFO_H
#define INFO_H

#include <string>
#include <vector>

#include <stdint.h>

#include "Misc.h"

class COMMON_EXPORT ExtraSetting final
{
    public:
        std::vector<uint8_t> serialise() const;
        void deserialise(const std::vector<uint8_t>& data);

        std::string label;
        enum class Type { TEXT, NUMBER, SLIDER, CHECKBOX } type;
        std::string currentValue; // Ignored for input
        std::string defaultValue;
        bool isNewlineAfterLabel;
        size_t size;

        // For NUMBER and SLIDER types only
        int64_t min;
        int64_t max;
        uint64_t interval;
        uint8_t precision;
};
using ExtraSettings = std::vector<ExtraSetting>;
COMMON_EXPORT ExtraSetting& getExtraSettingByLabel(ExtraSettings& extraSettings, const std::string label);

class COMMON_EXPORT Info final
{
    public:
        std::vector<uint8_t> serialise() const;
        void deserialise(const std::vector<uint8_t>& data);

        std::string name;
        std::string desc;
        bool isCurrentlyEnabled; // Ignored for input
        bool isDefaultEnabled;
        ExtraSettings extraSettings;
};

#endif // INFO_H
