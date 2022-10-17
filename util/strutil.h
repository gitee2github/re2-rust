// Copyright 2016 The RE2 Authors.  All Rights Reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include <string>

#include "re2/stringpiece.h"
#include "util/util.h"

namespace re2 {

std::string CEscape(const StringPiece& src);
void PrefixSuccessor(std::string* prefix);
std::string StringPrintf(const char* format, ...);

}  // namespace re2