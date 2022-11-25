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
 * Description: Rure is a C API to Rust's regex library.
 ******************************************************************************/
 #[macro_use]
 mod error;
 pub use crate::error::*;

 use std::ffi::{CStr, CString};
 use std::ops::Deref;
 use std::ptr;
 use std::slice;
 use std::str;
 
 use libc::{c_char, size_t};
 use regex::bytes::CaptureLocations;
 use regex::{bytes, Regex};
 
 use crate::error::{Error, ErrorKind};
 use std::io;
 use std::io::Write;
 use std::process::abort;
 
 include!("lib_internal.rs");
 
 const RURE_FLAG_CASEI: u32 = 1 << 0;
 const RURE_FLAG_MULTI: u32 = 1 << 1;
 const RURE_FLAG_DOTNL: u32 = 1 << 2;
 const RURE_FLAG_SWAP_GREED: u32 = 1 << 3;
 const RURE_FLAG_SPACE: u32 = 1 << 4;
 const RURE_FLAG_UNICODE: u32 = 1 << 5;
 const RURE_DEFAULT_FLAGS: u32 = RURE_FLAG_UNICODE;
 
 pub struct RegexBytes {
     re: bytes::Regex,
     // capture_names: HashMap<String, i32>,
 }
 
 pub struct RegexUnicode {
     re: Regex,
 }
 
 pub struct Options {
     size_limit: usize,
     dfa_size_limit: usize,
 }
 
 // The `RegexSet` is not exposed with option support or matching at an
 // arbitrary position with a crate just yet. To circumvent this, we use
 // the `Exec` structure directly.
 pub struct RegexSet {
     re: bytes::RegexSet,
 }
 
 #[repr(C)]
 pub struct rure_match {
     pub start: size_t,
     pub end: size_t,
 }
 
 pub struct Captures(bytes::Locations);
 
 pub struct IterCaptureNames {
     capture_names: bytes::CaptureNames<'static>,
     name_ptrs: Vec<*mut c_char>,
 }
 
 #[repr(C)]
 pub struct Atoms {
     atom: *mut c_char,
 }
 
 #[repr(C)]
 pub struct MyVec {
     data: *mut Atoms,
     len: i32,
 }
 
 impl Deref for RegexBytes {
     type Target = bytes::Regex;
     fn deref(&self) -> &bytes::Regex {
         &self.re
     }
 }
 
 impl Deref for RegexUnicode {
     type Target = Regex;
     fn deref(&self) -> &Regex {
         &self.re
     }
 }
 
 impl Deref for RegexSet {
     type Target = bytes::RegexSet;
     fn deref(&self) -> &bytes::RegexSet {
         &self.re
     }
 }
 
 impl Default for Options {
     fn default() -> Options {
         Options {
             size_limit: 10 * (1 << 20),
             dfa_size_limit: 2 * (1 << 20),
         }
     }
 }
 
 #[no_mangle]
 extern "C" fn rure_compile_must(pattern: *const c_char) -> *const RegexBytes {
     let len = unsafe { CStr::from_ptr(pattern).to_bytes().len() };
     let pat = pattern as *const u8;
     let mut err = Error::new(ErrorKind::None);
     let re = rure_compile(pat, len, RURE_DEFAULT_FLAGS, ptr::null(), &mut err);
     if err.is_err() {
         let _ = writeln!(&mut io::stderr(), "{}", err);
         let _ = writeln!(&mut io::stderr(), "aborting from rure_compile_must");
         abort()
     }
     re
 }
 
 #[no_mangle]
 extern "C" fn rure_compile(
     pattern: *const u8,
     length: size_t,
     flags: u32,
     options: *const Options,
     error: *mut Error,
 ) -> *const RegexBytes {
     let pat = unsafe { slice::from_raw_parts(pattern, length) };
     let pat = match str::from_utf8(pat) {
         Ok(pat) => pat,
         Err(err) => unsafe {
             if !error.is_null() {
                 *error = Error::new(ErrorKind::Str(err));
             }
             return ptr::null();
         },
     };
     rure_compile_internal(pat, flags, options, error)
 }
 
 #[no_mangle]
 extern "C" fn rure_free(re: *const RegexBytes) {
     unsafe {
         drop(Box::from_raw(re as *mut Regex));
     }
 }
 
 #[no_mangle]
 extern "C" fn rure_is_match(
     re: *const RegexBytes,
     haystack: *const u8,
     len: size_t,
     _start: size_t,
 ) -> bool {
     let re = unsafe { &*re };
     let haystack = unsafe { slice::from_raw_parts(haystack, len) };
     re.is_match(haystack)
 }
 
 #[no_mangle]
 extern "C" fn rure_find(
     re: *const RegexBytes,
     haystack: *const u8,
     len: size_t,
     start: size_t,
     match_info: *mut rure_match,
 ) -> bool {
     let re = unsafe { &*re };
     let haystack = unsafe { slice::from_raw_parts(haystack, len) };
     rure_find_internal(re, haystack, start, match_info)
 }
 
 #[no_mangle]
 extern "C" fn rure_find_captures(
     re: *const RegexBytes,
     haystack: *const u8,
     len: size_t,
     start: size_t,
     captures: *mut Captures,
 ) -> bool {
     let re = unsafe { &*re };
     let haystack = unsafe { slice::from_raw_parts(haystack, len) };
     let slots = unsafe { &mut (*captures).0 };
     re.read_captures_at(slots, haystack, start).is_some()
 }
 
 #[no_mangle]
 extern "C" fn rure_iter_capture_names_new(re: *const RegexBytes) -> *mut IterCaptureNames {
     let re = unsafe { &*re };
     Box::into_raw(Box::new(IterCaptureNames {
         capture_names: re.re.capture_names(),
         name_ptrs: Vec::new(),
     }))
 }
 
 #[no_mangle]
 extern "C" fn rure_iter_capture_names_free(it: *mut IterCaptureNames) {
     unsafe {
         let it = &mut *it;
         while let Some(ptr) = it.name_ptrs.pop() {
             drop(CString::from_raw(ptr));
         }
         drop(Box::from_raw(it));
     }
 }
 
 #[no_mangle]
 extern "C" fn rure_iter_capture_names_next(
     it: *mut IterCaptureNames,
     capture_name: *mut *mut c_char,
 ) -> bool {
     if capture_name.is_null() {
         return false;
     }
     let it = unsafe { &mut *it };
     let cn = match it.capture_names.next() {
         // Top-level iterator ran out of capture groups
         None => return false,
         Some(val) => {
             match val {
                 // inner Option didn't have a name
                 None => "",
                 Some(name) => name,
             }
         }
     };
     unsafe {
         let cs = match CString::new(cn.as_bytes()) {
             Result::Ok(val) => val,
             Result::Err(_) => return false,
         };
         let ptr = cs.into_raw();
         it.name_ptrs.push(ptr);
         *capture_name = ptr;
     }
     true
 }
 
 #[no_mangle]
 extern "C" fn rure_captures_new(re: *const RegexBytes) -> *mut Captures {
     let re = unsafe { &*re };
     let captures = Captures(re.locations());
     Box::into_raw(Box::new(captures))
 }
 
 #[no_mangle]
 extern "C" fn rure_captures_free(captures: *const Captures) {
     unsafe {
         drop(Box::from_raw(captures as *mut Captures));
     }
 }
 
 #[no_mangle]
 extern "C" fn rure_captures_at(
     captures: *const Captures,
     i: size_t,
     match_info: *mut rure_match,
 ) -> bool {
     let locs = unsafe { &(*captures).0 };
     rure_captures_at_internal(locs, i, match_info)
 }
 
 #[no_mangle]
 extern "C" fn rure_captures_len(captures: *const Captures) -> size_t {
     unsafe { (*captures).0.len() }
 }
 
 #[no_mangle]
 extern "C" fn rure_compile_set(
     patterns: *const *const u8,
     patterns_lengths: *const size_t,
     patterns_count: size_t,
     flags: u32,
     options: *const Options,
     error: *mut Error,
 ) -> *const RegexSet {
     let (raw_pats, raw_patsl) = unsafe {
         (
             slice::from_raw_parts(patterns, patterns_count),
             slice::from_raw_parts(patterns_lengths, patterns_count),
         )
     };
     rure_compile_set_internal(raw_pats, raw_patsl, patterns_count, flags, options, error)
 }
 
 #[no_mangle]
 extern "C" fn rure_set_free(re: *const RegexSet) {
     unsafe {
         drop(Box::from_raw(re as *mut RegexSet));
     }
 }
 
 #[no_mangle]
 extern "C" fn rure_set_is_match(
     re: *const RegexSet,
     haystack: *const u8,
     len: size_t,
     start: size_t,
 ) -> bool {
     let re = unsafe { &*re };
     let haystack = unsafe { slice::from_raw_parts(haystack, len) };
     re.is_match_at(haystack, start)
 }
 
 #[no_mangle]
 extern "C" fn rure_set_matches(
     re: *const RegexSet,
     haystack: *const u8,
     len: size_t,
     start: size_t,
     matches: *mut bool,
 ) -> bool {
     let re = unsafe { &*re };
     let matches = unsafe { slice::from_raw_parts_mut(matches, re.len()) };
     let haystack = unsafe { slice::from_raw_parts(haystack, len) };
 
     rure_set_matches_internal(re, matches, haystack, start)
 }
 
 #[no_mangle]
 extern "C" fn rure_set_len(re: *const RegexSet) -> size_t {
     unsafe { (*re).len() }
 }
 
 #[no_mangle]
 extern "C" fn rure_escape_must(pattern: *const c_char) -> *const c_char {
     let len = unsafe { CStr::from_ptr(pattern).to_bytes().len() };
     let pat = pattern as *const u8;
     let mut err = Error::new(ErrorKind::None);
     let esc = rure_escape(pat, len, &mut err);
     if err.is_err() {
         println!("{}", "aborting from rure_escape_must");
         let _ = writeln!(&mut io::stderr(), "{}", err);
         let _ = writeln!(&mut io::stderr(), "aborting from rure_escape_must");
         abort()
     }
     esc
 }
 
 /// A helper function that implements fallible escaping in a way that returns
 /// an error if escaping failed.
 ///
 /// This should ideally be exposed, but it needs API design work. In
 /// particular, this should not return a C string, but a `const uint8_t *`
 /// instead, since it may contain a NUL byte.
 fn rure_escape(pattern: *const u8, length: size_t, error: *mut Error) -> *const c_char {
     let pat: &[u8] = unsafe { slice::from_raw_parts(pattern, length) };
     let str_pat = match str::from_utf8(pat) {
         Ok(val) => val,
         Err(err) => unsafe {
             if !error.is_null() {
                 *error = Error::new(ErrorKind::Str(err));
             }
             return ptr::null();
         },
     };
     let esc_pat = regex::escape(str_pat);
     let c_esc_pat = match CString::new(esc_pat) {
         Ok(val) => val,
         Err(err) => unsafe {
             if !error.is_null() {
                 *error = Error::new(ErrorKind::Nul(err));
             }
             return ptr::null();
         },
     };
     c_esc_pat.into_raw() as *const c_char
 }
 
 #[no_mangle]
 extern "C" fn rure_cstring_free(s: *mut c_char) {
     unsafe {
         drop(CString::from_raw(s));
     }
 }
 
 #[no_mangle]
 extern "C" fn rure_replace(
     re: *const RegexUnicode,
     haystack: *const u8,
     len_h: size_t,
     rewrite: *const u8,
     len_r: size_t,
 ) -> *const u8 {
     let re = unsafe { &*re };
     let haystack = unsafe { slice::from_raw_parts(haystack, len_h) };
     let rewrite = unsafe { slice::from_raw_parts(rewrite, len_r) };
     rure_replace_internal(re, haystack, rewrite)
 }
 
 #[no_mangle]
 extern "C" fn rure_replace_all(
     re: *const RegexUnicode,
     haystack: *const u8,
     len_h: size_t,
     rewrite: *const u8,
     len_r: size_t,
 ) -> *const u8 {
     let re = unsafe { &*re };
     let haystack = unsafe { slice::from_raw_parts(haystack, len_h) };
     let rewrite = unsafe { slice::from_raw_parts(rewrite, len_r) };
     rure_replace_all_internal(re, haystack, rewrite)
 }
 
 /*
  *  Simple way to use regex
  */
 
 #[no_mangle]
 extern "C" fn rure_new(pattern: *const u8, length: size_t) -> *const RegexBytes {
     let pat = unsafe { slice::from_raw_parts(pattern, length) };
     rure_new_internal(pat)
 }
 
 #[no_mangle]
 extern "C" fn rure_consume(
     re: *const RegexBytes,
     haystack: *const u8,
     len: size_t,
     match_info: *mut rure_match,
 ) -> bool {
     let exp = unsafe { &*re };
     let haystack = unsafe { slice::from_raw_parts(haystack, len) };
     rure_consume_internal(exp, haystack, match_info)
 }
 
 #[no_mangle]
 extern "C" fn rure_max_submatch(rewrite: *const c_char) -> i32 {
     let len = unsafe { CStr::from_ptr(rewrite).to_bytes().len() };
     let pat = rewrite as *const u8;
     let text = unsafe { slice::from_raw_parts(pat, len) };
 
     rure_max_submatch_internal(text)
 }
 
 #[no_mangle]
 extern "C" fn rure_check_rewrite_string(rewrite: *const c_char, cap_num: i32) -> bool {
     let len = unsafe { CStr::from_ptr(rewrite).to_bytes().len() };
     let pat = rewrite as *const u8;
     let text = unsafe { slice::from_raw_parts(pat, len) };
 
     rure_check_rewrite_string_internal(text, cap_num)
 }
 
 #[no_mangle]
 extern "C" fn rure_rewrite_str_convert(rewrite: *const u8, length: size_t) -> *const c_char {
     let rewrite = unsafe { slice::from_raw_parts(rewrite, length) };
 
     rure_rewrite_str_convert_internal(rewrite)
 }
 
 #[no_mangle]
 extern "C" fn rure_rewrite(
     rewrite: *const u8,
     length: size_t,
     vecs: *const *const u8,
     vecs_lengths: *const size_t,
     vecs_count: size_t,
 ) -> *const c_char {
     // 获取rewrite
     let rewrite = unsafe { slice::from_raw_parts(rewrite, length) };
     let rewrite_str = std::str::from_utf8(rewrite).unwrap();
 
     //获取vecs中的内容
     let (raw_vecs, raw_vecsl) = unsafe {
         (
             slice::from_raw_parts(vecs, vecs_count),
             slice::from_raw_parts(vecs_lengths, vecs_count),
         )
     };
 
     let mut rure_vecs = Vec::with_capacity(vecs_count);
     for (&raw_vec, &raw_vecl) in raw_vecs.iter().zip(raw_vecsl) {
         let rure_vec = unsafe { slice::from_raw_parts(raw_vec, raw_vecl) };
         rure_vecs.push(str::from_utf8(rure_vec).unwrap());
     }
 
     rure_rewrite_internal(rewrite_str, vecs_count, rure_vecs)
 }
 
 #[no_mangle]
 extern "C" fn rure_replace_count(re: *const RegexUnicode, haystack: *const c_char) -> size_t {
     let len = unsafe { CStr::from_ptr(haystack).to_bytes().len() };
     let hay = haystack as *const u8;
 
     let re = unsafe { &*re };
     let haystack = unsafe { slice::from_raw_parts(hay, len) };
     rure_replace_count_internal(haystack, re)
 }
 
 #[no_mangle]
 extern "C" fn rure_filter_compile(
     regex_str: *const u8,
     regex_len: size_t,
     min_atoms_len: size_t,
 ) -> MyVec {
     let r = unsafe { slice::from_raw_parts(regex_str, regex_len) };
     let regex_str = str::from_utf8(r).unwrap();
     let atoms = my_compile(regex_str, min_atoms_len as i32);
     atoms
 }
 

