// #include "re2/sparse_array.h"
#include "regex-capi/include/rure.h"
namespace re2 {
// #include "re2/prog.h"
// Compiled form of regexp program.
 class Prog {
   //rure 更名为 Prog
};
 
// #include "re2/regexp.h"
class Regexp {
 public:

  // Flags for parsing.  Can be ORed together.
  enum ParseFlags {
    NoParseFlags  = 0,
    FoldCase      = 1<<0,   // Fold case during matching (case-insensitive).
    Literal       = 1<<1,   // Treat s as literal string instead of a regexp.
    ClassNL       = 1<<2,   // Allow char classes like [^a-z] and \D and \s
                            // and [[:space:]] to match newline.
    DotNL         = 1<<3,   // Allow . to match newline.
    MatchNL       = ClassNL | DotNL,
    OneLine       = 1<<4,   // Treat ^ and $ as only matching at beginning and
                            // end of text, not around embedded newlines.
                            // (Perl's default)
    Latin1        = 1<<5,   // Regexp and text are in Latin1, not UTF-8.
    NonGreedy     = 1<<6,   // Repetition operators are non-greedy by default.
    PerlClasses   = 1<<7,   // Allow Perl character classes like \d.
    PerlB         = 1<<8,   // Allow Perl's \b and \B.
    PerlX         = 1<<9,   // Perl extensions:
                            //   non-capturing parens - (?: )
                            //   non-greedy operators - *? +? ?? {}?
                            //   flag edits - (?i) (?-i) (?i: )
                            //     i - FoldCase
                            //     m - !OneLine
                            //     s - DotNL
                            //     U - NonGreedy
                            //   line ends: \A \z
                            //   \Q and \E to disable/enable metacharacters
                            //   (?P<name>expr) for named captures
                            //   \C to match any single byte
    UnicodeGroups = 1<<10,  // Allow \p{Han} for Unicode Han group
                            //   and \P{Han} for its negation.
    NeverNL       = 1<<11,  // Never match NL, even if the regexp mentions
                            //   it explicitly.
    NeverCapture  = 1<<12,  // Parse all parens as non-capturing.

    // As close to Perl as we can get.
    LikePerl      = ClassNL | OneLine | PerlClasses | PerlB | PerlX |
                    UnicodeGroups,

    // Internal use only.
    WasDollar     = 1<<13,  // on kRegexpEndText: was $ in regexp text
    AllParseFlags = (1<<14)-1,
  };
};
};