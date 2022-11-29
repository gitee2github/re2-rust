// Copyright 2006-2008 The RE2 Authors.  All Rights Reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

// Benchmarks for regular expression implementations.

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>

#include <iostream>
#include <fstream>
#include <sstream>

#include "util/benchmark.h"
#include "util/test.h"
// #include "util/flags.h"
#include "util/logging.h"
#include "util/malloc_counter.h"
#include "util/strutil.h"
// #include "re2/prog.h"
#include "re2/re2.h"
#include "re2/set.h"
// #include "re2/regexp.h"
#include "util/mutex.h"
// #include "util/pcre.h"

extern "C"
{
#include <rure.h>
}

namespace re2 {
void Test();
void MemoryUsage();
}  // namespace re2

typedef testing::MallocCounter MallocCounter;

namespace re2 {

int NumCPUs() {
  return static_cast<int>(std::thread::hardware_concurrency());
}

// Benchmark: failed search for regexp in random text.

// Generate random text that won't contain the search string,
// to test worst-case search behavior.
std::string RandomText(int64_t nbytes) {
  static const std::string* const text = []() {
    std::string* text = new std::string;
    srand(1);
    text->resize(16 << 20);
    for (int64_t i = 0; i < 16 << 20; i++) {
      // Generate a one-byte rune that isn't a control character (e.g. '\n').
      // Clipping to 0x20 introduces some bias, but we don't need uniformity.
      int byte = rand() & 0x7F;
      if (byte < 0x20)
        byte = 0x20;
      (*text)[i] = byte;
    }
    return text;
  }();
  CHECK_LE(nbytes, 16 << 20);
  return text->substr(0, nbytes);
}

// Benchmark: FindAndConsume

void FindAndConsume(benchmark::State& state) {
  std::string s = RandomText(state.range(0));
  s.append("Hello World");
  RE2 re("((Hello World))");
  for (auto _ : state) {
    StringPiece t = s;
    StringPiece u;
    CHECK(RE2::FindAndConsume(&t, re, &u));
    CHECK_EQ(u, "Hello World");
  }
  state.SetBytesProcessed(state.iterations() * state.range(0));
}

// BENCHMARK_RANGE(FindAndConsume, 8, 16)->ThreadRange(1, NumCPUs());

void EmptyPartialMatchRE2(benchmark::State& state) {
  RE2 re("");
  for (auto _ : state) {
    RE2::PartialMatch("", re);
  }
}

void EmptyPartialMatchRE2_text_re2_1KB(benchmark::State& state) {
  std::ifstream in("../../re2/testing/text_re2_1KB.txt");
  std::stringstream buffer;
  buffer << in.rdbuf();
  std::string s = buffer.str();
  RE2 re("");
  for (auto _ : state) {
    RE2::PartialMatch(s.substr(0, state.range(0)), re);
  }
  state.SetBytesProcessed(state.iterations() * state.range(0));
}

BENCHMARK(EmptyPartialMatchRE2)->ThreadRange(1, NumCPUs());
BENCHMARK_RANGE(EmptyPartialMatchRE2_text_re2_1KB, 2 << 6, 2 << 9);

void SimplePartialMatchRE2(benchmark::State& state) {
  RE2 re("abcdefg");
  for (auto _ : state) {
    RE2::PartialMatch("abcdefg", re);
  }
}
BENCHMARK(SimplePartialMatchRE2)->ThreadRange(1, NumCPUs());

static std::string http_text =
  "GET /asdfhjasdhfasdlfhasdflkjasdfkljasdhflaskdjhf"
  "alksdjfhasdlkfhasdlkjfhasdljkfhadsjklf HTTP/1.1";

void HTTPPartialMatchRE2(benchmark::State& state) {
  StringPiece a;
  RE2 re("(?-s)^(?:GET|POST) +([^ ]+) HTTP");
  for (auto _ : state) {
    RE2::PartialMatch(http_text, re, &a);
  }
}

BENCHMARK(HTTPPartialMatchRE2)->ThreadRange(1, NumCPUs());

static std::string smallhttp_text =
  "GET /abc HTTP/1.1";

void SmallHTTPPartialMatchRE2(benchmark::State& state) {
  StringPiece a;
  RE2 re("(?-s)^(?:GET|POST) +([^ ]+) HTTP");
  for (auto _ : state) {
    RE2::PartialMatch(smallhttp_text, re, &a);
  }
}

BENCHMARK(SmallHTTPPartialMatchRE2)->ThreadRange(1, NumCPUs());

void DotMatchRE2(benchmark::State& state) {
  StringPiece a;
  RE2 re("(?-s)^(.+)");
  for (auto _ : state) {
    RE2::PartialMatch(http_text, re, &a);
  }
}

BENCHMARK(DotMatchRE2)->ThreadRange(1, NumCPUs());

void ASCIIMatchRE2(benchmark::State& state) {
  StringPiece a;
  RE2 re("(?-s)^([ -~]+)");
  for (auto _ : state) {
    RE2::PartialMatch(http_text, re, &a);
  }
}

void ASCIIMatchRE2_text_re2_1KB(benchmark::State& state) {
  std::ifstream in("../../re2/testing/text_re2_1KB.txt");
  std::stringstream buffer;
  buffer << in.rdbuf();
  std::string s = buffer.str();
  RE2 re("(?-s)^([ -~]+)");
  for (auto _ : state) {
    RE2::PartialMatch(s.substr(0, state.range(0)), re);
  }
  state.SetBytesProcessed(state.iterations() * state.range(0));
}

BENCHMARK(ASCIIMatchRE2)->ThreadRange(1, NumCPUs());
BENCHMARK_RANGE(ASCIIMatchRE2_text_re2_1KB, 2 << 6, 2 << 9);


void FullMatchRE2(benchmark::State& state, const char *regexp) {
  std::string s = RandomText(state.range(0));
  s += "ABCDEFGHIJ";
  RE2 re(regexp, RE2::Latin1);
  for (auto _ : state) {
    
    CHECK(RE2::FullMatch(s, re));
  }
  state.SetBytesProcessed(state.iterations() * state.range(0));
}

void FullMatchRE2_text_re2_1KB(benchmark::State& state, const char *regexp) {

  std::ifstream in("../../re2/testing/text_re2_1KB.txt");
  std::stringstream buffer;
  buffer << in.rdbuf();
  std::string s = buffer.str();
  RE2 re(regexp, RE2::Latin1);
  for (auto _ : state) {
    
    CHECK(RE2::FullMatch(s.substr(0, state.range(0)), re));
  }
  state.SetBytesProcessed(state.iterations() * state.range(0));
}

void FullMatchRE2_text_dotnl_10(benchmark::State& state, const char *regexp) {

  const char * s = "aaa\nbbb\nccc\nddd\neee\naaa\nbbb\nccc\nddd\neee\nppp\n";
  RE2::Options opt;
  opt.set_never_nl(true);
  RE2 re(regexp, opt);
  for (auto _ : state) {
    
    CHECK(RE2::PartialMatch(s, re));
  }
  state.SetBytesProcessed(state.iterations() * state.range(0));
}

void FullMatchRE2_text_dotnl_30(benchmark::State& state, const char *regexp) {

  const char * s = "aaa\nbbb\nccc\nddd\neee\naaa\nbbb\nccc\nddd\neee\naaa\nbbb\nccc\nddd\neee\naaa\nbbb\nccc\nddd\neee\naaa\nbbb\nccc\nddd\neee\naaa\nbbb\nccc\nddd\neee\njjj\n";
  RE2::Options opt;
  opt.set_never_nl(true);
  RE2 re(regexp, opt);
  for (auto _ : state) {
    
    CHECK(RE2::PartialMatch(s, re));
  }
  state.SetBytesProcessed(state.iterations() * state.range(0));
}
void FullMatchRE2_text_dotnl_90(benchmark::State& state, const char *regexp) {

  const char * s = "aaa\nbbb\nccc\nddd\neee\naaa\nbbb\nccc\nddd\neee\naaa\nbbb\nccc\nddd\neee\naaa\nbbb\nccc\nddd\neee\naaa\nbbb\nccc\nddd\neee\naaa\nbbb\nccc\nddd\neee\naaa\nbbb\nccc\nddd\neee\naaa\nbbb\nccc\nddd\neee\naaa\nbbb\nccc\nddd\neee\naaa\nbbb\nccc\nddd\neee\naaa\nbbb\nccc\nddd\neee\naaa\nbbb\nccc\nddd\neee\naaa\nbbb\nccc\nddd\neee\naaa\nbbb\nccc\nddd\neee\naaa\nbbb\nccc\nddd\neee\naaa\nbbb\nccc\nddd\neee\naaa\nbbb\nccc\nddd\neee\naaa\nbbb\nccc\nddd\nqqq\n";
  RE2::Options opt;
  opt.set_never_nl(true);
  RE2 re(regexp, opt);
  for (auto _ : state) {
    
    CHECK(RE2::PartialMatch(s, re));
  }
  state.SetBytesProcessed(state.iterations() * state.range(0));
}

void Set_Match_UNANCHORED_NULL_RE2(benchmark::State& state)
{
  std::ifstream in("../../re2/testing/text_re2_1KB.txt");
  std::stringstream buffer;
  buffer << in.rdbuf();
  std::string str = buffer.str().substr(0, state.range(0));
  RE2::Set s(RE2::DefaultOptions, RE2::UNANCHORED);
  s.Add("(?s).*", NULL);
  s.Add("(?s).*$", NULL);
  s.Add("(?s)((.*)()()($))", NULL);
  s.Add("hwx", NULL);
  s.Add("ldi", NULL);
  s.Compile();
  for (auto _ : state) {
    s.Match(str, NULL);
  }
  state.SetBytesProcessed(state.iterations() * state.range(0));
}
BENCHMARK_RANGE(Set_Match_UNANCHORED_NULL_RE2, 2 << 6, 2 << 9);

void Set_Match_UNANCHORED_RE2(benchmark::State& state)
{
  std::ifstream in("../../re2/testing/text_re2_1KB.txt");
  std::stringstream buffer;
  buffer << in.rdbuf();
  std::string str = buffer.str().substr(0, state.range(0));
  RE2::Set s(RE2::DefaultOptions, RE2::UNANCHORED);
  s.Add("(?s).*", NULL);
  s.Add("(?s).*$", NULL);
  s.Add("(?s)((.*)()()($))", NULL);
  s.Add("hwx", NULL);
  s.Add("ldi", NULL);
  s.Compile();
  std::vector<int> v;
  for (auto _ : state) {
    s.Match(str, &v);
  }
  state.SetBytesProcessed(state.iterations() * state.range(0));
}
BENCHMARK_RANGE(Set_Match_UNANCHORED_RE2, 2 << 6, 2 << 9);

void Set_Match_ANCHOR_BOTH_NULL_RE2(benchmark::State& state)
{
  std::ifstream in("../../re2/testing/text_re2_1KB.txt");
  std::stringstream buffer;
  buffer << in.rdbuf();
  std::string str = buffer.str().substr(0, state.range(0));
  RE2::Set s(RE2::DefaultOptions, RE2::ANCHOR_BOTH);
  s.Add("(?s).*", NULL);
  s.Add("(?s).*$", NULL);
  s.Add("(?s)((.*)()()($))", NULL);
  s.Add("hwx", NULL);
  s.Add("ldi", NULL);
  s.Compile();
  for (auto _ : state) {
    s.Match(str, NULL);
  }
  state.SetBytesProcessed(state.iterations() * state.range(0));
}
BENCHMARK_RANGE(Set_Match_ANCHOR_BOTH_NULL_RE2, 2 << 6, 2 << 9);

void Set_Match_ANCHOR_BOTH_RE2(benchmark::State& state)
{
  std::ifstream in("../../re2/testing/text_re2_1KB.txt");
  std::stringstream buffer;
  buffer << in.rdbuf();
  std::string str = buffer.str().substr(0, state.range(0));
  RE2::Set s(RE2::DefaultOptions, RE2::ANCHOR_BOTH);
  s.Add("(?s).*", NULL);
  s.Add("(?s).*$", NULL);
  s.Add("(?s)((.*)()()($))", NULL);
  s.Add("hwx", NULL);
  s.Add("ldi", NULL);
  s.Compile();
  std::vector<int> v;
  for (auto _ : state) {
    s.Match(str, &v);
  }
  state.SetBytesProcessed(state.iterations() * state.range(0));
}
BENCHMARK_RANGE(Set_Match_ANCHOR_BOTH_RE2, 2 << 6, 2 << 9);

void Set_Match_ANCHOR_START_NULL_RE2(benchmark::State& state)
{
  std::ifstream in("../../re2/testing/text_re2_1KB.txt");
  std::stringstream buffer;
  buffer << in.rdbuf();
  std::string str = buffer.str().substr(0, state.range(0));
  RE2::Set s(RE2::DefaultOptions, RE2::ANCHOR_START);
  s.Add("(?s).*", NULL);
  s.Add("(?s).*$", NULL);
  s.Add("(?s)((.*)()()($))", NULL);
  s.Add("hwx", NULL);
  s.Add("ldi", NULL);
  s.Compile();
  for (auto _ : state) {
    s.Match(str, NULL);
  }
  state.SetBytesProcessed(state.iterations() * state.range(0));
}
BENCHMARK_RANGE(Set_Match_ANCHOR_START_NULL_RE2, 2 << 6, 2 << 9);

void Set_Match_ANCHOR_START_RE2(benchmark::State& state)
{
  std::ifstream in("../../re2/testing/text_re2_1KB.txt");
  std::stringstream buffer;
  buffer << in.rdbuf();
  std::string str = buffer.str().substr(0, state.range(0));
  RE2::Set s(RE2::DefaultOptions, RE2::ANCHOR_START);
  s.Add("(?s).*", NULL);
  s.Add("(?s).*$", NULL);
  s.Add("(?s)((.*)()()($))", NULL);
  s.Add("hwx", NULL);
  s.Add("ldi", NULL);
  s.Compile();
  std::vector<int> v;
  for (auto _ : state) {
    s.Match(str, &v);
  }
  state.SetBytesProcessed(state.iterations() * state.range(0));
}
BENCHMARK_RANGE(Set_Match_ANCHOR_START_RE2, 2 << 6, 2 << 9);

void Rure_Find_RE2(benchmark::State& state, const char *regexp)
{
  std::ifstream in("../../re2/testing/text_re2_1KB.txt");
  std::stringstream buffer;
  buffer << in.rdbuf();
  std::string s = buffer.str().substr(0, state.range(0));
  rure_error *err = rure_error_new();
  rure *re1 = rure_compile((const uint8_t *)regexp, strlen(regexp), RURE_DEFAULT_FLAGS, NULL, err);
  rure_match match = {0};
  for (auto _ : state) {
    bool matched = rure_find(re1, (const uint8_t *)s.c_str(), strlen(s.c_str()), 0, &match);
    CHECK(matched);
  }
  state.SetBytesProcessed(state.iterations() * state.range(0));
}

void Rure_is_Match_RE2(benchmark::State& state, const char *regexp)
{
  std::ifstream in("../../re2/testing/text_re2_1KB.txt");
  std::stringstream buffer;
  buffer << in.rdbuf();
  std::string s = buffer.str().substr(0, state.range(0));
  rure_error *err = rure_error_new();
  rure *re1 = rure_compile((const uint8_t *)regexp, strlen(regexp), RURE_DEFAULT_FLAGS, NULL, err);
  for (auto _ : state) {
    bool matched = rure_is_match((rure *)re1, (const uint8_t *)s.c_str(), strlen(s.c_str()), 0);
    CHECK(matched);
  }
  state.SetBytesProcessed(state.iterations() * state.range(0));
}

void Rure_Find_RE2_DotStar_text_re2_1KB(benchmark::State& state)  { Rure_Find_RE2(state, "(?s).*"); }
void Rure_Find_RE2_DotStarDollar_text_re2_1KB(benchmark::State& state)  { Rure_Find_RE2(state, "(?s).*$"); }
void Rure_Find_RE2_DotStarCapture_text_re2_1KB(benchmark::State& state)  { Rure_Find_RE2(state, "(?s)((.*)()()($))"); }

// BENCHMARK_RANGE(Rure_Find_RE2_DotStar_text_re2_1KB, 2 << 3, 2 << 9);
// BENCHMARK_RANGE(Rure_Find_RE2_DotStarDollar_text_re2_1KB, 2 << 3, 2 << 9);
// BENCHMARK_RANGE(Rure_Find_RE2_DotStarCapture_text_re2_1KB, 2 << 3, 2 << 9);

// 不加起止符 ^ 结束符 $ 的正则表达式，也就是PartialMatch，通过regex对外接口rure_is_match()函数直接测试
void Rure_is_Match_RE2_DotStar_text_re2_1KB(benchmark::State& state)  { Rure_is_Match_RE2(state, "(?s).*"); }
void Rure_is_Match_RE2_DotStarDollar_text_re2_1KB(benchmark::State& state)  { Rure_is_Match_RE2(state, "(?s).*$"); }
void Rure_is_Match_RE2_DotStarCapture_text_re2_1KB(benchmark::State& state)  { Rure_is_Match_RE2(state, "(?s)((.*)()()($))"); }

// BENCHMARK_RANGE(Rure_is_Match_RE2_DotStar_text_re2_1KB, 2 << 6, 2 << 9);
// BENCHMARK_RANGE(Rure_is_Match_RE2_DotStarDollar_text_re2_1KB, 2 << 6, 2 << 9);
// BENCHMARK_RANGE(Rure_is_Match_RE2_DotStarCapture_text_re2_1KB, 2 << 6, 2 << 9);

// 加起止符 ^ 结束符 $ 的正则表达式，也就是FullMatch，通过regex对外接口rure_is_match()函数直接测试
void Rure_is_Match_RE2_Begin_DotStar_End_text_re2_1KB(benchmark::State& state)  { Rure_is_Match_RE2(state, "^(?s).*$"); }
void Rure_is_Match_RE2_Begin_DotStarDollar_End_text_re2_1KB(benchmark::State& state)  { Rure_is_Match_RE2(state, "^(?s).*$$"); }
void Rure_is_Match_RE2_Begin_DotStarCapture_End_text_re2_1KB(benchmark::State& state)  { Rure_is_Match_RE2(state, "^(?s)((.*)()()($))$"); }

// BENCHMARK_RANGE(Rure_is_Match_RE2_Begin_DotStar_End_text_re2_1KB, 2 << 6, 2 << 9);
// BENCHMARK_RANGE(Rure_is_Match_RE2_Begin_DotStarDollar_End_text_re2_1KB, 2 << 6, 2 << 9);
// BENCHMARK_RANGE(Rure_is_Match_RE2_Begin_DotStarCapture_End_text_re2_1KB, 2 << 6, 2 << 9);

// 测试dot_nl=true模式
void FullMatch_RE2_text_dotnl_10(benchmark::State& state)  { FullMatchRE2_text_dotnl_10(state, "pp"); }
void FullMatch_RE2_text_dotnl_30(benchmark::State& state)  { FullMatchRE2_text_dotnl_30(state, "jj"); }
void FullMatch_RE2_text_dotnl_90(benchmark::State& state)  { FullMatchRE2_text_dotnl_90(state, "qq"); }

BENCHMARK_RANGE(FullMatch_RE2_text_dotnl_10, 2 << 9, 2 << 9);
BENCHMARK_RANGE(FullMatch_RE2_text_dotnl_30, 2 << 9, 2 << 9);
BENCHMARK_RANGE(FullMatch_RE2_text_dotnl_90, 2 << 9, 2 << 9);

// 加起止符 ^ 结束符 $ 的正则表达式，也就是FullMatch，通过原本RE2项目对外接口FullMatch()函数测试
void FullMatch_RE2_DotStar_text_re2_1KB(benchmark::State& state)  { FullMatchRE2_text_re2_1KB(state, "(?s).*"); }
void FullMatch_RE2_DotStarDollar_text_re2_1KB(benchmark::State& state)  { FullMatchRE2_text_re2_1KB(state, "(?s).*$"); }
void FullMatch_RE2_DotStarCapture_text_re2_1KB(benchmark::State& state)  { FullMatchRE2_text_re2_1KB(state, "(?s)((.*)()()($))"); }

BENCHMARK_RANGE(FullMatch_RE2_DotStar_text_re2_1KB, 2 << 6, 2 << 9);
BENCHMARK_RANGE(FullMatch_RE2_DotStarDollar_text_re2_1KB, 2 << 6, 2 << 9);
BENCHMARK_RANGE(FullMatch_RE2_DotStarCapture_text_re2_1KB, 2 << 6, 2 << 9);

void FullMatch_DotStar_CachedRE2(benchmark::State& state)  { FullMatchRE2(state, "(?s).*"); }

void FullMatch_DotStarDollar_CachedRE2(benchmark::State& state)  { FullMatchRE2(state, "(?s).*$"); }

void FullMatch_DotStarCapture_CachedRE2(benchmark::State& state)  { FullMatchRE2(state, "(?s)((.*)()()($))"); }

BENCHMARK_RANGE(FullMatch_DotStar_CachedRE2,  2 << 19, 2 << 19);

BENCHMARK_RANGE(FullMatch_DotStarDollar_CachedRE2,  2 << 6, 2 << 9);

BENCHMARK_RANGE(FullMatch_DotStarCapture_CachedRE2,  2 << 6, 2 << 9);

}  // namespace re2
