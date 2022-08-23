# re2-rust

a compatible RE2 API(
2021-11-01)  by calling Rust library [regex(rure)](https://github.com/rust-lang/regex)


``` Shell
$ git clone https://gitee.com/openeuler/re2-rust.git
$ cd re2-rust
```


###  安装rure库
使用rust-lang/regex的版本号为1.6.0
安装过程如下：
``` Shell
$ git clone -b 1.6.0 --depth=1 https://github.com/rust-lang/regex.git
$ cd regex
$ patch -p1 < ../patch/rure.patch
$ cd regex-capi
$ cargo build --verbose
```
对于编译完成的`librure.a`和`librure.so`文件需要进行手工安装
``` Shell
# note: go to re2-rust root directory first.
$ cd ../../

# put the librure.a and librure.so into the /usr/lib

$ sudo cp regex/target/debug/librure.a /usr/lib
$ sudo cp regex/target/debug/librure.so /usr/lib

# make librure.so visible
$ sudo ldconfig
```
手工安装rure.h文件
``` Shell
# copy the rure.h
$ sudo cp regex/regex-capi/include/rure.h /usr/include
```

使用rure库:
使用regex/regex-capi/ctest/目录下的 test.c文件进行测试
``` Shell
$ gcc test.c -o test -lrure
$ ./test
```

### 编译、安装re2-rust

``` Shell
$ make
$ sudo make install
```

# Links

* https://github.com/rust-lang/regex
* https://gitee.com/src-openeuler/re2
* https://github.com/google/re2



