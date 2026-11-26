#!/usr/bin/env python3
"""Package an optimized Mecha Hack client and its published game data."""
import argparse
import hashlib
import os
from pathlib import Path
import re
import shutil
import subprocess
import tarfile
import tempfile
import zipfile

from release_assets import prepare

ROOT = Path(__file__).resolve().parents[2]
ANDROID = ROOT / 'packaging/mecha/android'


def run(*args, **kwargs):
    subprocess.run(list(map(str, args)), check=True, **kwargs)


def stage_game(stage, runtime, deps):
    if not (runtime / 'runtime-models.json').is_file():
        raise ValueError('Select the published runtime asset directory')
    prepare(runtime, stage / 'assets/mecha')
    shutil.copytree(ROOT / 'assets/rpg', stage / 'assets/rpg')
    notices = stage / 'licenses'
    shutil.copytree(ROOT / 'licenses', notices)
    shutil.copy2(ROOT / 'data/hrtf/LICENSE.txt', notices / 'hrtf.txt')
    for name, license_name in [('sdl2', 'LICENSE.txt'), ('opus', 'COPYING'), ('openxr', 'LICENSE'), ('glew', 'LICENSE.txt')]:
        source = deps / name / license_name
        if source.is_file():
            shutil.copy2(source, notices / (name + '.txt'))
    (stage / 'README.txt').write_text(
        'Mecha Hack XR 0.2.5\n\nStart in XR with two tracked controllers. In the hangar, '
        'Online Co-op Lobby connects to the public server. The first pilot hosts. '
        'Use the same menu entry to leave. Offline play is the default.\n'
        'Game UDP 29597. Voice UDP 29598.\n')


def zip_tree(path, source):
    with zipfile.ZipFile(path, 'w', zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
        for f in sorted(source.rglob('*')):
            if f.is_file():
                archive.write(f, f.relative_to(source))


def desktop(stage, args):
    stage_game(stage, args.runtime, args.deps)
    if args.platform == 'windows':
        for rel in ['mecha-hack-xr.exe', 'deps/sdl2/SDL2.dll', 'deps/openxr/src/loader/libopenxr_loader.dll']:
            shutil.copy2(args.build / rel, stage)
        toolchain = args.deps / 'llvm-mingw'
        for dll in ['libc++.dll', 'libunwind.dll', 'libwinpthread-1.dll']:
            shutil.copy2(toolchain / 'x86_64-w64-mingw32/bin' / dll, stage)
        shutil.copy2(toolchain / 'LICENSE.TXT', stage / 'licenses/LLVM-runtime.txt')
        for note in (toolchain / 'x86_64-w64-mingw32/share/mingw32').glob('COPYING*'):
            shutil.copy2(note, stage / 'licenses' / ('MinGW-' + note.name))
        (stage / 'Start-XR.bat').write_bytes(b'@echo off\r\n"%~dp0mecha-hack-xr.exe" --xr --assets "%~dp0assets\\mecha" %*\r\n')
        zip_tree(args.out / 'mecha-hack-xr-windows-x86_64.zip', stage)
    else:
        (stage / 'bin').mkdir()
        for name in ['mecha-hack-xr', 'mecha-hack-server']:
            shutil.copy2(args.build / name, stage / 'bin' / name)
            run('strip', '--strip-unneeded', stage / 'bin' / name)
            launcher = stage / name
            launcher.write_text('#!/bin/sh\nset -eu\napp=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)\n'
                                'exec "$app/bin/' + name + '" --assets "$app/assets/mecha" "$@"\n')
            launcher.chmod(0o755)
        with tarfile.open(args.out / 'mecha-hack-xr-linux-x86_64.tar.gz', 'w:gz', compresslevel=6) as archive:
            archive.add(stage, arcname='mecha-hack-xr')


def quest(stage, args):
    game = stage / 'assets/game'
    game.mkdir(parents=True)
    stage_game(game, args.runtime, args.deps)
    revision = hashlib.sha256()
    for f in sorted(game.rglob('*')):
        if f.is_file():
            revision.update(f.relative_to(game).as_posix().encode() + b'\0' + hashlib.sha256(f.read_bytes()).digest())
    (game / 'revision.txt').write_text(revision.hexdigest())
    tool = args.ndk / 'toolchains/llvm/prebuilt/linux-x86_64'
    lib = stage / 'lib/arm64-v8a'
    lib.mkdir(parents=True)
    for f in [args.build / 'libmecha-hack-xr.so', args.build / 'deps/sdl2/libSDL2.so',
              args.deps / 'openxr-android/jni/arm64-v8a/libopenxr_loader.so',
              tool / 'sysroot/usr/lib/aarch64-linux-android/libc++_shared.so']:
        shutil.copy2(f, lib)
        run(tool / 'bin/llvm-strip', '--strip-unneeded', lib / f.name)
    for name in ['NOTICE', 'NOTICE.toolchain']:
        shutil.copy2(args.ndk / name, game / 'licenses' / ('Android-NDK-' + name))
    sdl_java = args.deps / 'sdl2/android-project/app/src/main/java'
    hid = (sdl_java / 'org/libsdl/app/HIDDeviceManager.java').read_text()
    for library, symbols in [('libmecha-hack-xr.so', ['SDL_main']), ('libSDL2.so', [
            'Java_org_libsdl_app_HIDDeviceManager_' + name for name in re.findall(r'\bnative\s+\w+\s+(\w+)\s*\(', hid)])]:
        exported = subprocess.check_output([tool / 'bin/llvm-nm', '-D', '--defined-only', lib / library], text=True)
        if any(' ' + name + '\n' not in exported for name in symbols):
            raise ValueError('Missing Android entry point in ' + library)
    platform = args.sdk / 'platforms/android-34/android.jar'
    tools = args.sdk / 'build-tools/36.1.0'
    java = args.java_home / 'bin'
    env = dict(os.environ, JAVA_HOME=str(args.java_home), PATH=str(java) + os.pathsep + os.environ['PATH'])
    for directory in ['classes', 'dex']:
        (stage / directory).mkdir()
    run(java / 'javac', '-source', '8', '-target', '8', '-bootclasspath', platform, '-classpath', tools / 'core-lambda-stubs.jar',
        '-d', stage / 'classes', *sorted(ANDROID.glob('*.java')), *sorted(sdl_java.rglob('*.java')))
    run(java / 'jar', 'cf', stage / 'classes.jar', '-C', stage / 'classes', '.')
    run(tools / 'd8', '--min-api', '29', '--lib', platform, '--output', stage / 'dex', stage / 'classes.jar', env=env)
    unaligned = stage / 'unaligned.apk'
    run(tools / 'aapt2', 'link', '-I', platform, '--manifest', ANDROID / 'AndroidManifest.xml', '-o', unaligned)
    with zipfile.ZipFile(unaligned, 'a', zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
        archive.write(stage / 'dex/classes.dex', 'classes.dex')
        for directory in ['assets', 'lib']:
            for f in sorted((stage / directory).rglob('*')):
                if f.is_file():
                    archive.write(f, f.relative_to(stage))
    apk = args.out / 'mecha-hack-xr-quest3-arm64.apk'
    run(tools / 'zipalign', '-P', '16', '-f', '4', unaligned, apk)
    run(tools / 'apksigner', 'sign', '--ks', args.keystore, '--ks-pass', 'file:' + str(args.password_file),
        '--v4-signing-enabled', 'false', apk, env=env)
    run(tools / 'apksigner', 'verify', '--verbose', apk, env=env)
    run(tools / 'zipalign', '-c', '-P', '16', '4', apk)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('platform', choices=['linux', 'windows', 'quest'])
    for name in ['build', 'runtime', 'deps', 'out']:
        parser.add_argument('--' + name, type=Path, required=True)
    for name in ['sdk', 'ndk', 'java-home', 'keystore', 'password-file']:
        parser.add_argument('--' + name, type=Path)
    args = parser.parse_args()
    if not re.search(r'^CMAKE_BUILD_TYPE:STRING=Release$', (args.build / 'CMakeCache.txt').read_text(), re.M):
        parser.error('An optimized Release build is required')
    if args.platform == 'quest' and not all([args.sdk, args.ndk, args.java_home, args.keystore, args.password_file]):
        parser.error('Quest packaging requires the Android SDK/NDK, JDK and signing credentials')
    args.out.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=args.out) as temp:
        (quest if args.platform == 'quest' else desktop)(Path(temp), args)


if __name__ == '__main__':
    main()
