# re2-rust

a compatible RE2 API(
2021-11-01)  by calling Rust library [regex(rure)](https://github.com/rust-lang/regex)


``` Shell
$ git clone https://gitee.com/openeuler/re2-rust.git
$ cd re2-rust
```

### 编译、安装re2-rust

``` Shell
$ mkdir build && cd build
$ cmake ..
$ make
$ sudo make install
$ g++ ../testinstall.cc -o testinstall -lre2
$ ./testinstall
```

or


``` Shell
$ make
$ sudo make install
```
使用rure库:

``` Shell
$ cd regex-capi/ctest/
$ gcc test.c -o test -lrure
$ ./test
```
## Test Rusults

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
从测试结果看re2-rust评分比re2略高，但是耗时re2-rust比re2增加很多，通过仔细分析发现正则表达式'[a-q][^u-z]{13}x'耗时特别高4280.7 - 130.5 = 4150.2 ms，另外'\b\w+nn\b'耗时322.6 - 23.9 = 298.7，除去这两个异常测试项外的16个测试项耗时re2-rust：334.4 ms vs. re2:362 ms ，也就是说re2-rust在大多数情况下性能比re2要好。

从测试耗时看re2-rust和rust_regex两者相差3%（多次测评结果看两者差距上下浮动5%以内），总体看re2-rust和rust_regex性能基本一致。

测试采用第三方正则表达式测试框架regex-performance，测试详情见test-results.txt

# Links

* https://github.com/rust-lang/regex
* https://gitee.com/src-openeuler/re2
* https://github.com/google/re2



