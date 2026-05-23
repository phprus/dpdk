# SPDX-License-Identifier: BSD-3-Clause
# Copyright(c) 2022 Loongson Technology Corporation Limited
# Copyright(c) 2026 Vladislav Shchapov <vladislav@shchapov.ru>

if not dpdk_conf.get('RTE_ARCH_64')
    error('Only 64-bit compiles are supported for this platform type')
endif
dpdk_conf.set('RTE_ARCH', 'loongarch')
dpdk_conf.set('RTE_ARCH_LOONGARCH', 1)
dpdk_conf.set('RTE_FORCE_INTRINSICS', 1)

dpdk_conf.set('RTE_MAX_LCORE', 256)
dpdk_conf.set('RTE_MAX_NUMA_NODES', 16)
dpdk_conf.set('RTE_CACHE_LINE_SIZE', 64)


cc_lasx_flags = []
if cc.get_define('__loongarch_asx', args: machine_args) == ''
    cc_lasx_flags = ['-mlasx']
endif

# we require LSX for DPDK
if cc.get_define('__loongarch_sx', args: machine_args) == ''
    message('Loongson SIMD eXtension (LSX) not enabled by default, explicitly enabling')
    machine_args += '-mlsx'
endif

base_flags = ['LSX']
foreach f:base_flags
    compile_time_cpuflags += ['RTE_CPUFLAG_' + f]
endforeach

optional_flags = [
    ['LASX', '__loongarch_asx']
]
foreach f:optional_flags
    if cc.get_define('@0@'.format(f[1]), args: machine_args) == '1'
        compile_time_cpuflags += ['RTE_CPUFLAG_' + f[0]]
    endif
endforeach
