#!/bin/bash

#开始执行检查
cargo fmt --all -- --check -v
cargo clean

cargo clippy --all-targets --all-features --tests --benches -- -v
cargo clean

cargo check
cargo clean

bins=$(sed -n '/[[bin]]/ {n;p}' Cargo.toml | sed 's/\"//g' | sed 's/name = //g')
for rust_bin in $bins
do
echo $rust_bin
cargo rustc --bin $rust_bin -- -D warnings -v
done

cargo build --release -v

RUST_BACKTRACE=1 cargo test --all -- --nocapture

cargo doc --all --no-deps
