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
 * Description: Interface implementation in re2.h.
 ******************************************************************************/

#include "re2/re2.h"
#include <iostream>
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#ifdef _MSC_VER
#include <intrin.h>
#endif
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include <atomic>
#include <iterator>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "re2/testing/util/util.h"
#include "re2/testing/util/logging.h"
// #include "util/strutil.h"
// #include "util/utf.h"
#include "regex_internal.h"

using namespace std;

extern "C"
{
#include "regex-capi/include/rure.h"
}

namespace re2
{
  // Maximum number of args we can set
  static const int kMaxArgs = 16;
  static const int kVecSize = 1 + kMaxArgs;

  const int RE2::Options::kDefaultMaxMem; // initialized in re2.h

  RE2::Options::Options(RE2::CannedOptions opt)
      : encoding_(opt == RE2::Latin1 ? EncodingLatin1 : EncodingUTF8),
        posix_syntax_(opt == RE2::POSIX),
        longest_match_(opt == RE2::POSIX),
        log_errors_(opt != RE2::Quiet),
        max_mem_(kDefaultMaxMem),
        literal_(false),
        never_nl_(false),
        dot_nl_(false),
        never_capture_(false),
        case_sensitive_(true),
        perl_classes_(false),
        word_boundary_(false),
        one_line_(false)
  {
  }

  // static empty objects for use as const references.
  // To avoid global constructors, allocated in RE2::Init().
  static const std::string *empty_string;
  static const std::map<std::string, int> *empty_named_groups;
  static const std::map<int, std::string> *empty_group_names;

  RE2::RE2(const char *pattern)
  {
    Init(pattern, DefaultOptions);
  }

  RE2::RE2(const std::string &pattern)
  {
    Init(pattern, DefaultOptions);
  }

  RE2::RE2(const StringPiece &pattern)
  {
    Init(pattern, DefaultOptions);
  }

  RE2::RE2(const StringPiece &pattern, const Options &options)
  {
    Init(pattern, options);
  }

  std::string encodingLatin1ToUTF8(std::string str)
  {
    string strOut;
    for (std::string::iterator it = str.begin(); it != str.end(); ++it)
    {
      uint8_t ch = *it;
      if (ch < 0x80)
      {
        strOut.push_back(ch);
      }
      else
      {
        strOut.push_back(0xc0 | ch >> 6);
        strOut.push_back(0x80 | (ch & 0x3f));
      }
    }
    return strOut;
  }

  void RE2::Init(const StringPiece &pattern, const Options &options)
  {
    std::string rure_str; // 正则表达式UTF-8编码形式
    static std::once_flag empty_once;
    std::call_once(empty_once, []() { //为了解决多线程中出现的资源竞争导致的数据不一致问题
      empty_string = new std::string;
      empty_named_groups = new std::map<std::string, int>;
      empty_group_names = new std::map<int, std::string>;
    });

    pattern_.assign(pattern.data(), pattern.size()); // Set value to a C substring.
    options_.Copy(options);                          // option
    entire_regexp_ = NULL;
    error_ = empty_string;
    error_code_ = NoError; // Erases the string, making it empty.
    error_arg_.clear();
    prefix_.clear();
    prefix_foldcase_ = false;
    suffix_regexp_ = NULL;
    prog_ = NULL;
    num_captures_ = -1;
    is_one_pass_ = false;

    rprog_ = NULL;
    named_groups_ = NULL;
    group_names_ = NULL;

    rure_error *err = rure_error_new();

    // 对传入的Latin-1编码的字符串要进行转换
    if (options.encoding() == RE2::Options::EncodingUTF8)
    { // UTF-8编码
      rure_str = pattern.ToString();
    }
    else
    { // Latin-1编码
      rure_str = encodingLatin1ToUTF8(pattern.ToString());
    }

    uint32_t flags = RURE_DEFAULT_FLAGS;
    if(options_.dot_nl()) flags = RURE_FLAG_DOTNL;
    // if(options_.never_nl()) flags = RURE_DEFAULT_FLAGS;
    if(options_.encoding() == RE2::Options::EncodingLatin1){
      flags |= RURE_FLAG_UNICODE; 
    }

    // for All
    rure *re = rure_compile((const uint8_t *)rure_str.c_str(), strlen(rure_str.c_str()), flags, NULL, err);
    //如果编译失败，打印错误信息
    if (re == NULL)
    {
      const char *msg = rure_error_message(err);
      LOG(ERROR) << "Error Compile '" << pattern.data() << "':" << msg << "'";
      std::string empty_character_classes = "empty character classes are not allowed";

      // 处理空字符集无法编译的问题
      std::string msg_info = msg;
      if (msg_info.find(empty_character_classes) != string::npos)
      {
        pattern_ = "";
        error_ = new std::string(msg);
        error_code_ = ErrorInternal;
      }
      else
      {
        if (options_.log_errors())
        {
          LOG(ERROR) << "Error Compile '" << pattern.data() << "':" << msg << "'";
        }
        error_ = new std::string(msg);
        error_code_ = ErrorInternal; // 暂时对这个错误进行赋值，如何处理错误类型？？？    
      }
      return;
    }
    prog_ = (Prog *)re;
    // for Consume and FindAndConsume
    suffix_regexp_ = (re2::Regexp *)rure_new((const uint8_t *)pattern.data(), pattern.size());
    // for FullMatch
    if(rure_str != "")
    {
      std::string FullMatch_rure_str = rure_str;
      FullMatch_rure_str.insert(0, "^(");
      FullMatch_rure_str.append(")$");
      entire_regexp_ = (re2::Regexp *)rure_compile((const uint8_t *)FullMatch_rure_str.c_str(), strlen(FullMatch_rure_str.c_str()), flags, NULL, err);
    }
    else
    {
      entire_regexp_ = (re2::Regexp *)re;
    }

    //获取捕获组的数量, 并对num_captures_其进行赋值
    rure_captures *caps = rure_captures_new(re);
    size_t captures_len = rure_captures_len(caps) - 1;
    if(!options_.never_capture()) 
    {
      num_captures_ = (int)captures_len;
    }
    else 
    {
      num_captures_ = 0;
    }

    rure_captures_free(caps);
    rure_error_free(err);
    error_ = empty_string;
    error_code_ = RE2::NoError;   
  }

  RE2::~RE2()
  {
    if (error_ != empty_string)
      delete error_;
    if (named_groups_ != NULL && named_groups_ != empty_named_groups)
      delete named_groups_;
    if (group_names_ != NULL && group_names_ != empty_group_names)
      delete group_names_;
  }

  // Returns named_groups_, computing it if needed.
  const std::map<std::string, int> &RE2::NamedCapturingGroups() const
  {
    std::map<std::string, int> *temp = new std::map<std::string, int>;
    std::string str;
    char *name;
    int i = 0;
    rure_iter_capture_names *it = rure_iter_capture_names_new((rure *)prog_);
    while (rure_iter_capture_names_next(it, &name))
    {
      str = name;
      if (str.length() != 0)
        temp->insert(make_pair(str, i));
      ++i;
    }
    named_groups_ = temp;

    return *named_groups_;
  }
  /*
  // Returns group_names_, computing it if needed.
  const std::map<int, std::string> &RE2::CapturingGroupNames() const
  {
    std::map<int, std::string> *temp = new std::map<int, std::string>;
    std::string str;
    char *name;
    int i = 0;
    rure_iter_capture_names *it = rure_iter_capture_names_new((rure *)prog_);
    while (rure_iter_capture_names_next(it, &name))
    {
      str = name;
      if (str.length() != 0)
        temp->insert(make_pair(i, str));
      ++i;
    }
    group_names_ = temp;

    return *group_names_;
  }
  */

  /***** Convenience interfaces *****/

  bool RE2::FullMatchN(const StringPiece &text, const RE2 &re,
                       const Arg *const args[], int n)
  {
    return re.DoMatch(text, ANCHOR_BOTH, NULL, args, n);
  }

  bool RE2::PartialMatchN(const StringPiece &text, const RE2 &re,
                          const Arg *const args[], int n)
  {
    return re.DoMatch(text, UNANCHORED, NULL, args, n);
  }

  bool RE2::ConsumeN(StringPiece *input, const RE2 &re,
                     const Arg *const args[], int n)
  {
    size_t consumed;
    if (re.DoMatch(*input, ANCHOR_START, &consumed, args, n))
    {
      input->remove_prefix(consumed);
      return true;
    }
    else
    {
      return false;
    }
  }

  bool RE2::FindAndConsumeN(StringPiece *input, const RE2 &re,
                            const Arg *const args[], int n)
  {
    size_t consumed;
    if (re.DoMatch(*input, UNANCHORED, &consumed, args, n))
    {
      input->remove_prefix(consumed);
      return true;
    }
    else
    {
      return false;
    }
  }


  bool RE2::Replace(std::string *str,
                    const RE2 &re,
                    const StringPiece &rewrite)
  {
    StringPiece vec[kVecSize];
    int nvec = 1 + MaxSubmatch(rewrite);
    if (nvec > 1 + re.NumberOfCapturingGroups() || nvec > static_cast<int>(arraysize(vec)))
      return false;
    std::string s;
    if (!re.Match(*str, 0, str->size(), UNANCHORED, vec, nvec) || !re.Rewrite(&s, rewrite, vec, nvec))
      return false;

    // 利用rure进行replace
    const char *rure_str = re.pattern_.c_str();
    // 对rewrite进行处理
    const char *rure_rewrite = rure_rewrite_str_convert((const uint8_t*)rewrite.data(), rewrite.size());

    rure *re_rure = rure_compile((const uint8_t *)rure_str, strlen(rure_str), RURE_DEFAULT_FLAGS, NULL, NULL);
    const char *str_rure = rure_replace(re_rure, (const uint8_t *)str->c_str(), strlen(str->c_str()),
                                        (const uint8_t *)rure_rewrite, strlen(rure_rewrite));
    *str = str_rure;

    return true;
  }

  int RE2::GlobalReplace(std::string *str,
                         const RE2 &re,
                         const StringPiece &rewrite)
  {
    StringPiece vec[kVecSize];
    int count = 0;
    int nvec = 1 + MaxSubmatch(rewrite);
    if (nvec > 1 + re.NumberOfCapturingGroups() || nvec > static_cast<int>(arraysize(vec)))
      return false;
    std::string s;
    if (!re.Match(*str, 0, str->size(), UNANCHORED, vec, nvec) || !re.Rewrite(&s, rewrite, vec, nvec))
      return false;

    // 利用rure进行replace_all
    const char *rure_str = re.pattern_.c_str();
    rure *rure_re = rure_compile_must(rure_str);
    count = rure_replace_count(rure_re, str->c_str());
    if (count != 0)
    {
      // 对rewrite进行处理
      const char *rure_rewrite = rure_rewrite_str_convert((const uint8_t*)rewrite.data(), rewrite.size());
      const char *str_rure = rure_replace_all(rure_re, (const uint8_t *)str->c_str(), strlen(str->c_str()),
                                              (const uint8_t *)rure_rewrite, strlen(rure_rewrite));
      *str = str_rure;
      return count;
    }
    return 0;
  }

  bool RE2::Extract(const StringPiece &text,
                    const RE2 &re,
                    const StringPiece &rewrite,
                    std::string *out)
  {
    StringPiece vec[kVecSize];
    int nvec = 1 + MaxSubmatch(rewrite);
    if (nvec > 1 + re.NumberOfCapturingGroups() || nvec > static_cast<int>(arraysize(vec)))
      return false;
    if (!re.Match(text, 0, text.size(), UNANCHORED, vec, nvec))
      return false;

    out->clear();
    return re.Rewrite(out, rewrite, vec, nvec);
  }

  std::string RE2::QuoteMeta(const StringPiece &unquoted)
  {
    std::string result;
    result.reserve(unquoted.size() << 1);

    // Escape any ascii character not in [A-Za-z_0-9].
    //
    // Note that it's legal to escape a character even if it has no
    // special meaning in a regular expression -- so this function does
    // that.  (This also makes it identical to the perl function of the
    // same name except for the null-character special case;
    // see `perldoc -f quotemeta`.)
    for (size_t ii = 0; ii < unquoted.size(); ++ii)
    {
      // Note that using 'isalnum' here raises the benchmark time from
      // 32ns to 58ns:
      if ((unquoted[ii] < 'a' || unquoted[ii] > 'z') &&
          (unquoted[ii] < 'A' || unquoted[ii] > 'Z') &&
          (unquoted[ii] < '0' || unquoted[ii] > '9') &&
          unquoted[ii] != '_' && unquoted[ii] != '!' &&
          unquoted[ii] != ' ' && unquoted[ii] != '\'' &&
          unquoted[ii] != '=' &&
          // If this is the part of a UTF8 or Latin1 character, we need
          // to copy this byte without escaping.  Experimentally this is
          // what works correctly with the regexp library.
          !(unquoted[ii] & 128))
      {
        if (unquoted[ii] == '\0')
        { // Special handling for null chars.
          // Note that this special handling is not strictly required for RE2,
          // but this quoting is required for other regexp libraries such as
          // PCRE.
          // Can't use "\\0" since the next character might be a digit.
          result += "\\x00";
          continue;
        }
        result += '\\';
      }
      result += unquoted[ii];
    }

    return result;
  }

  /***** Actual matching and rewriting code *****/

  bool RE2::Match(const StringPiece &text,
                  size_t startpos,
                  size_t endpos,
                  Anchor re_anchor,
                  StringPiece *submatch,
                  int nsubmatch) const
  {
    if(text.size() == 0 && pattern() == "")
    {
      return true;
    }
    if (!ok())
    {
      if (options_.log_errors())
        LOG(ERROR) << "Invalid RE2: " << *error_;
      return false;
    }

    if (startpos > endpos || endpos > text.size())
    {
      if (options_.log_errors())
        LOG(ERROR) << "RE2: invalid startpos, endpos pair. ["
                   << "startpos: " << startpos << ", "
                   << "endpos: " << endpos << ", "
                   << "text size: " << text.size() << "]";
      return false;
    }
    // 对null和empty进行处理
    if(text.data() == NULL)
    {
      for(int i = 0; i < nsubmatch; i++)
      {
        submatch[i] = NULL;
      }
      return true;
    }

    std::string haystack;
    if (text.data() == NULL || text[0] == '\0')
    {
      haystack = "";
    }
    else
    {
      haystack = text.ToString();
    }

    // Latin-1编码转换
    if (options_.encoding() == RE2::Options::EncodingLatin1)
    {
      haystack = encodingLatin1ToUTF8(text.as_string());
    }
    rure *re = (rure *)prog_;
    // rure *re1 = (rure *)rprog_;
    rure_match match = {0};
    size_t length = strlen(haystack.c_str());
    if(options_.never_nl())
    {
      std::string strs = haystack + '\n';
      size_t pos = strs.find('\n');
      bool flag = false;
      while(pos != strs.npos)
      {
        std::string temp = strs.substr(0, pos);
        bool matched = rure_is_match(re, (const uint8_t *)temp.c_str(), strlen(temp.c_str()), 0);
        if(matched && !nsubmatch){
          return true;
        }
        if(matched && nsubmatch){
          haystack = temp;
          length = strlen(haystack.c_str());
          flag = true;
          break;
        }
        strs = strs.substr(pos + 1, length + 1);
        pos = strs.find('\n');
      }
      if(!flag){return false;}
    }
    // bool matched = rure_find(re, (const uint8_t *)haystack, strlen(haystack), 0, &match);
    // 这里没有 if(re_anchor == ANCHOR_START)原因是因为：
    // 只有Consume()使用了ANCHOR_START，而传入Consume()的参数通常是三个或者三个以上，
    // 调用Consume()时，nsubmatch不为0，因此会去执行rure_captures_new()、rure_find_captures()、rure_captures_at()
    if(re_anchor == UNANCHORED)
    {
      // bool matched = rure_find(re, (const uint8_t *)haystack.c_str(), length, 0, &match);
      bool matched = rure_is_match(re, (const uint8_t *)haystack.c_str(), length, 0);
      if(!matched){
        return false;
      }
      else if(!nsubmatch){
        return true;
      }
    }
    else if(re_anchor == ANCHOR_BOTH)
    {

      bool matched = rure_find(re, (const uint8_t *)haystack.c_str(), length, 0, &match);
      if(!matched || match.start != 0 || match.end != length){
        return false;
      }
      else if(!nsubmatch){
        return true;
      }
    }
    
    // Demo  获取捕获组内容，存储到submatch数组中
    rure_captures *caps = rure_captures_new(re);
    rure_find_captures(re, (const uint8_t *)haystack.c_str(),
                       length, 0, caps);
    // size_t captures_len = num_captures_ + 1;
    
    rure_captures_at(caps, 0, &match);
    if (re_anchor == ANCHOR_START && match.start != 0)
      return false;

    for (int i = 0; i < nsubmatch; i++)
    {
      bool result = rure_captures_at(caps, i, &match);
      if (result)
      {
        size_t start = match.start;
        size_t end = match.end;
        size_t len = end - start;
        if(options_.encoding() == RE2::Options::EncodingUTF8){
          submatch[i] = StringPiece(text.data() + start, static_cast<size_t>(len));
        }
        else{
          submatch[i] = StringPiece(text.data() + start, static_cast<size_t>(len / 2));
        }
        
      }
      else
      {
        submatch[i] = StringPiece();
      }
    }
    return true;
  }

  // std::string_view in MSVC has iterators that aren't just pointers and
  // that don't allow comparisons between different objects - not even if
  // those objects are views into the same string! Thus, we provide these
  // conversion functions for convenience.
  static inline const char *BeginPtr(const StringPiece &s)
  {
    return s.data();
  }
  static inline const char *EndPtr(const StringPiece &s)
  {
    return s.data() + s.size();
  }

  // Internal matcher - like Match() but takes Args not StringPieces.
  bool RE2::DoMatch(const StringPiece &text,
                    Anchor re_anchor,
                    size_t *consumed,
                    const Arg *const *args,
                    int n) const
  {
    // re是否成功创建
    if (!ok())
    {
      if (options_.log_errors())
        LOG(ERROR) << "Invalid RE2: " << *error_;
      return false;
    }
    // re的捕获组数目小于给定数目，返回flase
    if (NumberOfCapturingGroups() < n)
    {
      // RE has fewer capturing groups than number of Arg pointers passed in.
      return false;
    }
    
    // for Consume and FindAndConsume
    rure_match match;
    if(consumed && n == 0 &&
        rure_consume((rure *)suffix_regexp_, (const uint8_t *)text.data(), (size_t)text.size(), &match))
    {
      *consumed = match.end;
      return true;
    }
    // for FullMatch(no captures)
    if(re_anchor == ANCHOR_BOTH && n == 0 && options_.encoding() == RE2::Options::EncodingUTF8)
    {
      bool matched = rure_is_match((rure *)entire_regexp_, (const uint8_t *)text.data(), (size_t)text.size(), 0);
      return matched;
    }

    // Count number of capture groups needed.
    int nvec;
    if (n == 0 && consumed == NULL)
      nvec = 0; // 0个捕获组
    else
      nvec = n + 1;

    StringPiece *vec;
    StringPiece stkvec[kVecSize];
    StringPiece *heapvec = NULL;

    // 判断是否超出已预定的内存
    if (nvec <= static_cast<int>(arraysize(stkvec)))
    {
      vec = stkvec;
    }
    else
    {
      vec = new StringPiece[nvec];
      heapvec = vec;
    }

    // 存在捕获组的判断

    // 匹配失败，返回false
    // startpos  endpos
    // vec 用于存放捕获到的数据
    // nvec 表示需要捕获的数据的个数

    //此处在改写的时候先不进行任何处理，直接使用之前的Match函数，完成之后在对Match进行改写
    if (!Match(text, 0, text.size(), re_anchor, vec, nvec))
    {

      // std::cout << "DoMatch : Match 带参 未匹配";
      delete[] heapvec;
      return false;
    }

    //  为consume赋值，consume的
    if (consumed != NULL)
      *consumed = static_cast<size_t>(EndPtr(vec[0]) - BeginPtr(text));

    // 以上的代码已经完成了整个字符数是否和正则表达式全局匹配
    // 结下来就是要对正表达式中存在的捕获组进行处理

    // 如果不需要捕获组，直接返回true
    if (n == 0 || args == NULL)
    {
      // We are not interested in results
      delete[] heapvec;
      return true;
    }

    // If we got here, we must have matched the whole pattern.
    for (int i = 0; i < n; i++)
    {
      const StringPiece &s = vec[i + 1];

      if (!args[i]->Parse(s.data(), s.size()))
      {
        // TODO: Should we indicate what the error was?
        delete[] heapvec;
        return false;
      }
    }
    delete[] heapvec;
    return true;
  }

  // Checks that the rewrite string is well-formed with respect to this
  // regular expression.
  bool RE2::CheckRewriteString(const StringPiece &rewrite,
                               std::string *error) const
  {
    int num_caps = NumberOfCapturingGroups();
    bool result = rure_check_rewrite_string(rewrite.data(), num_caps);
    if(!result){
      *error = "Rewrite schema error";
      return false;
    }
    return true; 

  }

  // Returns the maximum submatch needed for the rewrite to be done by Replace().
  // E.g. if rewrite == "foo \\2,\\1", returns 2.
  int RE2::MaxSubmatch(const StringPiece &rewrite)
  {
    int max = rure_max_submatch(rewrite.data());
    return max;
  }

  // Append the "rewrite" string, with backslash subsitutions from "vec",
  // to string "out".
  bool RE2::Rewrite(std::string *out,
                    const StringPiece &rewrite,
                    const StringPiece *vec,
                    int veclen) const
  {
    size_t len = rewrite.length();
    const char *rewrites[veclen];
    size_t rewrites_lengths[veclen];
    for(int i = 0; i < veclen; i++) {
      rewrites[i] = vec[i].data();
      rewrites_lengths[i] = vec[i].size();
    }
    const char *result = rure_rewrite((const uint8_t *)rewrite.data(), len, (const uint8_t **)rewrites, 
                                    rewrites_lengths, (size_t)veclen);
    if(result != NULL) {
      out->assign(result);
      return true;
    }
    return false;
  }

  /***** Parsers for various types *****/

  namespace re2_internal
  {

    template <>
    bool Parse(const char *str, size_t n, void *dest)
    {
      // We fail if somebody asked us to store into a non-NULL void* pointer
      return (dest == NULL);
    }

    template <>
    bool Parse(const char *str, size_t n, std::string *dest)
    {
      if (dest == NULL)
        return true;
      dest->assign(str, n);
      return true;
    }

    template <>
    bool Parse(const char *str, size_t n, StringPiece *dest)
    {
      if (dest == NULL)
        return true;
      *dest = StringPiece(str, n);
      return true;
    }

    template <>
    bool Parse(const char *str, size_t n, char *dest)
    {
      if (n != 1)
        return false;
      if (dest == NULL)
        return true;
      *dest = str[0];
      return true;
    }

    template <>
    bool Parse(const char *str, size_t n, signed char *dest)
    {
      if (n != 1)
        return false;
      if (dest == NULL)
        return true;
      *dest = str[0];
      return true;
    }

    template <>
    bool Parse(const char *str, size_t n, unsigned char *dest)
    {
      if (n != 1)
        return false;
      if (dest == NULL)
        return true;
      *dest = str[0];
      return true;
    }

    // Largest number spec that we are willing to parse
    static const int kMaxNumberLength = 32;

    // REQUIRES "buf" must have length at least nbuf.
    // Copies "str" into "buf" and null-terminates.
    // Overwrites *np with the new length.
    static const char *TerminateNumber(char *buf, size_t nbuf, const char *str,
                                       size_t *np, bool accept_spaces)
    {
      size_t n = *np;
      if (n == 0)
        return "";
      if (n > 0 && isspace(*str))
      {
        // We are less forgiving than the strtoxxx() routines and do not
        // allow leading spaces. We do allow leading spaces for floats.
        if (!accept_spaces)
        {
          return "";
        }
        while (n > 0 && isspace(*str))
        {
          n--;
          str++;
        }
      }

      // Although buf has a fixed maximum size, we can still handle
      // arbitrarily large integers correctly by omitting leading zeros.
      // (Numbers that are still too long will be out of range.)
      // Before deciding whether str is too long,
      // remove leading zeros with s/000+/00/.
      // Leaving the leading two zeros in place means that
      // we don't change 0000x123 (invalid) into 0x123 (valid).
      // Skip over leading - before replacing.
      bool neg = false;
      if (n >= 1 && str[0] == '-')
      {
        neg = true;
        n--;
        str++;
      }

      if (n >= 3 && str[0] == '0' && str[1] == '0')
      {
        while (n >= 3 && str[2] == '0')
        {
          n--;
          str++;
        }
      }

      if (neg)
      { // make room in buf for -
        n++;
        str--;
      }

      if (n > nbuf - 1)
        return "";

      memmove(buf, str, n);
      if (neg)
      {
        buf[0] = '-';
      }
      buf[n] = '\0';
      *np = n;
      return buf;
    }

    template <>
    bool Parse(const char *str, size_t n, float *dest)
    {
      if (n == 0)
        return false;
      static const int kMaxLength = 200;
      char buf[kMaxLength + 1];
      str = TerminateNumber(buf, sizeof buf, str, &n, true);
      char *end;
      errno = 0;
      float r = strtof(str, &end);
      if (end != str + n)
        return false; // Leftover junk
      if (errno)
        return false;
      if (dest == NULL)
        return true;
      *dest = r;
      return true;
    }

    template <>
    bool Parse(const char *str, size_t n, double *dest)
    {
      if (n == 0)
        return false;
      static const int kMaxLength = 200;
      char buf[kMaxLength + 1];
      str = TerminateNumber(buf, sizeof buf, str, &n, true);
      char *end;
      errno = 0;
      double r = strtod(str, &end);
      if (end != str + n)
        return false; // Leftover junk
      if (errno)
        return false;
      if (dest == NULL)
        return true;
      *dest = r;
      return true;
    }

    template <>
    bool Parse(const char *str, size_t n, long *dest, int radix)
    {
      if (n == 0)
        return false;
      char buf[kMaxNumberLength + 1];
      str = TerminateNumber(buf, sizeof buf, str, &n, false);
      char *end;
      errno = 0;
      long r = strtol(str, &end, radix);
      if (end != str + n)
        return false; // Leftover junk
      if (errno)
        return false;
      if (dest == NULL)
        return true;
      *dest = r;
      return true;
    }

    template <>
    bool Parse(const char *str, size_t n, unsigned long *dest, int radix)
    {
      if (n == 0)
        return false;
      char buf[kMaxNumberLength + 1];
      str = TerminateNumber(buf, sizeof buf, str, &n, false);
      if (str[0] == '-')
      {
        // strtoul() will silently accept negative numbers and parse
        // them.  This module is more strict and treats them as errors.
        return false;
      }

      char *end;
      errno = 0;
      unsigned long r = strtoul(str, &end, radix);
      if (end != str + n)
        return false; // Leftover junk
      if (errno)
        return false;
      if (dest == NULL)
        return true;
      *dest = r;
      return true;
    }

    template <>
    bool Parse(const char *str, size_t n, short *dest, int radix)
    {
      long r;
      if (!Parse(str, n, &r, radix))
        return false; // Could not parse
      if ((short)r != r)
        return false; // Out of range
      if (dest == NULL)
        return true;
      *dest = (short)r;
      return true;
    }

    template <>
    bool Parse(const char *str, size_t n, unsigned short *dest, int radix)
    {
      unsigned long r;
      if (!Parse(str, n, &r, radix))
        return false; // Could not parse
      if ((unsigned short)r != r)
        return false; // Out of range
      if (dest == NULL)
        return true;
      *dest = (unsigned short)r;
      return true;
    }

    template <>
    bool Parse(const char *str, size_t n, int *dest, int radix)
    {
      long r;
      if (!Parse(str, n, &r, radix))
        return false; // Could not parse
      if ((int)r != r)
        return false; // Out of range
      if (dest == NULL)
        return true;
      *dest = (int)r;
      return true;
    }

    template <>
    bool Parse(const char *str, size_t n, unsigned int *dest, int radix)
    {
      unsigned long r;
      if (!Parse(str, n, &r, radix))
        return false; // Could not parse
      if ((unsigned int)r != r)
        return false; // Out of range
      if (dest == NULL)
        return true;
      *dest = (unsigned int)r;
      return true;
    }

    template <>
    bool Parse(const char *str, size_t n, long long *dest, int radix)
    {
      if (n == 0)
        return false;
      char buf[kMaxNumberLength + 1];
      str = TerminateNumber(buf, sizeof buf, str, &n, false);
      char *end;
      errno = 0;
      long long r = strtoll(str, &end, radix);
      if (end != str + n)
        return false; // Leftover junk
      if (errno)
        return false;
      if (dest == NULL)
        return true;
      *dest = r;
      return true;
    }

    template <>
    bool Parse(const char *str, size_t n, unsigned long long *dest, int radix)
    {
      if (n == 0)
        return false;
      char buf[kMaxNumberLength + 1];
      str = TerminateNumber(buf, sizeof buf, str, &n, false);
      if (str[0] == '-')
      {
        // strtoull() will silently accept negative numbers and parse
        // them.  This module is more strict and treats them as errors.
        return false;
      }
      char *end;
      errno = 0;
      unsigned long long r = strtoull(str, &end, radix);
      if (end != str + n)
        return false; // Leftover junk
      if (errno)
        return false;
      if (dest == NULL)
        return true;
      *dest = r;
      return true;
    }

  } // namespace re2_internal
} // namespace re2
