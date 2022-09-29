// Copyright 2009 The RE2 Authors.  All Rights Reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.
#include <iostream>
#include <string.h>
#include "re2/filtered_re2.h"
#include <stddef.h>
#include <string>
#include <utility>

#include "util/util.h"
#include "util/logging.h"
// #include "re2/prefilter.h"
namespace re2 {
class Prefilter {};
// #include "re2/prefilter_tree.h"
  class PrefilterTree {
   public:
    PrefilterTree():min_atom_len_(3){};
    explicit PrefilterTree(int min_atom_len):min_atom_len_(min_atom_len){};
    ~PrefilterTree(){};
    int getMinAtomLen(){
      return min_atom_len_;
    }
   private:
    const int min_atom_len_;
  };
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

/**
 * 负责对字符集进行连接操作
 *
 */
std::vector<std::string> Connection(std::string str, std::vector<std::string> vec1, std::vector<char> vec2)
{
  std::vector<std::string> vec_tmp;
  if (str.size() > 0)
  {
    for (size_t i = 0; i < vec2.size(); i++)
    {
      vec_tmp.push_back(str + vec2[i]);
    }
  }
  else if (vec1.size() == 0)
  {
    for (auto x : vec2)
    {
      std::string str;
      str.push_back(x);
      vec_tmp.push_back(str);
    }
  }
  else
  {
    for (size_t i = 0; i < vec1.size(); i++)
    {
      for (size_t j = 0; j < vec2.size(); j++)
      {

        vec_tmp.push_back(vec1[i] + vec2[j]);
      }
    }
  }
  vec1.clear();
  return vec_tmp;
}

/**
 * 处理
 * a[a-c]a[zv]
 * [abc]
 * [a-c]+
 */
std::vector<char> CharClassExpansion(std::string str, int start_post, int end_post)
{
  std::vector<char> atoms; // 字符集中的atoms
  std::vector<char> vec_tmp;
  // std::vector<char> vec_op;
  std::vector<char> vec_char;
  std::string str_tmp;

  vec_tmp.clear();
  // vec_op.clear();
  vec_char.clear();
  int flag_connect = 0;
  for (int i = start_post; i <= end_post; i++)
  {
    if (str[i] == '[' || str[i] == ']')
    {
      continue;
    }
    else if ((str[i] >= 'a' && str[i] <= 'z') || (str[i] >= 0 && str[i] <= 9))
    {
      vec_char.push_back(str[i]);
    }
    else if (str[i] == '-')
    {
      flag_connect = 1;
      continue;
    }
  }

  // 将字符集拆分的所有可能字符存储到atoms中
  if (flag_connect == 1)
  {
    char x2 = vec_char[1];
    char x1 = vec_char[0];
    for (int i = int(x1); i <= int(x2); i++)
    {
      atoms.push_back(char(i));
    }
  }
  else if (flag_connect == 0)
  {
    for (auto x : vec_char)
    {
      atoms.push_back(x);
    }
  }
  return atoms;
}

/**
 *  (abc123|abc|ghi789|abc1234)
    3-abc
    6-abc123
    6-ghi789
    7-abc1234
 * abc  abc123  ghi789  abc1234
 */
std::vector<std::string> Group_multiple_selection(std::string str, int start_point, 
                                                  int end_point, size_t min_atoms_len)
{
  std::string str_tmp; // 暂存atoms
  std::multimap<int, std::string> atoms_tmp;
  std::vector<std::string> vec_atoms_one;  // 第一次暂存所有的atoms
  std::vector<std::string> vec_atoms_two;  // 第二次暂存所有的atoms
  std::vector<std::string> vec_atoms_real; // 符合规则的atoms
  // 先获取所有的atoms
  for (int i = start_point; i <= end_point; i++)
  {
    if (str[i] == '(')
      continue;
    if (str[i] == '|' || str[i] == ')')
    {
      if (str_tmp.size() >= min_atoms_len)
      {
        atoms_tmp.insert(make_pair(str_tmp.size(), str_tmp));
      }
      str_tmp.clear();
      continue;
    }
    else
    {
      str_tmp += str[i];
    }
  }
  // 去除所有所有比最短atoms长的元素，并且最短atoms是他们的子集
  for (auto it = atoms_tmp.begin(); it != atoms_tmp.end(); it++)
  {
    vec_atoms_one.push_back(it->second);
    vec_atoms_two.push_back(it->second);
  }
  for (size_t i = 0; i < vec_atoms_one.size(); i++)
  {
    for (size_t j = 0; j < vec_atoms_two.size(); j++)
    {
      if(vec_atoms_two[j].find(vec_atoms_one[i]) != std::string::npos && vec_atoms_one[i] != vec_atoms_two[j]
                                    && vec_atoms_one[i].size() != 0 && vec_atoms_two[j].size() != 0)
      { // 如果包含，则置为空
        vec_atoms_two[j] = "-1";
      }
    }
  }
  // 重新赋值给 vec_atoms_real
  for (size_t i = 0; i < vec_atoms_two.size(); i++)
  {
    if (vec_atoms_two[i] != "-1")
    {
      vec_atoms_real.push_back(vec_atoms_two[i]);
    }
  }
  return vec_atoms_real;
}

/**
 * 把所有的大写字母转换为小写字母
 * 1. 标准ASCII
 * 2. 非标准ASCII  如希腊字母
 */

void UpperToLower(std::string &str, int start_post, int end_post)
{
  // 标准ASCII转小写
  transform(str.begin(), str.end(), str.begin(), ::tolower);
}

void HandleCharacterCase(std::string &str)
{
  std::map<std::string, std::string> m = {{"\u0391", "\u03B1"}, {"\u0392", "\u03B2"}, {"\u0393", "\u03B3"},
                                            {"\u0394", "\u03B4"}, {"\u0395", "\u03B5"}, {"\u0396", "\u03B6"},
                                            {"\u0397", "\u03B7"}, {"\u0398", "\u03B8"}, {"\u0399", "\u03B9"},
                                            {"\u039A", "\u03BA"}, {"\u039B", "\u03BB"}, {"\u039C", "\u03BC"},
                                            {"\u039D", "\u03BD"}, {"\u039E", "\u03BE"}, {"\u039F", "\u03BF"}, 
                                            {"\u03A0", "\u03C0"}, {"\u03A1", "\u03C1"}, {"\u03A2", "\u03C2"},
                                            {"\u03A3", "\u03C3"}, {"\u03A4", "\u03C4"}, {"\u03A5", "\u03C5"},
                                            {"\u03A6", "\u03C6"}, {"\u03A7", "\u03C7"}, {"\u03A8", "\u03C8"},
                                            {"\u03A9", "\u03C9"}};
  for(size_t i = 0; i < str.length(); i += 2)
  {
    std::string subStr = str.substr(i, 2);
    if(m.count(subStr) > 0)
    {
      str.replace(i, 2, m[subStr]);
      continue;
    }
    else if(subStr == "ϖ")
    {
      str.replace(i, 2, "π");
      continue;
    }
    else if(subStr == "ς")
    {
      str.replace(i, 2, "σ");
      continue;
    }
  }
}


bool JudgeIsCharOrNumber(char x)
{
  if ((x >= 'a' && x <= 'z') || (x >= 0 && x <= 9))
    return true;
  return false;
}

bool JudgeMinux(char x)
{
  if(x == '-') return true;
  return false;
}

bool JudgeRBrace(char x)
{
  if(x == '}') return true;
  return false;
}
bool JudgeLBrace(char x)
{
  if(x == '{') return true;
  return false;
}

bool JudedIsGreekAlphabet(std::string str)
{
  std::vector<std::string> vec_alphabet = {"\u03B1", "\u03B2", "\u03B3", "\u03B4", "\u03B5", 
                                            "\u03B6", "\u03B7", "\u03B8", "\u03B9", "\u03BA", 
                                            "\u03BB", "\u03BC", "\u03BD", "\u03BE", "\u03BF",
                                            "\u03C0", "\u03C1", "\u03C2", "\u03C3", "\u03C4",
                                            "\u03C5", "\u03C6", "\u03C7", "\u03C8", "\u03C9"};
  for(auto x : vec_alphabet)
  {
    if(x == str) return true;
  }
  return false;
}

std::vector<std::string> MyCompile(std::string str, size_t min_atoms_len)
{
  std::vector<std::string> my_atoms;      // 最终得到的所有atoms
  std::vector<std::string> vec_atoms_tmp; // 暂存的atom
  std::vector<std::string> vec_con;
  std::vector<char> atoms_tmp;
  std::string atoms_tmp_string;
  std::string subStr;
  // 将字符串中的大写字符变为小写
  UpperToLower(str, 0, str.size());
  HandleCharacterCase(str);
  for (size_t i = 0; i < str.length(); i++)
  {
    // 处理希腊字母
    subStr.clear();
    subStr = str.substr(i, 2);
    if(JudedIsGreekAlphabet(subStr))
    {
      ++i;
      atoms_tmp_string += subStr;
      continue;
    }
    if(JudgeLBrace(str[i]))
    {
      do ++i; while(!JudgeRBrace(str[i]));
      ++i;
    }
    // 处理括号分组
    if (str[i] == '(')
    {
      int group_start_post = i;
      do
        ++i;
      while (str[i] != ')');
      int group_end_post = i;

      std::vector<std::string> vec = Group_multiple_selection(str, group_start_post, group_end_post, min_atoms_len);
      int tmp_post_group = i;
      ++tmp_post_group;
      if (str[tmp_post_group] == '.' && vec.size() != 0)
      {
        ++i;
        for (auto x : vec) my_atoms.push_back(x);
      }
      else if(str[tmp_post_group] == '{' && vec.size() != 0)
      {
        for (auto x : vec) my_atoms.push_back(x);

      }
      // "(abc123|def456|ghi789).*mnop[x-z]+"
      continue;
    }
    if (JudgeIsCharOrNumber(str[i]) || JudgeMinux(str[i]) || JudgeRBrace(str[i]))
    {
      atoms_tmp_string += str[i];
      continue;
    }
    // 处理
    if (str[i] == '\\')
    {
      if (atoms_tmp_string.size() > 0)
      {
        my_atoms.push_back(atoms_tmp_string);
        atoms_tmp_string.clear();
      }

      int escape_char_post = i;
      if (JudgeIsCharOrNumber(++escape_char_post))
        ++i;
      int escape_plus_post = i;
      if (str[++escape_plus_post] == '+')
        ++i;
      continue;
    }
    if (str[i] == '*')
    {
      continue;
    }
    if (str[i] == '.')
    {
      if (atoms_tmp_string.size() >= min_atoms_len && vec_atoms_tmp.size() == 0)
      {
        my_atoms.push_back(atoms_tmp_string);
        atoms_tmp_string.clear();
        continue;
      }
      else if (atoms_tmp_string.size() > 0 && vec_atoms_tmp.size() != 0)
      {
        for (auto x : vec_atoms_tmp)
        {
          my_atoms.push_back(x + atoms_tmp_string);
        }
        atoms_tmp_string.clear();
        vec_atoms_tmp.clear();
      }
      else
      {
        continue;
      }
    }
    // 处理多个字符集
    // a[a-b]a
    // a[a-b][a-b]
    if(str[i] == '[')
    {
      int start_post = i;
      do ++i; while(str[i] != ']');
      int plus_tmp = i;
      // 看是否有 + 号
      if(str[++plus_tmp] == '+')
      {
        
        if(atoms_tmp_string.size() > 2)
        {
          my_atoms.push_back(atoms_tmp_string);
          atoms_tmp_string.clear();
        }
      }
      else
      {
        // 如果[x-y]aaab[ab]形式
        if(atoms_tmp_string.size() > 0 && vec_con.size() >0)
        {
          vec_atoms_tmp.clear();
          for(auto x : vec_con)
          {
            vec_atoms_tmp.push_back(x + atoms_tmp_string);
          }
          atoms_tmp_string.clear();
        }
        int end_post = i;
        atoms_tmp.clear();
        atoms_tmp = CharClassExpansion(str, start_post, end_post);
        vec_con.clear();
        vec_con = Connection(atoms_tmp_string, vec_atoms_tmp, atoms_tmp);
        atoms_tmp_string.clear();
        vec_atoms_tmp.clear();
        for(size_t i = 0; i < vec_con.size(); i++)
        {
          vec_atoms_tmp.push_back(vec_con[i]);
        }

      }
    }
    if(int(str[i]) < 0)
    {
      atoms_tmp_string += str[i];
      continue;
    }
  }
  if(vec_atoms_tmp.size() > 0)
  {
    for(auto x : vec_atoms_tmp)
    {
      my_atoms.push_back(x);
    }
  }
  if(atoms_tmp_string.size() >= min_atoms_len)
  {
    my_atoms.push_back(atoms_tmp_string);
  }
  return my_atoms;
}



void FilteredRE2::Compile(std::vector<std::string>* atoms) {
  if (compiled_) {
    LOG(ERROR) << "Compile called already.";
    return;
  }

  if (re2_vec_.empty()) {
    LOG(ERROR) << "Compile called before Add.";
    return;
  }
  atoms->clear();
  
  for(size_t i = 0; i < re2_vec_.size(); i++)
  {
    std::vector<std::string> my_atoms = MyCompile(re2_vec_[i]->pattern(), prefilter_tree_->getMinAtomLen());
    for(auto x : my_atoms)
      atoms->push_back(x);
  }
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
  // std::map<std::string, bool> map;
  
  std::vector<std::string> atoms_total;
  std::vector<int> vec_per_num;
  std::vector<std::string> atoms_tmp;
  std::vector<size_t> re_v;

  for(size_t i = 0; i < re2_vec_.size(); i++)
  {
    std::vector<std::string> my_atoms = MyCompile(re2_vec_[i]->pattern(), min_atom_len);

    if(my_atoms.size() != 0)
    {
      for(auto x : my_atoms)
        atoms_total.push_back(x);
    }

  }
  for(size_t i = 0; i < atoms.size(); i++)
  {
    atoms_tmp.push_back(atoms_total[atoms[i]]);
  }
  for(size_t i = 0; i < re2_vec_.size(); i++)
  {
    std::vector<std::string> my_atoms = MyCompile(re2_vec_[i]->pattern(), min_atom_len);
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
