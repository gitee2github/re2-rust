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
#include "util/flags.h"
#include "util/logging.h"
#include "util/malloc_counter.h"
#include "util/strutil.h"
// #include "re2/prog.h"
#include "re2/re2.h"
// #include "re2/regexp.h"
#include "util/mutex.h"
#include "util/pcre.h"

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
/*
void Test() {
  Regexp* re = Regexp::Parse("(\\d+)-(\\d+)-(\\d+)", Regexp::LikePerl, NULL);
  CHECK(re);
  Prog* prog = re->CompileToProg(0);
  CHECK(prog);
  CHECK(prog->IsOnePass());
  CHECK(prog->CanBitState());
  const char* text = "650-253-0001";
  StringPiece sp[4];
  CHECK(prog->SearchOnePass(text, text, Prog::kAnchored, Prog::kFullMatch, sp, 4));
  CHECK_EQ(sp[0], "650-253-0001");
  CHECK_EQ(sp[1], "650");
  CHECK_EQ(sp[2], "253");
  CHECK_EQ(sp[3], "0001");
  delete prog;
  re->Decref();
  LOG(INFO) << "test passed\n";
}

void MemoryUsage() {
  const char* regexp = "(\\d+)-(\\d+)-(\\d+)";
  const char* text = "650-253-0001";
  {
    MallocCounter mc(MallocCounter::THIS_THREAD_ONLY);
    Regexp* re = Regexp::Parse(regexp, Regexp::LikePerl, NULL);
    CHECK(re);
    // Can't pass mc.HeapGrowth() and mc.PeakHeapGrowth() to LOG(INFO) directly,
    // because LOG(INFO) might do a big allocation before they get evaluated.
    fprintf(stderr, "Regexp: %7lld bytes (peak=%lld)\n",
            mc.HeapGrowth(), mc.PeakHeapGrowth());
    mc.Reset();

    Prog* prog = re->CompileToProg(0);
    CHECK(prog);
    CHECK(prog->IsOnePass());
    CHECK(prog->CanBitState());
    fprintf(stderr, "Prog:   %7lld bytes (peak=%lld)\n",
            mc.HeapGrowth(), mc.PeakHeapGrowth());
    mc.Reset();

    StringPiece sp[4];
    CHECK(prog->SearchOnePass(text, text, Prog::kAnchored, Prog::kFullMatch, sp, 4));
    fprintf(stderr, "Search: %7lld bytes (peak=%lld)\n",
            mc.HeapGrowth(), mc.PeakHeapGrowth());
    delete prog;
    re->Decref();
  }

  {
    MallocCounter mc(MallocCounter::THIS_THREAD_ONLY);

    PCRE re(regexp, PCRE::UTF8);
    fprintf(stderr, "RE:     %7lld bytes (peak=%lld)\n",
            mc.HeapGrowth(), mc.PeakHeapGrowth());
    PCRE::FullMatch(text, re);
    fprintf(stderr, "RE:     %7lld bytes (peak=%lld)\n",
            mc.HeapGrowth(), mc.PeakHeapGrowth());
  }

  {
    MallocCounter mc(MallocCounter::THIS_THREAD_ONLY);

    PCRE* re = new PCRE(regexp, PCRE::UTF8);
    fprintf(stderr, "PCRE*:  %7lld bytes (peak=%lld)\n",
            mc.HeapGrowth(), mc.PeakHeapGrowth());
    PCRE::FullMatch(text, *re);
    fprintf(stderr, "PCRE*:  %7lld bytes (peak=%lld)\n",
            mc.HeapGrowth(), mc.PeakHeapGrowth());
    delete re;
  }

  {
    MallocCounter mc(MallocCounter::THIS_THREAD_ONLY);

    RE2 re(regexp);
    fprintf(stderr, "RE2:    %7lld bytes (peak=%lld)\n",
            mc.HeapGrowth(), mc.PeakHeapGrowth());
    RE2::FullMatch(text, re);
    fprintf(stderr, "RE2:    %7lld bytes (peak=%lld)\n",
            mc.HeapGrowth(), mc.PeakHeapGrowth());
  }

  fprintf(stderr, "sizeof: PCRE=%zd RE2=%zd Prog=%zd Inst=%zd\n",
          sizeof(PCRE), sizeof(RE2), sizeof(Prog), sizeof(Prog::Inst));
}
*/
int NumCPUs() {
  return static_cast<int>(std::thread::hardware_concurrency());
}

// Regular expression implementation wrappers.
// Defined at bottom of file, but they are repetitive
// and not interesting.
/*
typedef void SearchImpl(benchmark::State& state, const char* regexp,
                        const StringPiece& text, Prog::Anchor anchor,
                        bool expect_match);

SearchImpl SearchDFA, SearchNFA, SearchOnePass, SearchBitState, SearchPCRE,
    SearchRE2, SearchCachedDFA, SearchCachedNFA, SearchCachedOnePass,
    SearchCachedBitState, SearchCachedPCRE, SearchCachedRE2;

typedef void ParseImpl(benchmark::State& state, const char* regexp,
                       const StringPiece& text);

ParseImpl Parse1NFA, Parse1OnePass, Parse1BitState, Parse1PCRE, Parse1RE2,
    Parse1Backtrack, Parse1CachedNFA, Parse1CachedOnePass, Parse1CachedBitState,
    Parse1CachedPCRE, Parse1CachedRE2, Parse1CachedBacktrack;

ParseImpl Parse3NFA, Parse3OnePass, Parse3BitState, Parse3PCRE, Parse3RE2,
    Parse3Backtrack, Parse3CachedNFA, Parse3CachedOnePass, Parse3CachedBitState,
    Parse3CachedPCRE, Parse3CachedRE2, Parse3CachedBacktrack;

ParseImpl SearchParse2CachedPCRE, SearchParse2CachedRE2;

ParseImpl SearchParse1CachedPCRE, SearchParse1CachedRE2;
*/
// Benchmark: failed search for regexp in random text.

// Generate random text that won't contain the search string,
// to test worst-case search behavior.
std::string RandomText(int64_t nbytes) {
  static const std::string* const text = []() {
    std::string* text = new std::string;
    srand(1);
    text->resize(16<<20);
    for (int64_t i = 0; i < 16<<20; i++) {
      // Generate a one-byte rune that isn't a control character (e.g. '\n').
      // Clipping to 0x20 introduces some bias, but we don't need uniformity.
      int byte = rand() & 0x7F;
      if (byte < 0x20)
        byte = 0x20;
      (*text)[i] = byte;
    }
    return text;
  }();
  CHECK_LE(nbytes, 16<<20);
  return text->substr(0, nbytes);
}

// Makes text of size nbytes, then calls run to search
// the text for regexp iters times.
/*
void Search(benchmark::State& state, const char* regexp, SearchImpl* search) {
  std::string s = RandomText(state.range(0));
  search(state, regexp, s, Prog::kUnanchored, false);
  state.SetBytesProcessed(state.iterations() * state.range(0));
}
*/

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

BENCHMARK_RANGE(FindAndConsume, 8, 16)->ThreadRange(1, NumCPUs());

////////////////////////////////////////////////////////////////////////
//
// Implementation routines.  Sad that there are so many,
// but all the interfaces are slightly different.

// Runs implementation to search for regexp in text, iters times.
// Expect_match says whether the regexp should be found.
// Anchored says whether to run an anchored search.
/*
void SearchPCRE(benchmark::State& state, const char* regexp,
                const StringPiece& text, Prog::Anchor anchor,
                bool expect_match) {
  for (auto _ : state) {
    PCRE re(regexp, PCRE::UTF8);
    CHECK_EQ(re.error(), "");
    if (anchor == Prog::kAnchored)
      CHECK_EQ(PCRE::FullMatch(text, re), expect_match);
    else
      CHECK_EQ(PCRE::PartialMatch(text, re), expect_match);
  }
}

void SearchRE2(benchmark::State& state, const char* regexp,
               const StringPiece& text, Prog::Anchor anchor,
               bool expect_match) {
  for (auto _ : state) {
    RE2 re(regexp);
    CHECK_EQ(re.error(), "");
    if (anchor == Prog::kAnchored)
      CHECK_EQ(RE2::FullMatch(text, re), expect_match);
    else
      CHECK_EQ(RE2::PartialMatch(text, re), expect_match);
  }
}

PCRE* GetCachedPCRE(const char* regexp) {
  static auto& mutex = *new Mutex;
  MutexLock lock(&mutex);
  static auto& cache = *new std::unordered_map<std::string, PCRE*>;
  PCRE* re = cache[regexp];
  if (re == NULL) {
    re = new PCRE(regexp, PCRE::UTF8);
    CHECK_EQ(re->error(), "");
    cache[regexp] = re;
  }
  return re;
}

RE2* GetCachedRE2(const char* regexp) {
  static auto& mutex = *new Mutex;
  MutexLock lock(&mutex);
  static auto& cache = *new std::unordered_map<std::string, RE2*>;
  RE2* re = cache[regexp];
  if (re == NULL) {
    re = new RE2(regexp);
    CHECK_EQ(re->error(), "");
    cache[regexp] = re;
  }
  return re;
}

void SearchCachedPCRE(benchmark::State& state, const char* regexp,
                      const StringPiece& text, Prog::Anchor anchor,
                      bool expect_match) {
  PCRE& re = *GetCachedPCRE(regexp);
  for (auto _ : state) {
    if (anchor == Prog::kAnchored)
      CHECK_EQ(PCRE::FullMatch(text, re), expect_match);
    else
      CHECK_EQ(PCRE::PartialMatch(text, re), expect_match);
  }
}

void SearchCachedRE2(benchmark::State& state, const char* regexp,
                     const StringPiece& text, Prog::Anchor anchor,
                     bool expect_match) {
  RE2& re = *GetCachedRE2(regexp);
  for (auto _ : state) {
    if (anchor == Prog::kAnchored)
      CHECK_EQ(RE2::FullMatch(text, re), expect_match);
    else
      CHECK_EQ(RE2::PartialMatch(text, re), expect_match);
  }
}


void Parse3PCRE(benchmark::State& state, const char* regexp,
                const StringPiece& text) {
  for (auto _ : state) {
    PCRE re(regexp, PCRE::UTF8);
    CHECK_EQ(re.error(), "");
    StringPiece sp1, sp2, sp3;
    CHECK(PCRE::FullMatch(text, re, &sp1, &sp2, &sp3));
  }
}

void Parse3RE2(benchmark::State& state, const char* regexp,
               const StringPiece& text) {
  for (auto _ : state) {
    RE2 re(regexp);
    CHECK_EQ(re.error(), "");
    StringPiece sp1, sp2, sp3;
    CHECK(RE2::FullMatch(text, re, &sp1, &sp2, &sp3));
  }
}

void Parse3CachedPCRE(benchmark::State& state, const char* regexp,
                      const StringPiece& text) {
  PCRE& re = *GetCachedPCRE(regexp);
  StringPiece sp1, sp2, sp3;
  for (auto _ : state) {
    CHECK(PCRE::FullMatch(text, re, &sp1, &sp2, &sp3));
  }
}

void Parse3CachedRE2(benchmark::State& state, const char* regexp,
                     const StringPiece& text) {
  RE2& re = *GetCachedRE2(regexp);
  StringPiece sp1, sp2, sp3;
  for (auto _ : state) {
    CHECK(RE2::FullMatch(text, re, &sp1, &sp2, &sp3));
  }
}


void Parse1PCRE(benchmark::State& state, const char* regexp,
                const StringPiece& text) {
  for (auto _ : state) {
    PCRE re(regexp, PCRE::UTF8);
    CHECK_EQ(re.error(), "");
    StringPiece sp1;
    CHECK(PCRE::FullMatch(text, re, &sp1));
  }
}

void Parse1RE2(benchmark::State& state, const char* regexp,
               const StringPiece& text) {
  for (auto _ : state) {
    RE2 re(regexp);
    CHECK_EQ(re.error(), "");
    StringPiece sp1;
    CHECK(RE2::FullMatch(text, re, &sp1));
  }
}

void Parse1CachedPCRE(benchmark::State& state, const char* regexp,
                      const StringPiece& text) {
  PCRE& re = *GetCachedPCRE(regexp);
  StringPiece sp1;
  for (auto _ : state) {
    CHECK(PCRE::FullMatch(text, re, &sp1));
  }
}

void Parse1CachedRE2(benchmark::State& state, const char* regexp,
                     const StringPiece& text) {
  RE2& re = *GetCachedRE2(regexp);
  StringPiece sp1;
  for (auto _ : state) {
    CHECK(RE2::FullMatch(text, re, &sp1));
  }
}

void SearchParse2CachedPCRE(benchmark::State& state, const char* regexp,
                            const StringPiece& text) {
  PCRE& re = *GetCachedPCRE(regexp);
  for (auto _ : state) {
    StringPiece sp1, sp2;
    CHECK(PCRE::PartialMatch(text, re, &sp1, &sp2));
  }
}

void SearchParse2CachedRE2(benchmark::State& state, const char* regexp,
                           const StringPiece& text) {
  RE2& re = *GetCachedRE2(regexp);
  for (auto _ : state) {
    StringPiece sp1, sp2;
    CHECK(RE2::PartialMatch(text, re, &sp1, &sp2));
  }
}

void SearchParse1CachedPCRE(benchmark::State& state, const char* regexp,
                            const StringPiece& text) {
  PCRE& re = *GetCachedPCRE(regexp);
  for (auto _ : state) {
    StringPiece sp1;
    CHECK(PCRE::PartialMatch(text, re, &sp1));
  }
}

void SearchParse1CachedRE2(benchmark::State& state, const char* regexp,
                           const StringPiece& text) {
  RE2& re = *GetCachedRE2(regexp);
  for (auto _ : state) {
    StringPiece sp1;
    CHECK(RE2::PartialMatch(text, re, &sp1));
  }
}
*/

void EmptyPartialMatchPCRE(benchmark::State& state) {
  PCRE re("");
  for (auto _ : state) {
    PCRE::PartialMatch("", re);
  }
}

void EmptyPartialMatchRE2(benchmark::State& state) {
  RE2 re("");
  for (auto _ : state) {
    RE2::PartialMatch("", re);
  }
}

void EmptyPartialMatchRE2_LiuZhitao(benchmark::State& state) {
  std::ifstream in("re2/testing/text_re2_1KB.txt");
  std::stringstream buffer;
  buffer << in.rdbuf();
  std::string s = buffer.str();
  RE2 re("");
  for (auto _ : state) {
    RE2::PartialMatch(s.substr(0, state.range(0)), re);
  }
  state.SetBytesProcessed(state.iterations() * state.range(0));
}

#ifdef USEPCRE
BENCHMARK(EmptyPartialMatchPCRE)->ThreadRange(1, NumCPUs());
#endif
BENCHMARK(EmptyPartialMatchRE2)->ThreadRange(1, NumCPUs());
BENCHMARK_RANGE(EmptyPartialMatchRE2_LiuZhitao, 8, 2<<9);

void SimplePartialMatchPCRE(benchmark::State& state) {
  PCRE re("abcdefg");
  for (auto _ : state) {
    PCRE::PartialMatch("abcdefg", re);
  }
}

void SimplePartialMatchRE2(benchmark::State& state) {
  RE2 re("abcdefg");
  for (auto _ : state) {
    RE2::PartialMatch("abcdefg", re);
  }
}
#ifdef USEPCRE
BENCHMARK(SimplePartialMatchPCRE)->ThreadRange(1, NumCPUs());
#endif
BENCHMARK(SimplePartialMatchRE2)->ThreadRange(1, NumCPUs());

static std::string http_text =
  "GET /asdfhjasdhfasdlfhasdflkjasdfkljasdhflaskdjhf"
  "alksdjfhasdlkfhasdlkjfhasdljkfhadsjklf HTTP/1.1";

void HTTPPartialMatchPCRE(benchmark::State& state) {
  StringPiece a;
  PCRE re("(?-s)^(?:GET|POST) +([^ ]+) HTTP");
  for (auto _ : state) {
    PCRE::PartialMatch(http_text, re, &a);
  }
}

void HTTPPartialMatchRE2(benchmark::State& state) {
  StringPiece a;
  RE2 re("(?-s)^(?:GET|POST) +([^ ]+) HTTP");
  for (auto _ : state) {
    RE2::PartialMatch(http_text, re, &a);
  }
}

#ifdef USEPCRE
BENCHMARK(HTTPPartialMatchPCRE)->ThreadRange(1, NumCPUs());
#endif
BENCHMARK(HTTPPartialMatchRE2)->ThreadRange(1, NumCPUs());

static std::string smallhttp_text =
  "GET /abc HTTP/1.1";

void SmallHTTPPartialMatchPCRE(benchmark::State& state) {
  StringPiece a;
  PCRE re("(?-s)^(?:GET|POST) +([^ ]+) HTTP");
  for (auto _ : state) {
    PCRE::PartialMatch(smallhttp_text, re, &a);
  }
}

void SmallHTTPPartialMatchRE2(benchmark::State& state) {
  StringPiece a;
  RE2 re("(?-s)^(?:GET|POST) +([^ ]+) HTTP");
  for (auto _ : state) {
    RE2::PartialMatch(smallhttp_text, re, &a);
  }
}

#ifdef USEPCRE
BENCHMARK(SmallHTTPPartialMatchPCRE)->ThreadRange(1, NumCPUs());
#endif
BENCHMARK(SmallHTTPPartialMatchRE2)->ThreadRange(1, NumCPUs());

void DotMatchPCRE(benchmark::State& state) {
  StringPiece a;
  PCRE re("(?-s)^(.+)");
  for (auto _ : state) {
    PCRE::PartialMatch(http_text, re, &a);
  }
}

void DotMatchRE2(benchmark::State& state) {
  StringPiece a;
  RE2 re("(?-s)^(.+)");
  for (auto _ : state) {
    RE2::PartialMatch(http_text, re, &a);
  }
}

#ifdef USEPCRE
BENCHMARK(DotMatchPCRE)->ThreadRange(1, NumCPUs());
#endif
BENCHMARK(DotMatchRE2)->ThreadRange(1, NumCPUs());

void ASCIIMatchPCRE(benchmark::State& state) {
  StringPiece a;
  PCRE re("(?-s)^([ -~]+)");
  for (auto _ : state) {
    PCRE::PartialMatch(http_text, re, &a);
  }
}

void ASCIIMatchRE2(benchmark::State& state) {
  StringPiece a;
  RE2 re("(?-s)^([ -~]+)");
  for (auto _ : state) {
    RE2::PartialMatch(http_text, re, &a);
  }
}

void ASCIIMatchRE2_LiuZhitao(benchmark::State& state) {
  std::ifstream in("re2/testing/text_re2_1KB.txt");
  std::stringstream buffer;
  buffer << in.rdbuf();
  std::string s = buffer.str();
  RE2 re("(?-s)^([ -~]+)");
  for (auto _ : state) {
    RE2::PartialMatch(s.substr(0, state.range(0)), re);
  }
  state.SetBytesProcessed(state.iterations() * state.range(0));
}

#ifdef USEPCRE
BENCHMARK(ASCIIMatchPCRE)->ThreadRange(1, NumCPUs());
#endif
BENCHMARK(ASCIIMatchRE2)->ThreadRange(1, NumCPUs());
BENCHMARK_RANGE(ASCIIMatchRE2_LiuZhitao, 8, 2<<9);

void FullMatchPCRE(benchmark::State& state, const char *regexp) {
  std::string s = RandomText(state.range(0));
  s += "ABCDEFGHIJ";
  PCRE re(regexp);
  for (auto _ : state) {
    CHECK(PCRE::FullMatch(s, re));
  }
  state.SetBytesProcessed(state.iterations() * state.range(0));
}

void FullMatchRE2(benchmark::State& state, const char *regexp) {
  std::string s = RandomText(state.range(0));
  s += "ABCDEFGHIJ";
  RE2 re(regexp, RE2::Latin1);
  for (auto _ : state) {
    CHECK(RE2::FullMatch(s, re));
  }
  state.SetBytesProcessed(state.iterations() * state.range(0));
}

void FullMatchRE2_LiuZhitao(benchmark::State& state, const char *regexp) {

  std::ifstream in("re2/testing/text_re2_1KB.txt");
  std::stringstream buffer;
  buffer << in.rdbuf();
  std::string s = buffer.str();
  RE2 re(regexp, RE2::Latin1);
  for (auto _ : state) {
    CHECK(RE2::FullMatch(s.substr(0, state.range(0)), re));
  }
  state.SetBytesProcessed(state.iterations() * state.range(0));
}

void Rure_Find_RE2(benchmark::State& state, const char *regexp)
{
  std::ifstream in("/home/freekeeper/Desktop/re2-rust/re2/testing/text_re2_1KB.txt");
  std::stringstream buffer;
  buffer << in.rdbuf();
  std::string s = buffer.str().substr(0, state.range(0));
  RE2 re(regexp);
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
  std::ifstream in("/home/freekeeper/Desktop/re2-rust/re2/testing/text_re2_1KB.txt");
  std::stringstream buffer;
  buffer << in.rdbuf();
  std::string s = buffer.str().substr(0, state.range(0));
  RE2 re(regexp);
  rure_error *err = rure_error_new();
  rure *re1 = rure_compile((const uint8_t *)regexp, strlen(regexp), RURE_DEFAULT_FLAGS, NULL, err);
  for (auto _ : state) {
    bool matched = rure_is_match(re1, (const uint8_t *)s.c_str(), strlen(s.c_str()), 0);
    CHECK(matched);
  }
  state.SetBytesProcessed(state.iterations() * state.range(0));
}

void Rure_Find_RE2_Bench1(benchmark::State& state)  { Rure_Find_RE2(state, "(?s).*"); }
void Rure_Find_RE2_Bench2(benchmark::State& state)  { Rure_Find_RE2(state, "(?s).*$"); }
void Rure_Find_RE2_Bench3(benchmark::State& state)  { Rure_Find_RE2(state, "(?s)((.*)()()($))"); }

BENCHMARK_RANGE(Rure_Find_RE2_Bench1, 2<<6, 2<<9);
BENCHMARK_RANGE(Rure_Find_RE2_Bench2, 2<<6, 2<<9);
BENCHMARK_RANGE(Rure_Find_RE2_Bench3, 2<<6, 2<<9);

void Rure_is_Match_RE2_Bench1(benchmark::State& state)  { Rure_is_Match_RE2(state, "(?s).*"); }
void Rure_is_Match_RE2_Bench2(benchmark::State& state)  { Rure_is_Match_RE2(state, "(?s).*$"); }
void Rure_is_Match_RE2_Bench3(benchmark::State& state)  { Rure_is_Match_RE2(state, "(?s)((.*)()()($))"); }

BENCHMARK_RANGE(Rure_is_Match_RE2_Bench1, 2<<6, 2<<9);
BENCHMARK_RANGE(Rure_is_Match_RE2_Bench2, 2<<6, 2<<9);
BENCHMARK_RANGE(Rure_is_Match_RE2_Bench3, 2<<6, 2<<9);


void FullMatch_DotStar_CachedRE2_LiuZhitao(benchmark::State& state)  { FullMatchRE2_LiuZhitao(state, "(?s).*"); }
void FullMatch_DotStarDollar_CachedRE2_LiuZhitao(benchmark::State& state)  { FullMatchRE2_LiuZhitao(state, "(?s).*$"); }
void FullMatch_DotStarCapture_CachedRE2_LiuZhitao(benchmark::State& state)  { FullMatchRE2_LiuZhitao(state, "(?s)((.*)()()($))"); }

BENCHMARK_RANGE(FullMatch_DotStar_CachedRE2_LiuZhitao, 8, 2<<9);
BENCHMARK_RANGE(FullMatch_DotStarDollar_CachedRE2_LiuZhitao, 8, 2<<9);
BENCHMARK_RANGE(FullMatch_DotStarCapture_CachedRE2_LiuZhitao, 8, 2<<9);

void FullMatch_DotStar_CachedPCRE(benchmark::State& state) { FullMatchPCRE(state, "(?s).*"); }
void FullMatch_DotStar_CachedRE2(benchmark::State& state)  { FullMatchRE2(state, "(?s).*"); }

void FullMatch_DotStarDollar_CachedPCRE(benchmark::State& state) { FullMatchPCRE(state, "(?s).*$"); }
void FullMatch_DotStarDollar_CachedRE2(benchmark::State& state)  { FullMatchRE2(state, "(?s).*$"); }

void FullMatch_DotStarCapture_CachedPCRE(benchmark::State& state) { FullMatchPCRE(state, "(?s)((.*)()()($))"); }
void FullMatch_DotStarCapture_CachedRE2(benchmark::State& state)  { FullMatchRE2(state, "(?s)((.*)()()($))"); }

#ifdef USEPCRE
BENCHMARK_RANGE(FullMatch_DotStar_CachedPCRE, 8, 2<<20);
#endif
BENCHMARK_RANGE(FullMatch_DotStar_CachedRE2,  8, 2<<20);

#ifdef USEPCRE
BENCHMARK_RANGE(FullMatch_DotStarDollar_CachedPCRE, 8, 2<<20);
#endif
BENCHMARK_RANGE(FullMatch_DotStarDollar_CachedRE2,  8, 2<<20);

#ifdef USEPCRE
BENCHMARK_RANGE(FullMatch_DotStarCapture_CachedPCRE, 8, 2<<20);
#endif

BENCHMARK_RANGE(FullMatch_DotStarCapture_CachedRE2,  8, 2<<20);

}  // namespace re2
