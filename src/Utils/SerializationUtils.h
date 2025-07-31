/*
Copyright 2021 The Foedag team

GPL License

Copyright (c) 2021 The Open-Source FPGA Foundation

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#pragma once

#include <memory>
#include <nlohmann_json/json.hpp>

// ---------------------------------------------------------------------
// Forward‑declare every domain type you will ever store in a shared_ptr
// *before* the template, so ADL knows a to_json / from_json will exist.
// ---------------------------------------------------------------------
namespace FOEDAG { 
    struct CommandWrapper; 
} // namespace FOEDAG

// ---------------------------------------------------------------------
// Generic specialisation for std::shared_ptr<T>
// ---------------------------------------------------------------------
namespace nlohmann
{

template <typename T>
struct adl_serializer<std::shared_ptr<T>> {
    static void to_json(json& j, const std::shared_ptr<T>& ptr) {
        if (ptr) {
            nlohmann::json tmp;
            adl_serializer<T>::to_json(tmp, *ptr);
            j = std::move(tmp);
        } else {
            j = nullptr; 
            return; 
        }
    }

    static void from_json(const json& j, std::shared_ptr<T>& ptr) {
        if (!j.is_null()) { 
            T obj;
            adl_serializer<T>::from_json(j, obj);
            ptr = std::make_shared<T>(std::move(obj));
        } else {
            ptr.reset(); 
        }
    }
};

} // namespace nlohmann




