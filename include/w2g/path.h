#ifndef W2G_PATH_H_
#define W2G_PATH_H_

#include <string>
#include <string_view>

namespace w2g {

// Lexical canonicalize for ABAC. Empty means reject (NUL, controls, or
// a relative ".." escape). Does not touch the filesystem.
std::string CanonicalPath(std::string_view in);

bool PrefixBound(std::string_view prefix, std::string_view val);

}  // namespace w2g

#endif  // W2G_PATH_H_
