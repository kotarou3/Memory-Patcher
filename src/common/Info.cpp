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

#include <stdexcept>

#include "Info.h"

std::vector<uint8_t> ExtraSetting::serialise() const
{
    std::vector<uint8_t> data;
    data.reserve(1024);

    serialiseIntegralTypeContinuousContainer(data, label);
    serialiseIntegralType(data, type);
    serialiseIntegralTypeContinuousContainer(data, currentValue);
    serialiseIntegralTypeContinuousContainer(data, defaultValue);
    serialiseIntegralType(data, isNewlineAfterLabel);
    serialiseIntegralType(data, size);
    serialiseIntegralType(data, min);
    serialiseIntegralType(data, max);
    serialiseIntegralType(data, interval);
    serialiseIntegralType(data, precision);
    return data;
}

void ExtraSetting::deserialise(const std::vector<uint8_t>& data)
{
    auto iterator = data.cbegin();

    deserialiseIntegralTypeContinuousContainer(iterator, label);
    deserialiseIntegralType(iterator, type);
    deserialiseIntegralTypeContinuousContainer(iterator, currentValue);
    deserialiseIntegralTypeContinuousContainer(iterator, defaultValue);
    deserialiseIntegralType(iterator, isNewlineAfterLabel);
    deserialiseIntegralType(iterator, size);
    deserialiseIntegralType(iterator, min);
    deserialiseIntegralType(iterator, max);
    deserialiseIntegralType(iterator, interval);
    deserialiseIntegralType(iterator, precision);
}

ExtraSetting& getExtraSettingByLabel(ExtraSettings& extraSettings, const std::string label)
{
	for (auto& extraSetting : extraSettings)
		if (extraSetting.label == label)
			return extraSetting;
	throw std::logic_error("No setting with that label exists.");
}

std::vector<uint8_t> Info::serialise() const
{
    std::vector<uint8_t> data;
    data.reserve(1024);

    serialiseIntegralTypeContinuousContainer(data, name);
    serialiseIntegralTypeContinuousContainer(data, desc);
    serialiseIntegralType(data, isCurrentlyEnabled);
    serialiseIntegralType(data, isDefaultEnabled);
    serialiseSerialisableTypeContainer(data, extraSettings);
    return data;
}

void Info::deserialise(const std::vector<uint8_t>& data)
{
    auto iterator = data.cbegin();

    deserialiseIntegralTypeContinuousContainer(iterator, name);
    deserialiseIntegralTypeContinuousContainer(iterator, desc);
    deserialiseIntegralType(iterator, isCurrentlyEnabled);
    deserialiseIntegralType(iterator, isDefaultEnabled);
    deserialiseDeserialisableTypeContainer(iterator, extraSettings);
}
