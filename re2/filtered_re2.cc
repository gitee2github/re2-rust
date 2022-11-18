// Copyright 2009 The RE2 Authors.  All Rights Reserved.
// Use of this source code is governed by a BSD-stylePrefilter
// license that can be found in the LICENSE file.
#include <iostream>
#include <map>
#include <string.h>
#include "re2/filtered_re2.h"
#include <stddef.h>
#include <string>
#include <utility>

#include "util/util.h"
#include "util/logging.h"
// #include "re2/prefilter.h"
extern "C"
{
#include "regex-capi/include/rure.h"
}
using namespace std;
namespace re2 {

std::map<std::string, std::vector<std::string>> map_atoms;

// #include "re2/prefilter_tree.h"
  class PrefilterTree {
   public:
    PrefilterTree():min_atom_len_(3){};
    explicit PrefilterTree(int min_atom_len):min_atom_len_(min_atom_len){};
    ~PrefilterTree(){};
    int getMinAtomLen(){
      return min_atom_len_;
    }
    bool get_is_latin_result() {return is_latin;};
    void set_latin(bool x);
    std::string get_latin_string() {return str_latin;};
    void set_latin_str(std::string x);
    
   private:
    const int min_atom_len_;
    bool is_latin;
    std::string str_latin;
  };
  void PrefilterTree::set_latin(bool x) {
    is_latin = x;
  }
  void PrefilterTree::set_latin_str(std::string x) {
    str_latin = x;
  }
};

namespace re2 {

FilteredRE2::FilteredRE2()
    : compiled_(false),
      prefilter_tree_(new PrefilterTree()) {
}

FilteredRE2::FilteredRE2(int min_atom_len)
    : compiled_(false),
      prefilter_tree_(new PrefilterTree(min_atom_len)) {
}

FilteredRE2::~FilteredRE2() {
  for (size_t i = 0; i < re2_vec_.size(); i++)
    delete re2_vec_[i];
}

FilteredRE2::FilteredRE2(FilteredRE2&& other)
    : re2_vec_(std::move(other.re2_vec_)),
      compiled_(other.compiled_),
      prefilter_tree_(std::move(other.prefilter_tree_)) {
  other.re2_vec_.clear();
  other.re2_vec_.shrink_to_fit();
  other.compiled_ = false;
  other.prefilter_tree_.reset(new PrefilterTree());
}

FilteredRE2& FilteredRE2::operator=(FilteredRE2&& other) {
  this->~FilteredRE2();
  (void) new (this) FilteredRE2(std::move(other));
  return *this;
}

RE2::ErrorCode FilteredRE2::Add(const StringPiece& pattern,
                                const RE2::Options& options, int* id) {
  RE2* re = new RE2(pattern, options);
  RE2::ErrorCode code = re->error_code();
  if(options.encoding() == RE2::Options::EncodingLatin1) {
    prefilter_tree_->set_latin(true);
    prefilter_tree_->set_latin_str(pattern.as_string());
  }
  else prefilter_tree_->set_latin(false);
  
  if (!re->ok()) {
    if (options.log_errors()) {
      LOG(ERROR) << "Couldn't compile regular expression, skipping: "
                 << pattern << " due to error " << re->error();
    }
    delete re;
  } else {
    *id = static_cast<int>(re2_vec_.size());
    re2_vec_.push_back(re);
  }

  return code;
}

int my_split(const std::string& src, const char& delim,
		std::vector<std::string>& vec)
{
	int src_len = src.length();
	int find_cursor = 0;
	int read_cursor = 0;
	if (src_len <= 0) return -1;
	vec.clear();
	while (read_cursor < src_len){
		find_cursor = src.find(delim, find_cursor);
		//1.找不到分隔符
		if (-1 == find_cursor){
			if (read_cursor <= 0) return -1;

			//最后一个子串, src结尾没有分隔符
			if (read_cursor < src_len){
				vec.push_back(src.substr(read_cursor, src_len - read_cursor));
				return 0;
			}
		}
		//2.有连续分隔符的情况
		else if (find_cursor == read_cursor){
			//字符串开头为分隔符, 也按空子串处理, 如不需要可加上判断&&(read_cursor!=0)
			vec.push_back(std::string(""));
		}
		//3.找到分隔符
		else
			vec.push_back(src.substr(read_cursor, find_cursor - read_cursor));

		read_cursor = ++find_cursor;
		if (read_cursor == src_len){
			//字符串以分隔符结尾, 如不需要末尾空子串, 直接return
			vec.push_back(std::string(""));
			return 0;
		} 
	}//end while()
  return 0;
}


void FilteredRE2::Compile(std::vector<std::string>* atoms) {
  map_atoms.clear();
  if (compiled_) {
    LOG(ERROR) << "Compile called already.";
    return;
  }

  if (re2_vec_.empty()) {
    LOG(ERROR) << "Compile called before Add.";
    return;
  }
  atoms->clear();

  // 处理latin的情况
  if(prefilter_tree_->get_is_latin_result()) {
    std::string str = prefilter_tree_->get_latin_string();
    std::vector<std::string> vec;
    vec.push_back(str);
    std::string str_low = str;
    transform(str_low.begin(),str_low.end(),str_low.begin(),::tolower);
    atoms->push_back(str_low);
    map_atoms.insert(map<std::string, std::vector<std::string>>::value_type(str, vec));
    map_atoms.insert(map<std::string, std::vector<std::string>>::value_type("total", vec));
    compiled_ = true;
    return;
  }
  
  for(size_t i = 0; i < re2_vec_.size(); i++)
  {
    // std::vector<std::string> my_atoms = MyCompile(re2_vec_[i]->pattern(), prefilter_tree_->getMinAtomLen());
    const char *regex = re2_vec_[i]->pattern().c_str();
    std::string regex_str = regex;
    const char *result = rure_filter_compile((const uint8_t *)regex, strlen(regex), prefilter_tree_->getMinAtomLen());
    std::string atoms_str = result;
    std::vector<std::string> vec;
    int iRet = my_split(atoms_str, '|', vec);
    if(iRet == 0) {
      map_atoms.insert(map<std::string, std::vector<std::string>>::value_type(regex_str, vec));
      for(auto it : vec) {
        atoms->push_back(it);
      }
    } else {
      if(atoms_str.size() != 0) {
        std::vector<std::string> v;
        v.push_back(atoms_str);
        map_atoms.insert(map<std::string, std::vector<std::string>>::value_type(regex_str, v));
        atoms->push_back(atoms_str);

      }
    }
    
  }
  map_atoms.insert(map<std::string, std::vector<std::string>>::value_type("total", *atoms));
  compiled_ = true;
}

int FilteredRE2::SlowFirstMatch(const StringPiece& text) const {
  for (size_t i = 0; i < re2_vec_.size(); i++)
  {
    if (RE2::PartialMatch(text, re2_vec_[i]->pattern())){
      return static_cast<int>(i);
    } 
  }
  return -1;
}

void AtomsToRegexps(std::vector<RE2*> re2_vec_, std::vector<int> atoms, std::vector<int> *regexps, int min_atom_len)
{
  // 根据atoms索引获取regexp索引的规则
  /*
   * 如果没有原子, 那么直接会把re加进去。
   * 如果这个正则表达式有原子，那么要把该正则表达式的所有的原子的索引全加入，这个正则表达式才能加入成功。
   */
  
  std::vector<std::string> atoms_total = map_atoms["total"];
  std::vector<std::string> atoms_tmp;
  // 获取所有的atoms
  // for(size_t i = 0; i < re2_vec_.size(); i++)
  // {
  //   std::vector<std::string> my_atoms = MyCompile(re2_vec_[i]->pattern(), min_atom_len);

  //   if(my_atoms.size() != 0)
  //   {
  //     for(auto x : my_atoms)
  //       atoms_total.push_back(x);
  //   }

  // }
  // map_atoms["total"].;
  // atoms_total.assign(map_atoms["total"].)
  for(size_t i = 0; i < atoms.size(); i++)
  {
    atoms_tmp.push_back(atoms_total[atoms[i]]);
  }
  for(size_t i = 0; i < re2_vec_.size(); i++)
  {
    // std::vector<std::string> my_atoms = MyCompile(re2_vec_[i]->pattern(), min_atom_len);
    std::string str = re2_vec_[i]->pattern();
    std::vector<std::string> my_atoms = map_atoms[str];
    if(my_atoms.size() == 0){
      regexps->push_back(i);
      continue;
    }
    else
    {
      int count = 0;
      for(size_t ii = 0; ii < my_atoms.size(); ii++)
      {
        for(size_t jj = 0; jj < atoms_tmp.size(); jj++)
        {
          if(my_atoms[ii] == atoms_tmp[jj]){
            count++;
            break;
          }
        }
      }
      if(count == (int)my_atoms.size()) regexps->push_back(int(i));
    }
  }
}

int FilteredRE2::FirstMatch(const StringPiece& text,
                            const std::vector<int>& atoms) const {
  if (!compiled_) {
    LOG(DFATAL) << "FirstMatch called before Compile.";
    return -1;
  }
  std::vector<int> regexps;
 
  AtomsToRegexps(re2_vec_, atoms, &regexps, prefilter_tree_->getMinAtomLen());
  
  for (size_t i = 0; i < regexps.size(); i++)
    if (RE2::PartialMatch(text, *re2_vec_[regexps[i]]))
      return static_cast<int>(i);
  return -1;
}

bool FilteredRE2::AllMatches(
    const StringPiece& text,
    const std::vector<int>& atoms,
    std::vector<int>* matching_regexps) const {
  matching_regexps->clear();

  std::vector<int> regexps;
  AtomsToRegexps(re2_vec_, atoms, &regexps, prefilter_tree_->getMinAtomLen());

  for (size_t i = 0; i < re2_vec_.size(); i++)
    if (RE2::PartialMatch(text, *re2_vec_[i]))
      matching_regexps->push_back(i);
  return !matching_regexps->empty();
  
}

void FilteredRE2::AllPotentials(
    const std::vector<int>& atoms,
    std::vector<int>* potential_regexps) const {
  AtomsToRegexps(re2_vec_, atoms, potential_regexps, prefilter_tree_->getMinAtomLen());
}



}  // namespace re2
