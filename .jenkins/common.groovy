// This file is for internal AMD use.
// If you are interested in running your own Jenkins, please raise a github issue for assistance.

def runCompileCommand(platform, project, jobName, boolean debug=false, boolean buildStatic=false)
{
    project.paths.construct_build_prefix()

    String clientArgs = '-DBUILD_CLIENTS_SAMPLES=ON -DBUILD_CLIENTS_TESTS=ON -DBUILD_CLIENTS_SELFTEST=ON -DBUILD_CLIENTS_RIDER=ON -DBUILD_FFTW=ON'
    String warningArgs = '-DWERROR=ON'
    String buildTypeArg = debug ? '-DCMAKE_BUILD_TYPE=Debug -DROCFFT_DEVICE_FORCE_RELEASE=ON' : '-DCMAKE_BUILD_TYPE=Release'
    String buildTypeDir = debug ? 'debug' : 'release'
    String staticArg = buildStatic ? '-DBUILD_SHARED_LIBS=off' : ''
    String hipClangArgs = jobName.contains('hipclang') ? '-DUSE_HIP_CLANG=ON -DHIP_COMPILER=clang' : ''
    String cmake = platform.jenkinsLabel.contains('centos') ? 'cmake3' : 'cmake'
    //Set CI node's gfx arch as target if PR, otherwise use default targets of the library
    String amdgpuTargets = env.BRANCH_NAME.startsWith('PR-') ? '-DAMDGPU_TARGETS=\$gfx_arch' : ''

    def command = """#!/usr/bin/env bash
                set -x

                cd ${project.paths.project_build_prefix}
                mkdir -p build/${buildTypeDir} && cd build/${buildTypeDir}
                ${auxiliary.gfxTargetParser()}
                ${cmake} -DCMAKE_CXX_COMPILER=/opt/rocm/bin/hipcc ${buildTypeArg} ${clientArgs} ${warningArgs} ${hipClangArgs} ${staticArg} ${amdgpuTargets} ../..
                make -j\$(nproc)
            """
    platform.runCommand(this, command)
}

def runTestCommand (platform, project, boolean debug=false)
{
    String sudo = auxiliary.sudo(platform.jenkinsLabel)
    String testBinaryName = debug ? 'rocfft-test-d' : 'rocfft-test'
    String directory = debug ? 'debug' : 'release'

    def command = """#!/usr/bin/env bash
                set -x
                cd ${project.paths.project_build_prefix}/build/${directory}/clients/staging
                ROCM_PATH=/opt/rocm GTEST_LISTENER=NO_PASS_LINE_IN_LOG ./${testBinaryName} --gtest_color=yes
            """
    platform.runCommand(this, command)
}

def runPackageCommand(platform, project, jobName, boolean debug=false)
{
    String directory = debug ? 'debug' : 'release'
    def packageHelper = platform.makePackage(platform.jenkinsLabel,"${project.paths.project_build_prefix}/build/${directory}",true)
    platform.runCommand(this, packageHelper[0])
    platform.archiveArtifacts(this, packageHelper[1])
}

def runSubsetBuildCommand(platform, project, jobName, genPattern, genSmall, genLarge, boolean onlyDouble)
{
    project.paths.construct_build_prefix()

    // Don't build client for now..
    // String clientArgs = '-DBUILD_CLIENTS_SAMPLES=ON -DBUILD_CLIENTS_TESTS=ON -DBUILD_CLIENTS_SELFTEST=ON -DBUILD_CLIENTS_RIDER=ON'
    String clientArgs = ''
    String warningArgs = '-DWERROR=ON'
    String buildTypeArg = '-DCMAKE_BUILD_TYPE=Release'
    String buildTypeDir = 'release'

    String genPatternArgs = "-DGENERATOR_PATTERN=${genPattern}"
    String manualSmallArgs = (genSmall != null) ? "-DGENERATOR_MANUAL_SMALL_SIZE=${genSmall}" : ''
    String manualLargeArgs = (genLarge != null) ? "-DGENERATOR_MANUAL_LARGE_SIZE=${genLarge}" : ''
    String precisionArgs = onlyDouble ? '-DGENERATOR_PRECISION=double' : ''
    String kernelArgs = "${genPatternArgs} ${manualSmallArgs} ${manualLargeArgs} ${precisionArgs}"

    String hipClangArgs = jobName.contains('hipclang') ? '-DUSE_HIP_CLANG=ON -DHIP_COMPILER=clang' : ''
    String cmake = platform.jenkinsLabel.contains('centos') ? 'cmake3' : 'cmake'
    //Set CI node's gfx arch as target if PR, otherwise use default targets of the library
    String amdgpuTargets = env.BRANCH_NAME.startsWith('PR-') ? '-DAMDGPU_TARGETS=\$gfx_arch' : ''

    def command = """#!/usr/bin/env bash
                set -x

                ls /fftw/lib

                export FFTW_ROOT=/fftw
                export FFTW_INCLUDE_PATH=\${FFTW_ROOT}/include
                export FFTW_LIB_PATH=\${FFTW_ROOT}/lib
                export LD_LIBRARY_PATH=\${FFTW_LIB_PATH}:\$LD_LIBRARY_PATH
                export CPLUS_INCLUDE_PATH=\${FFTW_INCLUDE_PATH}:\${CPLUS_INCLUDE_PATH}
                export CMAKE_PREFIX_PATH=\${FFTW_LIB_PATH}/cmake/fftw3:\${CMAKE_PREFIX_PATH}
                export CMAKE_PREFIX_PATH=\${FFTW_LIB_PATH}/cmake/fftw3f:\${CMAKE_PREFIX_PATH}

                cd ${project.paths.project_build_prefix}
                rm -rf build/${buildTypeDir}
                mkdir -p build/${buildTypeDir} && cd build/${buildTypeDir}
                ${auxiliary.gfxTargetParser()}
                ${cmake} -DCMAKE_CXX_COMPILER=/opt/rocm/bin/hipcc ${buildTypeArg} ${clientArgs} ${kernelArgs} ${warningArgs} ${hipClangArgs} ${amdgpuTargets} ../..
                make -j\$(nproc)
            """
    platform.runCommand(this, command)
}
return this
