#pragma once

#include <set>
#include <string>

namespace fp {

// Natural (human) sort comparator: compares runs of digits numerically so that
// e.g. x0[9] < x0[10] instead of the lexicographic x0[10] < x0[9].
struct NaturalLess {
  bool operator()(const std::string& a, const std::string& b) const {
    size_t i = 0, j = 0;
    while (i < a.size() && j < b.size()) {
      const bool aDigit = (a[i] >= '0' && a[i] <= '9');
      const bool bDigit = (b[j] >= '0' && b[j] <= '9');
      if (aDigit && bDigit) {
        // Compare numeric runs
        size_t ai = i, bi = j;
        while (ai < a.size() && a[ai] >= '0' && a[ai] <= '9') ++ai;
        while (bi < b.size() && b[bi] >= '0' && b[bi] <= '9') ++bi;
        const size_t aLen = ai - i, bLen = bi - j;
        if (aLen != bLen) return aLen < bLen; // fewer digits = smaller number
        const int cmp = a.compare(i, aLen, b, j, bLen);
        if (cmp != 0) return cmp < 0;
        i = ai; j = bi;
      } else {
        if (a[i] != b[j]) return a[i] < b[j];
        ++i; ++j;
      }
    }
    return a.size() < b.size();
  }
};

using NaturalStringSet = std::set<std::string, NaturalLess>;

}  // namespace fp
