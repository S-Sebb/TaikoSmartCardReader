# Set library type to static
project('TaikoSmartcardReader', 'cpp', version: '1.0.0', default_options : ['cpp_std=c++20', 'buildtype=release', 'default_library=static'])
cmake = import('cmake')
opt_var = cmake.subproject_options()
opt_var.set_override_option('cpp_std', 'c++20')

cpp = meson.get_compiler('cpp')

# Compiler and Linker Flags
add_project_arguments(
    cpp.get_supported_arguments(
        '-D_WIN32_WINNT=_WIN32_WINNT_WIN10'
    ),
    language: 'cpp'
)

winscard_lib = cpp.find_library('winscard', required: true)
nlohmann_json_dep = dependency('nlohmann_json', fallback: ['nlohmann_json', 'nlohmann_json_dep'], static: true)
curl_dep = dependency('curl', fallback: ['curl', 'curl_dep'], static: true)

add_project_link_arguments(
    cpp.get_supported_arguments(
        '-lws2_32',
        '-lntdll'
    ),
    language: 'cpp'
)

opt_var.add_cmake_defines({'BUILD_EXAMPLES': false})

# Define and build the shared library
scardreader_dll = shared_library(
    'scardreader',
    include_directories: [
        'src'
    ],
    vs_module_defs: 'src/scardreader.def',
    sources: [
        'src/dllmain.cpp',
        'src/helpers.cpp',
        'src/scard.cpp'
    ],
    dependencies: [
        winscard_lib,
        curl_dep,
        nlohmann_json_dep
    ],
    install : true,
    name_prefix: ''
)
