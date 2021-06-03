// Copyright (c) 2016 Adam Perry <adam.n.perry@gmail.com>
//
// This software may be modified and distributed under the terms of the MIT license.  See the
// LICENSE file for details.

use std::env;
use std::fs;
use std::path::Path;
use std::process::Command;
extern crate cmake;

fn main() {
  
    let dst = cmake::Config::new("parasail_c").define("BUILD_SHARED_LIBS","OFF").profile("Release").build();
    let path = dst.join("build");
    let outpath;
    if (path.join("Release").exists()) {
        outpath = path.join("Release");
    } else {
        outpath = path;
    }
    println!("cargo:rustc-link-search=native={}", outpath.display());
    println!("cargo:rustc-link-lib=static=parasail");
}
