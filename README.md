# re2-rust

## re2-rust介绍
re2-rust是用来兼容RE2 API[(version 2021-11-01)](https://github.com/google/re2/tree/2021-11-01)的项目，通过调用[Rust正则表达式库](https://github.com/rust-lang/regex)进行实现。re2-rust的功能与原本RE2基本保持一致。

re2-rust保留了re2中的对外的接口，分别在re2.h、set.h和filtered_re2.h中。

re2.h中的接口可以实现正则表达式的匹配、查找和替换的功能；set.h中的接口可以同时处理多组正则表达式；filtered_re.h中的接口提供了一种预过滤机制，有助于减少需要实际搜索的regexp的数量。这些接口再调用Rust正则库中提供的接口对用户传递过来的数据进行处理，最后再把结果进行返回。


## 编译、安装re2-rust
``` Shell
$ git clone https://gitee.com/openeuler/re2-rust.git
$ cd re2-rust
```
**使用openEuler 22.03-LTS**

``` Shell
dnf install git
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh
source "$HOME/.cargo/env"
dnf install g++
git clone https://gitee.com/openeuler/re2-rust.git
cd re2-rust
make
make install
make test
g++ testinstall.cc -o testinstall -lre2
./testinstall
```

**使用Ubuntu 20.04**

``` Shell
$ make
$ sudo make install
$ make test
$ g++ testinstall.cc -o testinstall -lre2
$ ./testinstall
```

## 性能测试
RE2-Rust项目中只需要对re2目录下filtered_re2.h、re2.h、set.h文件中声明的部分函数进行性能测试，而filtered_re2.h中的主要函数是通过调用re2.h中的`PartialMatch()`函数实现的，所以下面只对re2.h和set.h文件中主要函数进行性能测试。相关的性能测试代码详见regexp_benchmark.cc文件。
re2.h文件中相关函数的性能测试：
我们对re2.h对外接口中的`FullMatch()`、`PartialMatch()`、`FindAndConsume()`三个函数进行了测试，下面表格中的re2-c++和re2-rust分别通过上述三个函数测试了表格中的八个正则表达式，但由于这三个函数的本质是调用了`RE2::DoMatch()`函数，所以在表格中不对上面三个函数进行区分。下面表格是regexp_benchmark.cc中一些正则表达式在text_re2_1KB.txt文本下的执行时间。

| 正则表达式  （含义）                    | RE2-C++       | RE2-Rust      | PCRE         | Regex         |
| --------------------------------------- | ------------- | ------------- | ------------ | ------------- |
| `“”`                                    | 339 ns/iter   | 213 ns/iter   | 133 ns/iter  | 54 ns/iter    |
| **空**                                  | 3019.80 MB/s  | 4785.82 MB/s  | 7653.53 MB/s | 18890.09 MB/s |
| `"abcdefg"`                             | 820 ns/iter   | 259 ns/iter   | 1686 ns/iter | 97 ns/iter    |
| **匹配abcdefg字符串**                   | 1248.70 MB/s  | 3951.78 MB/s  | 607.26 MB/s  | 10507.00 MB/s |
| `"(?-s)^(?:GET|POST) +([^ ]+) HTTP"`    | 343 ns/iter   | 246 ns/iter   | 147 ns/iter  | 92 ns/iter    |
| **匹配HTTP请求报文格式**                | 2982.79 MB/s  | 4157.21 MB/s  | 6932.47 MB/s | 11096.98 MB/s |
| `"(?-s)^(.+)"`                          | 542 ns/iter   | 212 ns/iter   | 203 ns/iter  | 56 ns/iter    |
| **匹配行首连续出现一次以上的字符**      | 1886.72 MB/s  | 4807.80 MB/s  | 5031.92 MB/s | 18062.85 MB/s |
| ` "(?-s)^([ -~]+)"`                     | 557 ns/iter   | 217 ns/iter   | 190 ns/iter  | 59 ns/iter    |
| **匹配行首连续出现一次以上的ASCII字符** | 1835.28 MB/s  | 4715.87 MB/s  | 5365.38 MB/s | 17188.64 MB/s |
| `"(?s).*"`                              | 349 ns/iter   | 21223 ns/iter | 154 ns/iter  | 2588 ns/iter  |
| **匹配任意字符**                        | 2929.63 MB/s  | 48.25 MB/s    | 6640.02 MB/s | 395.62 MB/s   |
| `"(?s).*$"`                             | 11401 ns/iter | 19678 ns/iter | 159 ns/iter  | 2468 ns/iter  |
| **匹配任意字符**                        | 89.81 MB/s    | 52.04 MB/s    | 6415.22 MB/s | 414.86 MB/s   |
| `"(?s)((.*)()()($))"`                   | 11179 ns/iter | 19873 ns/iter | 260 ns/iter  | 2488 ns/iter  |
| **匹配任意字符**                        | 91.59 MB/s    | 51.53 MB/s    | 3937.18 MB/s | 411.54 MB/s   |

注：(?s)表示单行模式

set.h文件中相关函数的性能测试：

可以看到，set.h中主要是下面的函数接口有匹配功能：

`bool Match(const StringPiece& text, std::vector* v) const;`

上述函数功能为同一文本可同时匹配多个正则表达式，并将匹配到的结果保存到向量v中，若传入的v为空则表示不需要返回匹配结果。

我们使用的待匹配文本还是text_re2_1KB.txt中的数据，同时匹配五个正则表达式，分别是`"(?s).*"`、`"(?s).*"`、`"(?s)((.*)()()("`、`"(?*s*)((.∗)()()())"`、`"hwx"`、`"ldi"`。

由于对于锚点为`RE2::UNANCHORED`、`RE2::ANCHOR_BOTH`、`RE2::ANCHOR_START`三种不同情况已经在`RE2::Set::Add()`已经进行了处理，所以对锚点三种不同情况的处理并不计算在匹配时间。为方便RE2-Rust与RE2-C++、Regex进行性能对比分析，我们采用锚点为RE2::UNANCHORED进行性能对比，详细性能评测代码见regexp_benchmark.cc文件中`Set_Match_UNANCHORED_RE2()`和`Set_Match_UNANCHORED_NULL_RE2()`函数。下面是set.h文件中`RE2::Set::Match()`在RE2-C++、RE2-Rust、Regex三种不同正则表达式框架下的性能对比结果（PCRE不支持同时匹配多个正则表达式）：

|             | RE2-C++      | RE2-Rust     | Regex        |
| ----------- | ------------ | ------------ | ------------ |
| **V为空**   | 1716 ns/iter | 383 ns/iter  | 18 ns/iter   |
|             | 596.67 MB/s  | 2671.52 MB/s | 56944 MB/s   |
| **V不为空** | 8231 ns/iter | 535 ns/iter  | 6686 ns/iter |
|             | 124.40 MB/s  | 1910.52 MB/s | 153 MB/s     |

另外我们采用第三方正则表达式测试框架regex-performance，通过一些指定的正则表达式，对主流的正则表达式库进行了评测（测试详情可见https://gitee.com/openeuler/re2-rust/blob/master/test-results.txt），得到了如下结果：
```
Total Results:
[      ctre] time:  4010462.7 ms, score:      6 points,
[     boost] time:  2010606.4 ms, score:      3 points,
[    cppstd] time:  3118716.6 ms, score:      0 points,
[      pcre] time:  32853.7 ms, score:      4 points,
[  pcre-dfa] time:  22640.6 ms, score:      5 points,
[  pcre-jit] time:    851.1 ms, score:     45 points,
[       re2] time:    516.4 ms, score:     42 points,
[      onig] time:  43648.0 ms, score:      6 points,
[       tre] time:   9306.2 ms, score:      0 points,
[     hscan] time:    382.4 ms, score:     69 points,
[      yara] time:  2001920.1 ms, score:     13 points,
[  re2-rust] time:   4937.7 ms, score:     44 points,
[rust_regex] time:   4790.2 ms, score:     56 points,
[rust_regrs] time:  47772.1 ms, score:      6 points,
```
从以上测试结果看re2-rust评分比re2略高，但是耗时re2-rust比re2增加很多，通过仔细分析发现正则表达式`'[a-q][^u-z]{13}x'`耗时特别高4280.7 - 130.5 = 4150.2 ms，另外`'\b\w+nn\b'`耗时322.6 - 23.9 = 298.7，除去这两个异常测试项外的16个测试项耗时re2-rust：334.4 ms vs. re2: 362 ms ，也就是说re2-rust在大多数情况下性能比re2要好。
从测试耗时看re2-rust和rust_regex两者相差3%（多次测评结果看两者差距上下浮动5%以内），总体看re2-rust和rust_regex性能基本一致。
综合对比可知：

1.  RE2-Rust在大部分测试用例下性能优于RE2-C++，而在涉及到捕获组会差于RE2-C++，原因可见https://github.com/rust-lang/regex/discussions/903
2.  RE2-rust和Regex性能大致相当，但是由于RE2-Rust是调用了Regex的对外的C接口，所以RE2-rust会比Regex多了函数调用开销、特殊处理、错误判断等开销，故RE2-Rust性能会略低于Regex
3.  RE2-Rust支持多行模式，但不支持同名的捕获组
4.  RE2-Rust比RE2-C++支持更少的转义字符，比如`”\C”`


# Links

* https://github.com/rust-lang/regex
* https://gitee.com/src-openeuler/re2
* https://github.com/google/re2
* https://gitee.com/mengning997/regex-performance for re2-rust
