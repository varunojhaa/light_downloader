cmake_minimum_required(VERSION 3.20)
project(LightDownloader LANGUAGES NONE)

set(RUST_MANIFEST "${CMAKE_CURRENT_SOURCE_DIR}/rust-core/Cargo.toml")
set(RUST_TARGET_DIR "${CMAKE_CURRENT_BINARY_DIR}/cargo-target")

add_custom_target(light-downloader-rust ALL
    COMMAND cargo build --release --manifest-path "${RUST_MANIFEST}" --target-dir "${RUST_TARGET_DIR}"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    USES_TERMINAL
    COMMENT "Building portable Light Downloader CLI")

if(WIN32)
  add_custom_target(light-downloader-windows
    COMMAND powershell -NoProfile -ExecutionPolicy Bypass -File "${CMAKE_CURRENT_SOURCE_DIR}/build-llvm.ps1"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    USES_TERMINAL)
endif()