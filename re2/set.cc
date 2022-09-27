// Copyright 2010 The RE2 Authors.  All Rights Reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "re2/set.h"

#include <iostream>
#include <stddef.h>
#include <algorithm>
#include <memory>
#include <utility>
#include <map>

#include "util/util.h"
#include "util/logging.h"
#include "re2/re2.h"
#include "regex_internal.h"
#include "re2/stringpiece.h"
extern "C"
{
  #include "regex-capi/include/rure.h"
}
using namespace std;

namespace re2
{
  static int regex_counter = 0;
  static std::map<int, const char *> regex_map;

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
    // std::cout << "析构函数调用\n";
    regex_counter = 0;
    if(regex_map.size() != 0) regex_map.clear();
    
  }

  RE2::Set::Set(Set && other)
      : options_(other.options_),
        anchor_(other.anchor_),
        compiled_(other.compiled_),
        prog_(std::move(other.prog_))
  {
    other.compiled_ = false;
    other.prog_.reset();
    regex_map.clear();
    regex_counter = 0;

  }

  RE2::Set &RE2::Set::operator=(Set &&other)
  {
    this->~Set();
    (void)new (this) Set(std::move(other));
    return *this;
  }

  
  int RE2::Set::Add(const StringPiece &pattern, std::string *error)
  {
    int place_num = regex_counter;
    const char *rure_str = pattern.data();
    rure_error *err = rure_error_new();
    rure *re = rure_compile((const uint8_t *)rure_str, strlen(rure_str), RURE_DEFAULT_FLAGS, NULL, err);
    
    if (re == NULL)
    {
      const char *msg = rure_error_message(err);
      if(error)
      {
        error->assign(msg);
        LOG(ERROR) << "Error Compile '" << pattern.data() << "':" << msg << "'";
      }
      // rure_free(re);
      // rure_error_free(err);
      return -1;
    }
    else
    {
      regex_map.insert(pair<int, const char *>(regex_counter++, rure_str));
      // rure_free(re);
      return place_num;
    }
  }

  bool RE2::Set::Compile()
  {
    if (compiled_) {
      LOG(DFATAL) << "RE2::Set::Compile() called more than once";
      return false;
    }
    compiled_ = true;
    const size_t PAT_COUNT = regex_map.size();
    const char *patterns[PAT_COUNT];
    size_t patterns_lengths[PAT_COUNT];
    int i = 0;
    for (auto it : regex_map) {
      patterns[i] = it.second;
      patterns_lengths[i++] = strlen(it.second);
    }

    rure_error *err = rure_error_new();
    rure_set *re = rure_compile_set((const uint8_t **) patterns, 
                                      patterns_lengths, PAT_COUNT, 0, NULL, err);
    
    if(re == NULL){
      const char *msg = rure_error_message(err);
      std::cout << msg << std::endl;
      compiled_ = false;
      return false;
    } 
    prog_.reset((Prog *)re);
    // rure_set_free(re);
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
    // RE2::UNANCHORED     可以直接处理
    // RE2::ANCHOR_BOTH   思路：可以使用上种处理方式进行处理，之后判断是否是完全匹配！

    // 处理完成上面之后，再对vector进行赋值
    // 1. v == NULL
    // 2. v != NULL

    if (!compiled_) {
      LOG(DFATAL) << "RE2::Set::Match() called before compiling";
      if (error_info != NULL)
        error_info->kind = kNotCompiled;
      return false;
    }
    
    const char *pat_str = text.data();
    size_t length = strlen(pat_str);
    switch (anchor_)
    {
      case RE2::UNANCHORED:
      {
        if(v == NULL)
        {
          
          bool result = rure_set_is_match((rure_set *)prog_.get(), 
                                            (const uint8_t *)pat_str, length, 0);
          return result;
        }
        else
        { 
          v->clear();
          bool matches[regex_map.size()];
          bool result = rure_set_matches((rure_set *)prog_.get(), 
                                            (const uint8_t *)pat_str, length, 0, matches);
          if(!result) return false;
          for(size_t i = 0; i < regex_map.size(); i++)
          {
            if(matches[i]) v->push_back(i);
          }
          return true;
        }
        break;
      }

      case RE2::ANCHOR_BOTH:
      {
        if(v == NULL)
        {
          bool matches[regex_map.size()];
          bool result = rure_set_matches((rure_set *)prog_.get(), 
                                            (const uint8_t *)pat_str, length, 0, matches);
          if(!result) return false;
          for(size_t i = 0; i < regex_map.size(); i++)
          {
            if(matches[i])
            {
              const char *pattern = regex_map[i];
              rure *re = rure_compile_must(pattern);
              rure_match match = {0};
              rure_find(re, (const uint8_t *)pat_str, strlen(pat_str),
                             0, &match);
              if(match.start == 0 && match.end == strlen(pat_str)) return true;
            }
            
          }
          return false;
        }
        else
        { 
          v->clear();
          bool matches[regex_map.size()];
          bool result = rure_set_matches((rure_set *)prog_.get(), 
                                            (const uint8_t *)pat_str, length, 0, matches);
          if(!result) return false;
          for(size_t i = 0; i < regex_map.size(); i++)
          {
            if(matches[i])
            {
              const char *pattern = regex_map[i];
              rure *re = rure_compile_must(pattern);
              rure_match match = {0};
              rure_find(re, (const uint8_t *)pat_str, strlen(pat_str),
                             0, &match);
              if(match.start == 0 && match.end == strlen(pat_str)) v->push_back(i);
            }  
          }
          if(v->size()) return true;
          else return false;
        }
        break;
      }
      case RE2::ANCHOR_START:
      {
        if(v == NULL)
        {
          
          bool result = rure_set_is_match((rure_set *)prog_.get(), 
                                            (const uint8_t *)pat_str, length, 0);
          return result;
        }
        else
        { 
          v->clear();
          bool matches[regex_map.size()];
          bool result = rure_set_matches((rure_set *)prog_.get(), 
                                            (const uint8_t *)pat_str, length, 0, matches);
          if(!result) return false;
          for(size_t i = 0; i < regex_map.size(); i++)
          {
            if(matches[i]) v->push_back(i);
          }
          return true;
        }
        break;
      }
    }
    return true;
  }

} // namespace re2
