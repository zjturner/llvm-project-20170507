# -*- Python -*-

import os
import sys
import re
import platform
import subprocess


import lit.util
import lit.formats
from lit.llvm import llvm_config
from lit.llvm.subst import FindTool
from lit.llvm.subst import ToolSubst

# name: The name of this test suite.
config.name = 'LLDB'

# testFormat: The test format to use to interpret tests.
config.test_format = lit.formats.ShTest(not llvm_config.use_lit_shell)

# suffixes: A list of file extensions to treat as test files. This is overriden
# by individual lit.local.cfg files in the test subdirectories.
config.suffixes = ['.test', '.cpp', '.s']

# excludes: A list of directories to exclude from the testsuite. The 'Inputs'
# subdirectories contain auxiliary inputs for various tests in their parent
# directories.
config.excludes = ['Inputs', 'CMakeLists.txt', 'README.txt', 'LICENSE.txt']

# test_source_root: The root path where tests are located.
config.test_source_root = os.path.dirname(__file__)

# test_exec_root: The root path where tests should be run.
config.test_exec_root = os.path.join(config.lldb_obj_root, 'lit')


llvm_config.use_default_substitutions()

flags = []
if platform.system() in ['Darwin']:
    try:
        out = subprocess.check_output(['xcrun', '--show-sdk-path']).strip()
        res = 0
    except OSError:
        res = -1
    if res == 0 and out:
        sdk_path = lit.util.to_string(out)
        lit_config.note('using SDKROOT: %r' % sdk_path)
        flags = ['-isysroot', sdk_path]
elif platform.system() in ['OpenBSD']:
    flags = ['-pthread']

# Set up substitutions for primary tools.  These tools must come from config.lldb_tools_dir
# which is basically the build output directory.  We do not want to find these in path or
# anywhere else, since they are specifically the programs which are actually being tested.

dsname = 'debugserver' if platform.system() in ['Darwin'] else 'lldb-server'
dsargs = [] if platform.system() in ['Darwin'] else ['gdbserver']
lldbmi = ToolSubst('%lldbmi', command=FindTool('lldb-mi'), extra_args=['--synchronous'], unresolved='ignore')
primary_tools = [
    ToolSubst('%lldb', command=FindTool('lldb'), extra_args=['-S', os.path.join(config.test_source_root, 'lit-lldb-init')]),
    lldbmi,
    ToolSubst('%debugserver', command=FindTool(dsname), extra_args=dsargs, unresolved='ignore'),
    'lldb-test'
    ]

llvm_config.add_tool_substitutions(primary_tools, [config.lldb_tools_dir])
if lldbmi.was_resolved:
    config.available_features.add('lldb-mi')

# Set up substitutions for support tools.  These tools can be overridden at the CMake
# level (by specifying -DLLDB_LIT_TOOLS_DIR), installed, or as a last resort, we can use
# the just-built version.
additional_tool_dirs=[]
if config.lldb_lit_tools_dir:
    additional_tool_dirs.append(config.lldb_lit_tools_dir)

llvm_config.use_clang(additional_flags=flags, additional_tool_dirs=additional_tool_dirs, required=True)
if config.have_lld:
    llvm_config.use_lld(additional_tool_dirs=additional_tool_dirs, required=True)
    config.available_features.add('lld')


support_tools = ['yaml2obj', 'obj2yaml', 'llvm-pdbutil', 'llvm-mc', 'llvm-readobj', 'llvm-objdump', 'llvm-objcopy']
llvm_config.add_tool_substitutions(support_tools,
                                   additional_tool_dirs + [config.lldb_tools_dir, config.llvm_tools_dir])

if re.match(r'^arm(hf.*-linux)|(.*-linux-gnuabihf)', config.target_triple):
    config.available_features.add("armhf-linux")

def calculate_arch_features(arch_string):
    # This will add a feature such as x86, arm, mips, etc for each built
    # target
    features = []
    for arch in arch_string.split():
        features.append(arch.lower())
    return features

# Run llvm-config and add automatically add features for whether we have
# assertions enabled, whether we are in debug mode, and what targets we
# are built for.
llvm_config.feature_config(
    [('--assertion-mode', {'ON': 'asserts'}),
     ('--build-mode', {'DEBUG': 'debug'}),
     ('--targets-built', calculate_arch_features)
     ])
