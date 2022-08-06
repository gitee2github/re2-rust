// Copyright 2003-2009 The RE2 Authors.  All Rights Reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

// Regular expression interface RE2.
//
// Originally the PCRE C++ wrapper, but adapted to use
// the new automata-based regular expression engines.

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

#include "util/util.h"
#include "util/logging.h"
#include "util/strutil.h"
#include "util/utf.h"
// #include "re2/sparse_array.h"
// #include "re2/prog.h"
// #include "re2/regexp.h"
#include "regex_internal.h"

using namespace std;

extern "C"
{
#include <rure.h>
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

  int RE2::Options::ParseFlags() const
  {
    int flags = Regexp::ClassNL;
    switch (encoding())
    {
    default:
      if (log_errors())
        LOG(ERROR) << "Unknown encoding " << encoding();
      break;
    case RE2::Options::EncodingUTF8:
      break;
    case RE2::Options::EncodingLatin1:
      flags |= Regexp::Latin1;
      break;
    }

    if (!posix_syntax())
      flags |= Regexp::LikePerl;

    if (literal())
      flags |= Regexp::Literal;

    if (never_nl())
      flags |= Regexp::NeverNL;

    if (dot_nl())
      flags |= Regexp::DotNL;

    if (never_capture())
      flags |= Regexp::NeverCapture;

    if (!case_sensitive())
      flags |= Regexp::FoldCase;

    if (perl_classes())
      flags |= Regexp::PerlClasses;

    if (word_boundary())
      flags |= Regexp::PerlB;

    if (one_line())
      flags |= Regexp::OneLine;

    return flags;
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
    const char *rure_str; // 正则表达式UTF-8编码形式
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
    // pattern --> rure --> Prog
    // Compile
    // 要对flages进行设置，对应RE2中传入的option
    // 对传入的Latin-1编码的字符串要进行转换
    if (options.encoding() == 1)
    { // UTF-8编码
      rure_str = pattern.data();
    }
    else
    { // Latin-1编码
      rure_str = encodingLatin1ToUTF8(pattern.ToString()).c_str();
    }

    // 空字符串的处理???
    rure *re = rure_compile((const uint8_t *)rure_str, strlen(rure_str), RURE_DEFAULT_FLAGS, NULL, err);
    const char *msg = rure_error_message(err);

    std::string empty_character_classes = "empty character classes are not allowed";
    // 处理空字符集无法编译的问题
    std::string empty_info = msg;

    //如果编译失败，打印错误信息
    if (re == NULL)
    {
      if (empty_info.find(empty_character_classes) != string::npos)
      {
        rure_error_free(err);
        rure_error *err_tmp = rure_error_new();
        const char *empty_char = "";
        re = rure_compile((const uint8_t *)empty_char, strlen(empty_char), RURE_DEFAULT_FLAGS, NULL, err_tmp);
        prog_ = (Prog *)re;
        rure_error_free(err_tmp);
        // std::cout << "empty character classes are not allowed" << std::endl;
      }
      else
      {
        if (options_.log_errors())
        {
          LOG(ERROR) << "Error Compile '" << pattern.data() << "':" << msg << "'";
        }
        error_ = new std::string(msg);
        error_code_ = ErrorInternal; // 暂时对这个错误进行赋值，如何处理错误类型？？？
        // rure_free(re);
        // rure_error_free(err);

        return;
      }
    }
    else
    {
      prog_ = (Prog *)re;
      error_ = empty_string;
      error_code_ = RE2::NoError;
    }

    //获取捕获组的数量, 并对num_captures_其进行赋值
    rure_captures *caps = rure_captures_new(re);
    size_t captures_len = rure_captures_len(caps) - 1;
    num_captures_ = (int)captures_len;

    // 问题？？？
    // rure_free和rure_captures_free是否要进行使用？
    // error_code_如何进行赋值，RegexpErrorToRE2删除了？？？
    // rure_free(re);
  }

  // Returns rprog_, computing it if needed.
  re2::Prog *RE2::ReverseProg() const
  {
    // std::call_once(rprog_once_, [](const RE2* re) {
    //   re->rprog_ =
    //       re->suffix_regexp_->CompileToReverseProg(re->options_.max_mem() / 3);
    //   if (re->rprog_ == NULL) {
    //     if (re->options_.log_errors())
    //       LOG(ERROR) << "Error reverse compiling '" << trunc(re->pattern_) << "'";
    //     // We no longer touch error_ and error_code_ because failing to compile
    //     // the reverse Prog is not a showstopper: falling back to NFA execution
    //     // is fine. More importantly, an RE2 object is supposed to be logically
    //     // immutable: whatever ok() would have returned after Init() completed,
    //     // it should continue to return that no matter what ReverseProg() does.
    //   }
    // }, this);
    return rprog_;
  }

  RE2::~RE2()
  {
    if (suffix_regexp_)
      // suffix_regexp_->Decref();
      if (entire_regexp_)
        // entire_regexp_->Decref();
        // delete prog_;
        // delete rprog_;
        if (error_ != empty_string)
          delete error_;
    if (named_groups_ != NULL && named_groups_ != empty_named_groups)
      delete named_groups_;
    if (group_names_ != NULL && group_names_ != empty_group_names)
      delete group_names_;
  }

  int RE2::ProgramSize() const
  {
    // if (prog_ == NULL)
    //   return -1;
    // return prog_->size();
    return 0;
  }

  int RE2::ReverseProgramSize() const
  {
    // if (prog_ == NULL)
    //   return -1;
    // Prog* prog = ReverseProg();
    // if (prog == NULL)
    //   return -1;
    // return prog->size();
    return 0;
  }

  // // Finds the most significant non-zero bit in n.
  // static int FindMSBSet(uint32_t n) {
  //   DCHECK_NE(n, 0);
  // #if defined(__GNUC__)
  //   return 31 ^ __builtin_clz(n);
  // #elif defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
  //   unsigned long c;
  //   _BitScanReverse(&c, n);
  //   return static_cast<int>(c);
  // #else
  //   int c = 0;
  //   for (int shift = 1 << 4; shift != 0; shift >>= 1) {
  //     uint32_t word = n >> shift;
  //     if (word != 0) {
  //       n = word;
  //       c += shift;
  //     }
  //   }
  //   return c;
  // #endif
  // }

  // static int Fanout(Prog* prog, std::vector<int>* histogram) {
  //   SparseArray<int> fanout(prog->size());
  //   prog->Fanout(&fanout);
  //   int data[32] = {};
  //   int size = 0;
  //   for (SparseArray<int>::iterator i = fanout.begin(); i != fanout.end(); ++i) {
  //     if (i->value() == 0)
  //       continue;
  //     uint32_t value = i->value();
  //     int bucket = FindMSBSet(value);
  //     bucket += value & (value-1) ? 1 : 0;
  //     ++data[bucket];
  //     size = std::max(size, bucket+1);
  //   }
  //   if (histogram != NULL)
  //     histogram->assign(data, data+size);
  //   return size-1;
  // }

  int RE2::ProgramFanout(std::vector<int> *histogram) const
  {
    // if (prog_ == NULL)
    //   return -1;
    // return Fanout(prog_, histogram);
    return 0;
  }

  int RE2::ReverseProgramFanout(std::vector<int> *histogram) const
  {
    // if (prog_ == NULL)
    //   return -1;
    // Prog* prog = ReverseProg();
    // if (prog == NULL)
    //   return -1;
    // return Fanout(prog, histogram);
    return 0;
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

  // 处理Rewrite 将所有的 //number 转换为 ${number}
  std::string rewrite_re2_to_rure(re2::StringPiece rewrite)
  {
    std::string rure_rewrite;
    for (const char *s = rewrite.data(), *end = s + rewrite.size();
         s < end; s++)
    {
      if (*s != '\\')
      {
        rure_rewrite.push_back(*s);
        continue;
      }
      s++;
      int c = (s < end) ? *s : -1;
      if (isdigit(c))
      {
        rure_rewrite.append("${");
        rure_rewrite.push_back(c);
        rure_rewrite.push_back('}');
      }
    }
    return rure_rewrite;
  }

  bool RE2::Replace(std::string *str,
                    const RE2 &re,
                    const StringPiece &rewrite)
  {

    StringPiece vec[kVecSize];
    int nvec = 1 + MaxSubmatch(rewrite);
    if (nvec > 1 + re.NumberOfCapturingGroups())
      return false;
    if (nvec > static_cast<int>(arraysize(vec)))
      return false;
    if (!re.Match(*str, 0, str->size(), UNANCHORED, vec, nvec))
      return false;

    std::string s;
    if (!re.Rewrite(&s, rewrite, vec, nvec))
      return false;

    // 利用rure进行replace
    const char *rure_str = re.pattern_.c_str();

    // 对rewrite进行处理
    const char *rure_rewrite = rewrite_re2_to_rure(rewrite).c_str();

    rure *re_rure = rure_compile((const uint8_t *)rure_str, strlen(rure_str), RURE_DEFAULT_FLAGS, NULL, NULL);

    const char *str_rure = rure_replace(re_rure, (const uint8_t *)str->c_str(), strlen(str->c_str()),
                                        (const uint8_t *)rure_rewrite, strlen(rure_rewrite));

    // assert(vec[0].data() >= str->data());
    // assert(vec[0].data() + vec[0].size() <= str->data() + str->size());
    // str->replace(vec[0].data() - str->data(), vec[0].size(), str_rure);
    *str = str_rure;

    return true;
  }

  int RE2::GlobalReplace(std::string *str,
                         const RE2 &re,
                         const StringPiece &rewrite)
  {
    //   StringPiece vec[kVecSize];
    //   int nvec = 1 + MaxSubmatch(rewrite);
    //   if (nvec > 1 + re.NumberOfCapturingGroups())
    //     return false;
    //   if (nvec > static_cast<int>(arraysize(vec)))
    //     return false;

    //   const char* p = str->data();
    //   const char* ep = p + str->size();
    //   const char* lastend = NULL;
    //   std::string out;
    //   int count = 0;
    // #ifdef FUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION
    //   // Iterate just once when fuzzing. Otherwise, we easily get bogged down
    //   // and coverage is unlikely to improve despite significant expense.
    //   while (p == str->data()) {
    // #else
    //   while (p <= ep) {
    // #endif
    //     if (!re.Match(*str, static_cast<size_t>(p - str->data()),
    //                   str->size(), UNANCHORED, vec, nvec))
    //       break;
    //     if (p < vec[0].data())
    //       out.append(p, vec[0].data() - p);
    //     if (vec[0].data() == lastend && vec[0].empty()) {
    //       // Disallow empty match at end of last match: skip ahead.
    //       //
    //       // fullrune() takes int, not ptrdiff_t. However, it just looks
    //       // at the leading byte and treats any length >= 4 the same.
    //       if (re.options().encoding() == RE2::Options::EncodingUTF8 &&
    //           fullrune(p, static_cast<int>(std::min(ptrdiff_t{4}, ep - p)))) {
    //         // re is in UTF-8 mode and there is enough left of str
    //         // to allow us to advance by up to UTFmax bytes.
    //         Rune r;
    //         int n = chartorune(&r, p);
    //         // Some copies of chartorune have a bug that accepts
    //         // encodings of values in (10FFFF, 1FFFFF] as valid.
    //         if (r > Runemax) {
    //           n = 1;
    //           r = Runeerror;
    //         }
    //         if (!(n == 1 && r == Runeerror)) {  // no decoding error
    //           out.append(p, n);
    //           p += n;
    //           continue;
    //         }
    //       }
    //       // Most likely, re is in Latin-1 mode. If it is in UTF-8 mode,
    //       // we fell through from above and the GIGO principle applies.
    //       if (p < ep)
    //         out.append(p, 1);
    //       p++;
    //       continue;
    //     }
    //     re.Rewrite(&out, rewrite, vec, nvec);
    //     p = vec[0].data() + vec[0].size();
    //     lastend = p;
    //     count++;
    //   }

    //   if (count == 0)
    //     return 0;

    //   if (p < ep)
    //     out.append(p, ep - p);
    //   using std::swap;
    //   swap(out, *str);
    //   return count;
    return 0;
  }

  bool RE2::Extract(const StringPiece &text,
                    const RE2 &re,
                    const StringPiece &rewrite,
                    std::string *out)
  {
    StringPiece vec[kVecSize];
    int nvec = 1 + MaxSubmatch(rewrite);
    if (nvec > 1 + re.NumberOfCapturingGroups())
      return false;
    if (nvec > static_cast<int>(arraysize(vec)))
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

  bool RE2::PossibleMatchRange(std::string *min, std::string *max,
                               int maxlen) const
  {
    // if (prog_ == NULL)
    //   return false;

    // int n = static_cast<int>(prefix_.size());
    // if (n > maxlen)
    //   n = maxlen;

    // // Determine initial min max from prefix_ literal.
    // *min = prefix_.substr(0, n);
    // *max = prefix_.substr(0, n);
    // if (prefix_foldcase_) {
    //   // prefix is ASCII lowercase; change *min to uppercase.
    //   for (int i = 0; i < n; i++) {
    //     char& c = (*min)[i];
    //     if ('a' <= c && c <= 'z')
    //       c += 'A' - 'a';
    //   }
    // }

    // // Add to prefix min max using PossibleMatchRange on regexp.
    // std::string dmin, dmax;
    // maxlen -= n;
    // if (maxlen > 0 && prog_->PossibleMatchRange(&dmin, &dmax, maxlen)) {
    //   min->append(dmin);
    //   max->append(dmax);
    // } else if (!max->empty()) {
    //   // prog_->PossibleMatchRange has failed us,
    //   // but we still have useful information from prefix_.
    //   // Round up *max to allow any possible suffix.
    //   PrefixSuccessor(max);
    // } else {
    //   // Nothing useful.
    //   *min = "";
    //   *max = "";
    //   return false;
    // }

    return true;
  }

  // // Avoid possible locale nonsense in standard strcasecmp.
  // // The string a is known to be all lowercase.
  // static int ascii_strcasecmp(const char* a, const char* b, size_t len) {
  //   const char* ae = a + len;

  //   for (; a < ae; a++, b++) {
  //     uint8_t x = *a;
  //     uint8_t y = *b;
  //     if ('A' <= y && y <= 'Z')
  //       y += 'a' - 'A';
  //     if (x != y)
  //       return x - y;
  //   }
  //   return 0;
  // }

  /***** Actual matching and rewriting code *****/

  bool RE2::Match(const StringPiece &text,
                  size_t startpos,
                  size_t endpos,
                  Anchor re_anchor,
                  StringPiece *submatch,
                  int nsubmatch) const
  {

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

    const char *haystack = text.data();
    rure *re = (rure *)prog_;
    rure_match match = {0};
    bool matched = rure_find(re, (const uint8_t *)haystack, strlen(haystack), 0, &match);

    switch (re_anchor)
    {
    // ANCHOR_BOTH FullMatch
    case ANCHOR_BOTH:
    {
      // 是否是FullMatch
      if (nsubmatch != 0)
      {

        if (!matched)
        {
          return false;
        }
        else
        {
          if (match.start != 0 || match.end != strlen(haystack))
          {
            return false;
          }
        }
      }
      else
      {
        if (matched && match.start == startpos && match.end == endpos)
        {
          return true;
        }
        else
        {
          return false;
        }
      }
      break;
    }
    // UNANCHORED  PartialMatch
    case UNANCHORED:
    {
      if (nsubmatch != 0)
      {
        if (!matched)
        {
          return false;
        }
      }
      else
      {
        if (matched && match.end != 0)
          return true;
        else
          return false;
      }
      break;
    }
    case ANCHOR_START:
    {
      if (nsubmatch == 0)
      {
        if (matched && match.start == startpos)
          return true;
        else
          return false;
      }
      else
      {
        if (!matched)
          return false;
      }
    }
    }

    // Demo  获取捕获组内容，存储到submatch数组中

    size_t length = strlen(haystack);

    rure_captures *caps = rure_captures_new(re);
    rure_find_captures(re, (const uint8_t *)haystack,
                       length, 0, caps);
    size_t captures_len = num_captures_ + 1;

    rure_captures_at(caps, 0, &match);
    if (re_anchor == ANCHOR_START && match.start != 0)
      return false;

    for (size_t i = 0; i < captures_len; i++)
    {
      bool result = rure_captures_at(caps, i, &match);
      if (result)
      {
        size_t start = match.start;
        size_t end = match.end;
        size_t len = end - start;

        submatch[i] = StringPiece(text.data() + start, static_cast<size_t>(len));
        // std::cout << "i=" << i << ", start=" << start << ", submatch=" << submatch[i] << endl;
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

    // 判断是否FullMatch, 判空
    const char *haystack;
    if (text.data() == NULL || text[0] == '\0')
    {
      haystack = "";
    }
    else
    {
      haystack = text.data();
    }

    // Latin-1编码转换
    if (options_.encoding() == 2)
    {
      // std::cout << "DoMatch-Latin-1\n";
      haystack = encodingLatin1ToUTF8(text.as_string()).c_str();
    }

    rure *re = (rure *)prog_;
    rure_match match = {0};
    bool matched = rure_find(re, (const uint8_t *)haystack, strlen(haystack), 0, &match);

    // Count number of capture groups needed.
    int nvec;
    if (n == 0 && consumed == NULL)
      nvec = 0; // 0个捕获组
    else
      nvec = n + 1;
    // 0个捕获组的匹配判断
    if (nvec == 0)
    {
      switch (re_anchor)
      {
      // ANCHOR_BOTH FullMatch
      case ANCHOR_BOTH:
      {
        if (!matched)
        {
          return false;
        }
        else
        {
          if (match.start == 0 && match.end == strlen(haystack))
          {
            // std::cout << "DoMatch : 0个捕获组, FullMatch成功!!\n";
            return true;
          }
          else
          {
            // std::cout << "位置不对\n";
            return false;
          }
        }

        break;
      }
      // ANCHOR_START
      case ANCHOR_START:
      {
        if (!matched)
        {
          return false;
        }
        else
        {
          if (match.start == 0)
          {
            return true;
          }
          else
          {
            // std::cout << "位置不对\n";
            return false;
          }
        }
        break;
      }

      // UNANCHORED  PartialMatch
      case UNANCHORED:
      {
        if (!matched)
        {
          return false;
        }
        else
        {
          return true;
        }

        break;
      }
      }
    }

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
    // if (n == 0 || args == NULL)
    // {
    //   // We are not interested in results
    //   delete[] heapvec;
    //   return true;
    // }

    // If we got here, we must have matched the whole pattern.
    for (int i = 0; i < n; i++)
    {
      // cout << vec[i].data() << endl;
      const StringPiece &s = vec[i + 1];
      // std::cout << s.data() << "-" << s.size() <<std::endl;

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
    // int max_token = -1;
    // for (const char *s = rewrite.data(), *end = s + rewrite.size();
    //      s < end; s++) {
    //   int c = *s;
    //   if (c != '\\') {
    //     continue;
    //   }
    //   if (++s == end) {
    //     *error = "Rewrite schema error: '\\' not allowed at end.";
    //     return false;
    //   }
    //   c = *s;
    //   if (c == '\\') {
    //     continue;
    //   }
    //   if (!isdigit(c)) {
    //     *error = "Rewrite schema error: "
    //              "'\\' must be followed by a digit or '\\'.";
    //     return false;
    //   }
    //   int n = (c - '0');
    //   if (max_token < n) {
    //     max_token = n;
    //   }
    // }

    // if (max_token > NumberOfCapturingGroups()) {
    //   *error = StringPrintf(
    //       "Rewrite schema requests %d matches, but the regexp only has %d "
    //       "parenthesized subexpressions.",
    //       max_token, NumberOfCapturingGroups());
    //   return false;
    // }
    return true;
  }

  // Returns the maximum submatch needed for the rewrite to be done by Replace().
  // E.g. if rewrite == "foo \\2,\\1", returns 2.
  int RE2::MaxSubmatch(const StringPiece &rewrite)
  {
    int max = 0;
    for (const char *s = rewrite.data(), *end = s + rewrite.size();
         s < end; s++)
    {
      if (*s == '\\')
      {
        s++;
        int c = (s < end) ? *s : -1;
        if (isdigit(c))
        {
          int n = (c - '0');
          if (n > max)
            max = n;
        }
      }
    }
    return max;
  }

  // Append the "rewrite" string, with backslash subsitutions from "vec",
  // to string "out".
  bool RE2::Rewrite(std::string *out,
                    const StringPiece &rewrite,
                    const StringPiece *vec,
                    int veclen) const
  {
    for (const char *s = rewrite.data(), *end = s + rewrite.size();
         s < end; s++)
    {
      if (*s != '\\')
      {
        out->push_back(*s);
        continue;
      }
      s++;
      int c = (s < end) ? *s : -1;
      if (isdigit(c))
      {
        int n = (c - '0');
        if (n >= veclen)
        {
          if (options_.log_errors())
          {
            LOG(ERROR) << "invalid substitution \\" << n
                       << " from " << veclen << " groups";
          }
          return false;
        }
        StringPiece snip = vec[n];
        if (!snip.empty())
          out->append(snip.data(), snip.size());
      }
      else if (c == '\\')
      {
        out->push_back('\\');
      }
      else
      {
        if (options_.log_errors())
          LOG(ERROR) << "invalid rewrite pattern: " << rewrite.data();
        return false;
      }
    }
    return true;
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

  namespace hooks
  {

#ifdef RE2_HAVE_THREAD_LOCAL
    thread_local const RE2 *context = NULL;
#endif

    template <typename T>
    union Hook
    {
      void Store(T *cb) { cb_.store(cb, std::memory_order_release); }
      T *Load() const { return cb_.load(std::memory_order_acquire); }

#if !defined(__clang__) && defined(_MSC_VER)
      // Citing https://github.com/protocolbuffers/protobuf/pull/4777 as precedent,
      // this is a gross hack to make std::atomic<T*> constant-initialized on MSVC.
      static_assert(ATOMIC_POINTER_LOCK_FREE == 2,
                    "std::atomic<T*> must be always lock-free");
      T *cb_for_constinit_;
#endif

      std::atomic<T *> cb_;
    };

    template <typename T>
    static void DoNothing(const T &) {}

#define DEFINE_HOOK(type, name)                                       \
  static Hook<type##Callback> name##_hook = {{&DoNothing<type>}};     \
  void Set##type##Hook(type##Callback *cb) { name##_hook.Store(cb); } \
  type##Callback *Get##type##Hook() { return name##_hook.Load(); }

    DEFINE_HOOK(DFAStateCacheReset, dfa_state_cache_reset)
    DEFINE_HOOK(DFASearchFailure, dfa_search_failure)

#undef DEFINE_HOOK

  } // namespace hooks

} // namespace re2
