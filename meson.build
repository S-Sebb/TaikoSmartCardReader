project('TaikoSmartcardReader', 'c', 'cpp', version: '1.0.0', default_options : ['c_std=c11', 'cpp_std=c++23'])
warning_level = 3
debug = true
optimization = 3
b_lto = true
b_pgo = 'use'
cmake = import('cmake')
opt_var = cmake.subproject_options()
opt_var.set_override_option('cpp_std', 'c++23')

cpp = meson.get_compiler('cpp')

add_project_arguments(
    cpp.get_supported_arguments(
        '-D_WIN32_WINNT=_WIN32_WINNT_WIN10',
    ),
    language: 'cpp',
)

hid_lib = cpp.find_library('hid', required: false)
winscard_lib = cpp.find_library('winscard', required: false)
setupapi_lib = cpp.find_library('setupapi', required: false)

add_project_link_arguments(
    cpp.get_supported_arguments(
        '-static',
        #'-s',
        '-lws2_32',
        '-lssp',
        '-lntdll',
        '-Wl,--allow-multiple-definition',
    ),
    language: 'cpp'
)


opt_var.add_cmake_defines({'BUILD_EXAMPLES': false})

scardreader_dll = shared_library(
    'scardreader',
    include_directories: [
        'src'
    ],
    vs_module_defs : 'src/scardreader.def',
    sources : [
        'src/dllmain.cpp',
        'src/helpers.cpp',
        'src/scard.cpp'
    ],
    dependencies: [
        hid_lib,
        winscard_lib,
        setupapi_lib,
    ],
    name_prefix: ''
)
