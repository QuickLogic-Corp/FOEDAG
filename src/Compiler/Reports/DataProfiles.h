/*
Copyright 2022 The Foedag team

GPL License

Copyright (c) 2022 The Open-Source FPGA Foundation

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
#include <vector>
#include <map>
#include <string>

namespace FOEDAG {

template<typename T>
class DataProfiles {
public:
  std::shared_ptr<T> get(const std::string& key) {
    if (!contain(key)) {
      create(key);
    }
    return m_data[key];
  }

  const std::string& currentKey() const { return m_currentKey; }
  
  void setCurrentKey(const std::string& key) {
    if (!contain(key)) {
      create(key);
    }
    m_currentKey = key;
  }

  std::vector<std::string> keys() const {
    std::vector<std::string> result;
    for (auto it = m_data.begin(); it != m_data.end(); ++it) {
      std::string key = it->first;
      result.push_back(key);
    }
    return result;
  }

  std::shared_ptr<T> current() {
    return get(m_currentKey);
  }

  void clear() {
    m_data.clear();
    m_currentKey = "";
  }

private:
  std::string m_currentKey;
  std::map<std::string, std::shared_ptr<T>> m_data;

  bool contain(const std::string& key) {
    return m_data.find(key) != m_data.end();
  }

  void create(const std::string& key = "") {
    m_data[key] = std::make_shared<T>();
  }
};

}  // namespace FOEDAG
