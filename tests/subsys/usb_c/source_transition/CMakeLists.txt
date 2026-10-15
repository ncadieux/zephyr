# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.28.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(usbc_source_transition)

set(common_dir ${ZEPHYR_BASE}/samples/subsys/usb_c/common)
target_include_directories(app PRIVATE ${common_dir})
target_sources(app PRIVATE
  src/main.c
  ${common_dir}/source_transition.c
)
