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
    PrefilterTree(){};
    explicit PrefilterTree(int min_atom_len){};
    ~PrefilterTree(){};
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
  if(str.size() > 0)
  {
    for(size_t i = 0; i < vec2.size(); i++)
    {
      vec_tmp.push_back(str + vec2[i]);
    }
  }
  else if(vec1.size() == 0)
  {
    for(auto x : vec2)
    {
      std::string str;
      str.push_back(x);
      vec_tmp.push_back(str);
    }
  }
  else
  {
    for(size_t i = 0; i < vec1.size(); i++)
    {
      for(size_t j = 0; j < vec2.size(); j++)
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
  std::vector<char> atoms;   // 字符集中的atoms
  std::vector<char> vec_tmp;
  std::vector<char> vec_op;
  std::vector<char> vec_char;
  std::string str_tmp;

  vec_tmp.clear();
  vec_op.clear();
  vec_char.clear();
  int flag_connect = 0;
  int flag_plus = 0;
  for (int i = start_post; i <= end_post; i++)
  {
    if (str[i] == '[')
    {
      vec_op.push_back(str[i]);
    }
    else if ((str[i] >= 'a' && str[i] <= 'z') || (str[i] >= 0 && str[i] <= 9))
    {
      vec_char.push_back(str[i]);
    }
    else if (str[i] == '-')
    {
      flag_connect = 1;
      vec_op.push_back(str[i]);
    }
    else if (str[i] == ']')
    {
      vec_op.push_back(str[i]);
    }
    else
    {
      flag_plus = 0;
      vec_char.clear();
      vec_op.clear();
    }
  }

  // 将字符集拆分的所有可能字符存储到atoms中
  if (flag_connect == 1)
  {
    char x2 = vec_char[1];
    char x1 = vec_char[0];
    for (int i = int(x1); i <= int(x2); i++)
    {
      // std::string str_real;
      // str_real += char(i);
      atoms.push_back(char(i));
    }
  }
  else if (flag_connect == 0 && flag_plus == 0)
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
std::vector<std::string> Group_multiple_selection(std::string str, int start_point, int end_point)
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
      if (str_tmp.size() >= 3)
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
      if (vec_atoms_two[j].find(vec_atoms_one[i]) != std::string::npos &&
          vec_atoms_one[i] != vec_atoms_two[j])
      { // 如果包含，则置为空
        vec_atoms_two[j] = "";
      }
    }
  }
  // 重新赋值给 vec_atoms_real
  for (size_t i = 0; i < vec_atoms_two.size(); i++)
  {
    if (vec_atoms_two[i] != "")
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

bool JudgeIsCharOrNumber(char x)
{
  if ((x >= 'a' && x <= 'z') || (x >= 0 && x <= 9))
    return true;
  return false;
}
static std::vector<std::string> my_atoms; // 最终存储所有的atoms
static std::vector<std::string> vec_atoms_tmp; // 暂时存储atoms
static std::vector<std::string> vec_con;
static std::vector<char> atoms_tmp;
void MyCompile(std::string str, int start_post, int end_post)
{
  std::string atoms_tmp_string;          // 暂时存储
  // int start_post = 0;             // 开始位置
  // int end_post = 0;               // 结束位置
  // 将字符串所有的大写转换为小写  non-ASCII的字符现在没有进行处理
  if(start_post > end_post)
  {
    for (size_t i = 0; i < vec_atoms_tmp.size(); i++)
    {
      my_atoms.push_back(vec_atoms_tmp[i]);
    }
    return;
  }
  UpperToLower(str, 0, str.size());

  for (int i = start_post; i <= end_post; i++)
  {
    if(str[i] == '*')
    {
      continue;
    }
    if (str[i] == '.' )
    {
      if (atoms_tmp_string.size() > 2 && vec_atoms_tmp.size() == 0)
      {
        my_atoms.push_back(atoms_tmp_string);
        atoms_tmp_string.clear();
        continue;
      }
      else if(atoms_tmp_string.size() > 0 && vec_atoms_tmp.size() != 0)
      {
        for(auto x : vec_atoms_tmp)
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
    // 先处理普通字符
    if (JudgeIsCharOrNumber(str[i]))
    {
      atoms_tmp_string += str[i];
      continue;
    }

    // 处理括号分组
    if(str[i] == '(')
    {
      int group_start_post = i;
      do
      {
        ++i;
      } while(str[i] != ')');
      int group_end_post = i;
      
      std::vector<std::string> vec = Group_multiple_selection(str, group_start_post, group_end_post);
      int tmp_post_group = i;
      if(str[tmp_post_group + 1] == '.' && vec.size() != 0)
      {
        ++i;
        for(auto x : vec)
        {
          my_atoms.push_back(x);
        }
      }
      // "(abc123|def456|ghi789).*mnop[x-z]+"
      continue; 

    }

    // 处理字符集
    // [a-z]+
    // 012345
    if (str[i] == '[')
    {

      start_post = i;
      while(str[++i] != ']')
      {

      }
      int tmp_post = i;
      // 看是否有 + 号

      if(str[++tmp_post] == '+')
      {
        end_post = ++i;
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
        end_post = tmp_post - 1;
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
    int start = end_post + 1;
    if(str[start] == '[' || JudgeIsCharOrNumber(str[start]))
    {
      MyCompile(str, start, str.size() - 1);       
      
    }
    else
    {
      for (size_t i = 0; i < vec_atoms_tmp.size(); i++)
      {
        my_atoms.push_back(vec_atoms_tmp[i]);
      }
      
    }
    if(i == int(str.length() - 1) && atoms_tmp_string.size() > 2)
    {
      my_atoms.push_back(atoms_tmp_string);
    }

  }
  if(atoms_tmp_string.size() > 2)
  {
    my_atoms.push_back(atoms_tmp_string);
    atoms_tmp_string.clear();
  }
}



// static std::vector<std::string> atoms_tmp;

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
  /*
    获取到所有的atoms
    存在在atoms和atoms_tmp中
  */
  for(size_t i = 0; i < re2_vec_.size(); i++)
  {
    my_atoms.clear();
    vec_atoms_tmp.clear(); 
    vec_con.clear();
    atoms_tmp.clear();

    MyCompile(re2_vec_[i]->pattern(), 0, re2_vec_[i]->pattern().size() - 1);
    for(auto x : my_atoms)
    {
     atoms->push_back(x);
    }

  }
  compiled_ = true;
}


int FilteredRE2::SlowFirstMatch(const StringPiece& text) const {
  for (size_t i = 0; i < re2_vec_.size(); i++)
    if (RE2::PartialMatch(text, *re2_vec_[i]))
      return static_cast<int>(i);
  return -1;
}

int FilteredRE2::FirstMatch(const StringPiece& text,
                            const std::vector<int>& atoms) const {
  if (!compiled_) {
    LOG(DFATAL) << "FirstMatch called before Compile.";
    return -1;
  }
  std::vector<int> regexps;
  // 根据atoms索引获取regexp索引的规则
  /*
   * 如果没有原子, 那么直接会把re加进去。
   * 如果这个正则表达式有原子，那么要把该正则表达式的所有的原子的索引全加入，这个正则表达式才能加入成功。
   */
  for(size_t i = 0; i < re2_vec_.size(); i++)
  {
    my_atoms.clear();
    vec_atoms_tmp.clear(); 
    vec_con.clear();
    atoms_tmp.clear();
    MyCompile(re2_vec_[i]->pattern(), 0, re2_vec_[i]->pattern().size() - 1);
    if(my_atoms.size() == 0)
    {
      regexps.push_back(i);
      continue;
    }
    else
    {
      for(auto x : my_atoms)
      {
        int flag = 0;
        for(auto y : atoms)
        {
          if(x == my_atoms[y]) 
            continue;
          else
          {
            flag = 1;
            break;
          }
          if(flag == 0) regexps.push_back(i);
        }
      }
    }
  }

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
  // for(int i = 0; i < atoms_tmp.size(); i++){
    
  // }
  // prefilter_tree_->RegexpsGivenStrings(atoms, &regexps);
  for (size_t i = 0; i < re2_vec_.size(); i++)
    if (RE2::PartialMatch(text, *re2_vec_[i]))
      matching_regexps->push_back(i);
  return !matching_regexps->empty();
  
}

void FilteredRE2::AllPotentials(
    const std::vector<int>& atoms,
    std::vector<int>* potential_regexps) const {
  // prefilter_tree_->RegexpsGivenStrings(atoms, potential_regexps);
}

void FilteredRE2::RegexpsGivenStrings(const std::vector<int>& matched_atoms,
                                      std::vector<int>* passed_regexps) {
  // prefilter_tree_->RegexpsGivenStrings(matched_atoms, passed_regexps);
}

void FilteredRE2::PrintPrefilter(int regexpid) {
  // prefilter_tree_->PrintPrefilter(regexpid);
}

}  // namespace re2
