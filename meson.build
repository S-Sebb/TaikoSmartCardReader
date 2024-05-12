project('TaikoSmartcardReader', 'c', 'cpp', version: '1.0.0', default_options : ['c_std=c11', 'cpp_std=c++20', 'b_vscrt=md', 'warning_level=3', 'optimization=2', 'debug=false', 'b_lto=true'])

cpp = meson.get_compiler('cpp')
cmake = import('cmake')
opt_var = cmake.subproject_options()
opt_var.set_override_option('cpp_std', 'c++20')

# Compiler and Linker Flags
add_project_arguments(
    cpp.get_supported_arguments(
        '-D_WIN32_WINNT=_WIN32_WINNT_WIN10'
    ),
    language: 'cpp'
)

hid_lib = cpp.find_library('hid', required: true)
winscard_lib = cpp.find_library('winscard', required: true)
setupapi_lib = cpp.find_library('setupapi', required: true)
nlohmann_json = subproject('nlohmann_json')
nlohmann_json_dep = nlohmann_json.get_variable('nlohmann_json_dep')
curl = subproject('curl')
curl_dep = curl.get_variable('curl_dep')

# Link arguments should be set correctly for the platform and compiler
add_project_link_arguments(
    cpp.get_supported_arguments(
        '-lws2_32',
        '-lntdll'
    ),
    language: 'cpp'
)

# Define and build the shared library
scardreader_dll = shared_library(
    'scardreader',
    include_directories: [
        include_directories('src')
    ],
    sources: [
        'src/dllmain.cpp',
        'src/helpers.cpp',
        'src/scard.cpp'
    ],
    dependencies: [
        hid_lib,
        winscard_lib,
        setupapi_lib,
        curl_dep,
        nlohmann_json_dep
    ],
    vs_module_defs: 'src/scardreader.def',
    name_prefix: '',
    install: true
)
