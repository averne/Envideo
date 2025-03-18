project('envideo', ['c', 'cpp'],
    default_options: [
        'buildtype=release',
        'b_ndebug=if-release',
        'b_lto=true',
        'strip=true',
        'warning_level=1',
        'c_std=gnu11',
        'cpp_std=gnu++20',
        'auto_features=enabled',
    ],
    license: 'GPLv2+',
    version: '1.0.0',
)

lib_src = []
lib_inc = []
lib_dep = []

conf_data = configuration_data({
    'CONFIG_NVIDIA':  get_option('nvidia') .enabled(),
    'CONFIG_NVGPU':   get_option('nvgpu')  .enabled(),
})

lib_inc += include_directories(
    '.',
    'include',
    'src/nvclasses',
    'src/nvgpu/uapi',
    'src/nvidia/open-gpu-kernel-modules/src/common/sdk/nvidia/inc',
)

lib_src += files(
    'src/cmdbuf.cpp',
    'src/constraints.cpp',
    'src/envideo.cpp',
)

if get_option('nvgpu').enabled()
    lib_src += files('src/nvgpu/device.cpp', 'src/nvgpu/channel.cpp')
endif

if get_option('nvidia').enabled()
    lib_src += files('src/nvidia/device.cpp', 'src/nvidia/channel.cpp')
    lib_inc += include_directories(
        'src/nvidia/open-gpu-kernel-modules/src/nvidia/inc/libraries',
        'src/nvidia/open-gpu-kernel-modules/src/nvidia/generated',
        'src/nvidia/open-gpu-kernel-modules/src/nvidia/arch/nvalloc/unix/include',
        'src/nvidia/open-gpu-kernel-modules/kernel-open/common/inc',
    )
endif

configure_file(output: 'config.h', configuration: conf_data)

envideo_lib = library('envideo', lib_src,
    include_directories: lib_inc,
    dependencies: lib_dep,
    install: true,
)

pkgconfig = import('pkgconfig')
pkgconfig.generate(envideo_lib,
    filebase:    meson.project_name(),
    description: 'Low-level interaction with Nvidia multimedia engines',
)

install_headers('include/envideo.h', subdir: 'envideo')
install_headers(
    'src/nvclasses/cpuopsys.h',  'src/nvclasses/nvtypes.h',   'src/nvclasses/nvmisc.h',
    'src/nvclasses/clc9b0.h',    'src/nvclasses/clc9b7.h',    'src/nvclasses/cle7d0.h',    'src/nvclasses/clb0b6.h',
    'src/nvclasses/nvdec_drv.h', 'src/nvclasses/nvenc_drv.h', 'src/nvclasses/nvjpg_drv.h', 'src/nvclasses/vic_drv.h',
    subdir: 'envideo/classes',
)

if get_option('tests')
    gtest_dep  = dependency('gtest_main', default_options: ['default_library=static'], fallback: ['gtest',  'gtest_main_dep'])
    xxhash_dep = dependency('libxxhash',  default_options: ['default_library=static'], fallback: ['xxhash', 'xxhash_dep'])

    e = executable('test-device',
        files('test/device.cpp'),
        include_directories: lib_inc,
        link_with: envideo_lib,
        dependencies: gtest_dep,
    )
    test('device', e)

    e = executable('test-map',
        files('test/map.cpp'),
        include_directories: lib_inc,
        link_with: envideo_lib,
        dependencies: gtest_dep,
    )
    test('map', e)

    e = executable('test-channel',
        files('test/channel.cpp'),
        include_directories: lib_inc,
        link_with: envideo_lib,
        dependencies: gtest_dep,
    )
    test('channel', e)

    e = executable('test-cmdbuf',
        files('test/cmdbuf.cpp'),
        include_directories: lib_inc,
        link_with: envideo_lib,
        dependencies: gtest_dep,
    )
    test('cmdbuf', e)

    e = executable('test-copy',
        files('test/copy.cpp'),
        include_directories: lib_inc,
        link_with: envideo_lib,
        dependencies: [gtest_dep, xxhash_dep],
    )
    test('copy', e)

    e = executable('test-decode',
        files('test/decode.cpp'),
        include_directories: lib_inc,
        link_with: envideo_lib,
        dependencies: [gtest_dep, xxhash_dep],
    )
    test('decode', e)
endif
