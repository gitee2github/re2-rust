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
 * Create: 2022-11-25
 * Description: The business logic implementation layer uses pure rust.
 ******************************************************************************/
use regex::bytes::RegexBuilder;
use regex::bytes::RegexSetBuilder;
 fn rure_compile_internal(
    pat: &str,
    flags: u32,
) -> RegexBuilder {
    let mut builder = bytes::RegexBuilder::new(pat);
    builder.case_insensitive(flags & RURE_FLAG_CASEI > 0);
    builder.multi_line(flags & RURE_FLAG_MULTI > 0);
    builder.dot_matches_new_line(flags & RURE_FLAG_DOTNL > 0);
    builder.swap_greed(flags & RURE_FLAG_SWAP_GREED > 0);
    builder.ignore_whitespace(flags & RURE_FLAG_SPACE > 0);
    builder.unicode(flags & RURE_FLAG_UNICODE > 0);
    builder
}

fn rure_compile_set_internal(
    pats: Vec<&str>,
    flags: u32,
) -> RegexSetBuilder {
    let mut builder = bytes::RegexSetBuilder::new(pats);

    builder.case_insensitive(flags & RURE_FLAG_CASEI > 0);
    builder.multi_line(flags & RURE_FLAG_MULTI > 0);
    builder.dot_matches_new_line(flags & RURE_FLAG_DOTNL > 0);
    builder.swap_greed(flags & RURE_FLAG_SWAP_GREED > 0);
    builder.ignore_whitespace(flags & RURE_FLAG_SPACE > 0);
    builder.unicode(flags & RURE_FLAG_UNICODE > 0);
    builder
}

fn rure_set_matches_internal(
    re: &RegexSet,
    matches: &mut [bool],
    haystack: &[u8],
    start: size_t,
) -> bool {
    // read_matches_at isn't guaranteed to set non-matches to false
    for item in matches.iter_mut() {
        *item = false;
    }
    re.read_matches_at(matches, haystack, start)
}

fn rure_replace_internal(re: &RegexUnicode, haystack: &[u8], rewrite: &[u8]) -> *const u8 {
    let haystack = match str::from_utf8(haystack) {
        Ok(haystack) => haystack,
        Err(_err) => {
            return ptr::null();
        }
    };
    let rewrite = match str::from_utf8(rewrite) {
        Ok(rewrite) => rewrite,
        Err(_err) => {
            return ptr::null();
        }
    };
    let result = re.replace(haystack, rewrite).into_owned();
    let c_esc_pat = match CString::new(result) {
        Ok(val) => val,
        Err(err) => {
            println!("{}", err);
            return ptr::null();
        }
    };
    c_esc_pat.into_raw() as *const u8
}

fn rure_replace_all_internal(re: &RegexUnicode, haystack: &[u8], rewrite: &[u8]) -> *const u8 {
    let haystack = match str::from_utf8(haystack) {
        Ok(haystack) => haystack,
        Err(_err) => {
            return ptr::null();
        }
    };
    let rewrite = match str::from_utf8(rewrite) {
        Ok(rewrite) => rewrite,
        Err(_err) => {
            return ptr::null();
        }
    };
    let result = re.replace_all(haystack, rewrite).into_owned();
    let c_esc_pat = match CString::new(result) {
        Ok(val) => val,
        Err(err) => {
            println!("{}", err);
            return ptr::null();
        }
    };
    c_esc_pat.into_raw() as *const u8
}

fn rure_new_internal(pat: &[u8]) -> *const RegexBytes {
    let pat = match str::from_utf8(pat) {
        Ok(pat) => pat,
        Err(_err) => {
            return ptr::null();
        }
    };
    let exp = match bytes::Regex::new(pat) {
        Ok(val) => Box::into_raw(Box::new(val)),
        Err(_) => ptr::null(),
    };
    exp as *const RegexBytes
}

fn rure_max_submatch_internal(text: &[u8]) -> i32 {
    let mut max: i32 = 0;
    let mut flag = 0;
    let zero_number = '0' as i32;
    let rewrite = std::str::from_utf8(text).unwrap();
    for s in rewrite.chars() {
        if s == '\\' {
            flag = 1;
            continue;
        }
        if s.is_ascii_digit() && flag == 1 {
            let max_ = s as i32 - zero_number;
            if max_ > max {
                max = max_;
            }
            flag = 0;
        }
    }
    max
}

fn rure_check_rewrite_string_internal(text: &[u8], cap_num: i32) -> bool {
    let s = std::str::from_utf8(text).unwrap();
    let mut max_token = -1;
    let chars = s.chars().collect::<Vec<char>>();
    let mut i = 0;
    while i < chars.len() {
        if chars[i] != '\\' {
            i += 1;
            continue;
        }
        i += 1;

        if i == chars.len() {
            println!("Rewrite schema error: '\\' not allowed at end.");
            return false;
        }
        // i += 1;
        if chars[i] == '\\' {
            i += 1;
            continue;
        }
        if !chars[i].is_ascii_digit() {
            println!("'\\' must be followed by a digit or '\\'.");
            return false;
        }

        let n = chars[i] as i32 - '0' as i32;
        println!("n = {}", n);
        i += 1;

        if n > max_token {
            max_token = n;
        }
    }

    if max_token > cap_num {
        return false;
    }
    return true;
}

fn rure_rewrite_str_convert_internal(rewrite: &[u8]) -> *const c_char {
    let rewrite_str = std::str::from_utf8(rewrite).unwrap();
    let rewrite_chars = rewrite_str.chars().collect::<Vec<char>>();
    let mut i = 0;
    let mut rewrite_rure_str = String::new();
    while i < rewrite_chars.len() {
        if rewrite_chars[i] != '\\' {
            rewrite_rure_str.push(rewrite_chars[i]);
            i += 1;
            continue;
        }
        {
            i += 1;
            let c = {
                if i < rewrite_chars.len() {
                    rewrite_chars[i]
                } else {
                    '#'
                }
            };
            if c.is_ascii_digit() {
                rewrite_rure_str.push_str("${");
                rewrite_rure_str.push(c);
                rewrite_rure_str.push('}');
            }
        }
        i += 1;
    }
    let rure_str = match CString::new(rewrite_rure_str) {
        Ok(val) => val,
        Err(err) => {
            println!("{}", err);
            return ptr::null();
        }
    };
    rure_str.into_raw() as *const c_char
}

fn rure_rewrite_internal(
    rewrite_str: &str,
    vecs_count: size_t,
    rure_vecs: Vec<&str>,
) -> *const c_char {
    let rewrite_chars = rewrite_str.chars().collect::<Vec<char>>();
    let mut i = 0;
    let mut out = String::new();
    while i < rewrite_chars.len() {
        if rewrite_chars[i] != '\\' {
            out.push(rewrite_chars[i]);
            i += 1;
            continue;
        }
        i += 1;
        let c = {
            if i < rewrite_chars.len() {
                rewrite_chars[i]
            } else {
                '~'
            }
        };
        // let n
        if c.is_ascii_digit() {
            let n = c as usize - '0' as usize;
            if n >= vecs_count {
                return ptr::null();
            }
            let elem = rure_vecs[n];
            if !elem.is_empty() {
                out.push_str(elem);
            }
            i += 1;
        } else if rewrite_chars[i] == '\\' {
            out.push('\\');
            i += 1;
        } else {
            return ptr::null();
        }
    }
    let out = match CString::new(out) {
        Ok(val) => val,
        Err(err) => {
            println!("{}", err);
            return ptr::null();
        }
    };
    out.into_raw() as *const c_char
}

fn rure_replace_count_internal(haystack: &[u8], re: &RegexUnicode) -> size_t {
    let mut count = 0;
    let haystack = str::from_utf8(haystack).unwrap();
    for _mat in re.find_iter(haystack) {
        count += 1;
    }
    count
}

/**
* 负责对字符集进行连接操作
*
*/
fn connection(str: &str, vec1: Vec<String>, vec2: Vec<char>) -> Vec<String> {
    let mut vec_tmp = Vec::new();
    if str.len() > 0 {
        for chars in vec2 {
            let s = format!("{}{}", str, chars);
            vec_tmp.push(s);
        }
    } else if vec1.len() == 0 {
        for elem in vec2 {
            vec_tmp.push(elem.to_string())
        }
    } else {
        for chars_i in vec1 {
            for chars_j in vec2.clone() {
                let s = format!("{}{}", chars_i, chars_j);
                vec_tmp.push(s);
            }
        }
    }
    vec_tmp
}

/**
*  (abc123|abc|ghi789|abc1234)
 3-abc
 6-abc123
 6-ghi789
 7-abc1234
* abc  abc123  ghi789  abc1234
*/
fn group_multiple_selection(str: &str, min_atoms_len: i32) -> Vec<String> {
    let mut str_tmp = String::new(); // 暂存atoms
    let mut atoms_tmp = Vec::new(); // 最终的atoms
    for elem in str.chars() {
        if elem == '(' {
            continue;
        }
        if elem == '|' || elem == ')' {
            if str_tmp.len() as i32 >= min_atoms_len {
                atoms_tmp.push(str_tmp.clone());
            }
            str_tmp.clear();
            continue;
        } else {
            str_tmp.push(elem);
        }
    }
    atoms_tmp.sort_by(|a, b| a.len().cmp(&b.len()));

    for i in 0..atoms_tmp.len() {
        let mut j = i + 1;
        while j < atoms_tmp.len() {
            if atoms_tmp[j].contains(atoms_tmp[i].as_str()) && !atoms_tmp[i].is_empty() {
                atoms_tmp.remove(j);
            } else {
                j += 1;
            }
        }
    }
    atoms_tmp
}

/**
* 处理
* a[a-c]a[zv]
* [abc]
* [a-c]+
*/

fn char_class_expansion(str: &str) -> Vec<char> {
    let mut flag_connect = 0;
    let mut atoms_chars = Vec::new();
    let mut vec_char = Vec::new();
    for elem in str.chars() {
        if elem == '[' || elem == ']' {
            continue;
        } else if elem.is_ascii_alphabetic() || elem.is_ascii_digit() {
            vec_char.push(elem);
        } else if elem == '-' {
            flag_connect = 1;
        }
    }
    if flag_connect == 1 {
        let x = vec_char[0];
        let y = vec_char[1];
        for elem in x..=y {
            atoms_chars.push(elem);
        }
    } else {
        atoms_chars = vec_char;
    }

    atoms_chars
}

fn my_compile(str: &str, min_atoms_len: i32) -> MyVec {
    let mut my_atoms = Vec::new(); // 所有的的原子
    let mut atoms_tmp_string = String::new(); // 暂时存储的字符串
    let mut vec_chars_con: Vec<String> = Vec::new();
    // 将所有的大写字符转换为小写
    let str = str.to_lowercase();
    let chars = str.chars().collect::<Vec<char>>();
    let mut i = 0;
    while i < chars.len() {
        // 处理分组括号
        if chars[i] == '(' {
            let group_start_post = i;
            while chars[i] != ')' {
                i += 1;
            }
            let group_end_post = i;
            let str_group = &str[group_start_post..group_end_post + 1];
            let vec = group_multiple_selection(str_group, min_atoms_len);

            let mut tmp_post_group = i;
            tmp_post_group += 1;
            if tmp_post_group >= chars.len() {
                if vec.len() != 0 {
                    // 右括号为自后一个字符的情况
                    for elem in vec {
                        my_atoms.push(Atoms {
                            atom: CString::new(elem).unwrap().into_raw(),
                        });
                    }
                }
                i += 1;
                continue;
            }
            if chars[tmp_post_group] == '.' && vec.len() != 0 {
                i += 1;
                for elem in vec {
                    my_atoms.push(Atoms {
                        atom: CString::new(elem).unwrap().into_raw(),
                    });
                }
            } else if chars[tmp_post_group] == '{' && vec.len() != 0 {
                for elem in vec {
                    my_atoms.push(Atoms {
                        atom: CString::new(elem).unwrap().into_raw(),
                    });
                }
            }
            i += 1;
            continue;
        }
        if chars[i] == '.' {
            if atoms_tmp_string.len() as i32 >= min_atoms_len && vec_chars_con.len() == 0 {
                my_atoms.push(Atoms {
                    atom: CString::new(atoms_tmp_string.clone()).unwrap().into_raw(),
                });
            }
            if vec_chars_con.len() > 0 && atoms_tmp_string.len() > 0 {
                for elems in vec_chars_con.clone() {
                    my_atoms.push(Atoms {
                        atom: CString::new(format!("{}{}", elems.clone(), atoms_tmp_string))
                            .unwrap()
                            .into_raw(),
                    });
                }
                vec_chars_con.clear();
            }
            atoms_tmp_string.clear();
            i += 1;
            continue;
        }

        if chars[i] == '*' || chars[i] == '+' {
            i += 1;
            continue;
        }

        // 处理多个字符集
        if chars[i] == '[' {
            let start_post = i;
            while chars[i] != ']' {
                i += 1;
            }
            let mut plus_tmp = i;
            plus_tmp += 1;
            if plus_tmp < chars.len() && chars[plus_tmp] == '+' {
                if atoms_tmp_string.len() as i32 >= min_atoms_len {
                    my_atoms.push(Atoms {
                        atom: CString::new(atoms_tmp_string.clone()).unwrap().into_raw(),
                    });
                    atoms_tmp_string.clear();
                    i += 2;
                    continue;
                }
            }
            let str_char_set = &str[start_post..plus_tmp];
            if atoms_tmp_string.len() > 0 && vec_chars_con.len() > 0 {
                for elem in vec_chars_con.clone() {
                    vec_chars_con.push(format!("{}{}", elem, atoms_tmp_string));
                }
                atoms_tmp_string.clear();
            }
            let atoms_tmp = char_class_expansion(str_char_set);
            vec_chars_con = connection(atoms_tmp_string.as_str(), vec_chars_con, atoms_tmp);
            atoms_tmp_string.clear();

            if i == chars.len() - 1 && vec_chars_con.len() > 0 {
                for elem in vec_chars_con.clone() {
                    if elem.len() as i32 >= min_atoms_len {
                        my_atoms.push(Atoms {
                            atom: CString::new(elem).unwrap().into_raw(),
                        });
                    }
                }
            }
            i += 1;
            continue;
        }

        if chars[i] == '{' {
            while chars[i] != '}' {
                i += 1;
            }
            i += 1;
            continue;
        }

        if chars[i] == '\\' {
            if atoms_tmp_string.len() as i32 >= min_atoms_len {
                my_atoms.push(Atoms {
                    atom: CString::new(atoms_tmp_string.clone()).unwrap().into_raw(),
                });
                atoms_tmp_string.clear();
            }
            i += 2;
            continue;
        }

        if chars[i] != '+' {
            atoms_tmp_string.push(chars[i]);
        }

        if i == chars.len() - 1 && atoms_tmp_string.len() as i32 >= min_atoms_len {
            my_atoms.push(Atoms {
                atom: CString::new(atoms_tmp_string.clone()).unwrap().into_raw(),
            });
            atoms_tmp_string.clear();
        }
        i += 1;
    }
    let mut a = my_atoms.into_boxed_slice();
    let data = a.as_mut_ptr();
    let len = a.len() as i32;
    std::mem::forget(a);
    MyVec { data, len }
}
