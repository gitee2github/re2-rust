/******************************************************************************
 * Copyright (c) USTC(Suzhou) & Huawei Technologies Co., Ltd. 2022. All rights reserved.
 * re2-rust licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *     http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR
 * PURPOSE.
 * See the Mulan PSL v2 for more details.
 * Author: mengning<mengning@ustc.edu.cn>, liuzhitao<freekeeper@mail.ustc.edu.cn>, yangwentong<ywt0821@163.com>
 * Create: 2022-06-21
 * Description: Interface implementation in set.h.
 ******************************************************************************/



#include <iostream>
#include <stddef.h>
#include <algorithm>
#include <memory>
#include <utility>
#include <map>

#include "re2/testing/util/util.h"
#include "re2/testing/util/logging.h"
#include "re2/re2.h"
#include "re2/set.h"
#include "re2/stringpiece.h"
#include "regex_internal.h"
#include "regex-capi/include/regex_capi.h"

using namespace std;

namespace re2
{
  RE2::Set::Set(const RE2::Options &options, RE2::Anchor anchor)
      : options_(options),
        anchor_(anchor),
        compiled_(false),
        size_(0)
  {
    options_.set_never_capture(true); // might unblock some optimisations
  }

  RE2::Set::~Set()
  {
    size_ = 0;
    elem_.clear();
  }

  RE2::Set::Set(Set &&other)
      : options_(other.options_),
        anchor_(other.anchor_),
        compiled_(other.compiled_),
        prog_(std::move(other.prog_))
  {
    other.elem_.clear();
    other.elem_.shrink_to_fit();
    other.compiled_ = false;
    other.size_ = 0;
    other.prog_.reset();
  }

  RE2::Set &RE2::Set::operator=(Set &&other)
  {
    this->~Set();
    (void)new (this) Set(std::move(other));
    return *this;
  }

  int RE2::Set::Add(const StringPiece &pattern, std::string *error)
  {
    int place_num = size_;
    std::string rure_pattern = pattern.as_string();
    if (anchor_ == RE2::ANCHOR_START)
    { // 处理RE2::ANCHOR_START的情况
      rure_pattern.insert(0, "^");
    }
    else if (anchor_ == RE2::ANCHOR_BOTH)
    { // 处理RE2::ANCHOR_BOTH的情况
      rure_pattern.insert(0, "^");
      rure_pattern.append("$");
    }
    rure_error *err = rure_error_new();
    rure *re = rure_compile((const uint8_t *)rure_pattern.c_str(), strlen(rure_pattern.c_str()), RURE_DEFAULT_FLAGS, NULL, err);
    if (re == NULL)
    {
      const char *msg = rure_error_message(err);
      if (error != NULL)
      {
        error->assign(msg);
        LOG(ERROR) << "Regexp Error '" << pattern.data() << "':" << msg << "'";
      }
      // rure_free(re);
      return -1;
    }
    else
    {
      elem_.push_back(pair<std::string, re2::Regexp *>(rure_pattern, (re2::Regexp *)nullptr));
      size_++;
      // rure_free(re);
      return place_num;
    }
  }

  bool RE2::Set::Compile()
  {
    if (compiled_)
    {
      LOG(ERROR) << "RE2::Set::Compile() called more than once";
      return false;
    }
    compiled_ = true;
    const size_t PAT_COUNT = elem_.size();
    const char *patterns[PAT_COUNT];
    size_t patterns_lengths[PAT_COUNT];
    for (size_t i = 0; i < elem_.size(); i++)
    {
      patterns[i] = elem_[i].first.c_str();
      patterns_lengths[i] = elem_[i].first.length();
    }

    rure_error *err = rure_error_new();
    rure_set *re = rure_compile_set((const uint8_t **)patterns,
                                    patterns_lengths, PAT_COUNT, 0, NULL, err);
    if (re == NULL)
    {
      compiled_ = false;
      rure_set_free(re);
      return false;
    }
    prog_.reset((Prog *)re);
    compiled_ = true;
    return true;
  }

  bool RE2::Set::Match(const StringPiece &text, std::vector<int> *v) const
  {
    return Match(text, v, NULL);
  }

  bool RE2::Set::Match(const StringPiece &text, std::vector<int> *v,
                       ErrorInfo *error_info) const
  {
    if (!compiled_)
    {
      LOG(ERROR) << "RE2::Set::Match() called before compiling";
      if (error_info != NULL)
        error_info->kind = kNotCompiled;
      return false;
    }

    const char *pat_str = text.data();
    size_t length = strlen(pat_str);
    if (v == NULL)
    {
      bool result = rure_set_is_match((rure_set *)prog_.get(),
                                      (const uint8_t *)pat_str, length, 0);
      return result;
    }
    else
    {
      v->clear();
      bool matches[elem_.size()];
      bool result = rure_set_matches((rure_set *)prog_.get(),
                                     (const uint8_t *)pat_str, length, 0, matches);
      if (!result)
        return false;
      for (size_t i = 0; i < elem_.size(); i++)
      {
        if (matches[i])
          v->push_back(i);
      }
      return true;
    }
    return true;
  }
} // namespace re2
