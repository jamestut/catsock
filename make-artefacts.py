#!/usr/bin/env python3
import sys
import os
from os import path
import subprocess
import multiprocessing
import argparse
import shutil

CFLAGS_COMMON = ['-O2', '-g0']
EXE_NAME = 'catsock'

def printr(*args, **kwargs):
    kwargs['file'] = sys.stderr
    print(*args, **kwargs)

def get_nproc(args):
    nproc = 1 if args.single else multiprocessing.cpu_count()
    nproc = str(nproc)
    return nproc

def build_linux(ap):
    # valid targets: 'clang --print-targets'
    TARGETS = ['aarch64', 'x86_64']
    TRIPLET_SUFFIX = 'redhat-linux-gnu'

    SYSROOT_NAME = 'glibc'

    ap.add_argument('targets', nargs='*',
        help=f'Specify which platforms to build. Supported platforms: {" ".join(TARGETS)}')
    args = ap.parse_args()

    if args.targets:
        if not all(i in TARGETS for i in args.targets):
            printr("One or more of the specified targets are not supported.")
            sys.exit(1)
        TARGETS = args.targets

    if 'CC' not in os.environ:
        printr('Please specify clang executable in $CC variable.')
        sys.exit(1)

    # check that $CC is actually clang
    if not subprocess.run(
        [os.environ['CC'], '--version'],
        stdout=subprocess.PIPE,
        check=True
    ).stdout.startswith(b'clang'):
        printr('Ensure that $CC is clang.')
        sys.exit(1)

    # check that $LD llvm's linker
    ld_str = subprocess.run(
        ['ld', '-version'],
        stdout=subprocess.PIPE,
        check=True
    ).stdout
    if not (ld_str.startswith(b'LLD') and b'compatible with GNU linkers' in ld_str):
        printr('Ensure that system ld is lld.')
        sys.exit(1)

    # ensure the sysroots are specified
    SYSROOT_VARS=[f'SYSROOT_{i.replace("-", "_")}' for i in TARGETS]
    sysroot_specified = True
    for tgt, v in zip(TARGETS, SYSROOT_VARS):
        if v not in os.environ:
            sysroot_specified = False
            printr(f"Please specify sysroot for target '{tgt}' in ${v} variable.")
        else:
            if ' ' in os.environ[v]:
                printr("Ensure no space character in SYSROOT variables.")
                exit(1)
    if not sysroot_specified:
        exit(1)

    make_env = os.environ.copy()
    ldflags = ['--rtlib=compiler-rt']
    if args.verbose_linker:
        ldflags.append('-v')
    make_env['LDFLAGS'] = ' '.join(ldflags)

    nproc = get_nproc(args)

    subprocess.run(['make', 'cleanartefacts'], check=True)

    for tgt, sysroot_var in zip(TARGETS, SYSROOT_VARS):
        print(f'Building for {tgt} ...')
        make_env['CFLAGS'] = f'--target={tgt}-{TRIPLET_SUFFIX} --sysroot {os.environ[sysroot_var]}'
        subprocess.run(['make', 'clean'], check=True)
        subprocess.run(['make', f'-j{nproc}'], env=make_env, check=True)
        subprocess.run(['strip', EXE_NAME], check=True)
        shutil.move(EXE_NAME, f'{EXE_NAME}-{SYSROOT_NAME}-{tgt}')

def build_darwin(ap):
    args = ap.parse_args()

    nproc = get_nproc(args)

    make_env = os.environ.copy()
    if args.verbose_linker:
        make_env['LDFLAGS'] = '-v'
    make_env['CFLAGS'] = '-arch x86_64 -arch arm64 -O2 -g0'

    subprocess.run(['make', f'-j{nproc}'], env=make_env, check=True)
    shutil.move(EXE_NAME, f'{EXE_NAME}-darwin-universal')

if __name__ == '__main__':
    os.chdir(path.split(sys.argv[0])[0])

    ap = argparse.ArgumentParser()
    ap.add_argument('--single', action='store_true',
        help="If specified, run the 'make' command single-threaded.")
    ap.add_argument('--verbose-linker', action='store_true',
        help="Pass '-v' option to the LDFLAGS.")

    if sys.platform == 'linux':
        build_linux(ap)
    elif sys.platform == 'darwin':
        build_darwin(ap)
    else:
        printr(f"Unsupported platform '{sys.platform}'")

    print("Done!")
