#!/usr/bin/env python3
"""
Finish a macdeployqt'd .app built against Homebrew libraries.

macdeployqt doesn't follow @rpath dependencies of the Homebrew libraries it copies
(e.g. freetype -> brotli), so the app can fail to load its Qt platform plugin on
another Mac. This copies every library the bundle still needs from Homebrew into
Contents/Frameworks and points all references at the bundled copies.

usage: macos_fix_bundle.py path/to/App.app
"""
import os
import shutil
import subprocess
import sys

app = os.path.abspath(sys.argv[1])
frameworks = os.path.join(app, "Contents", "Frameworks")
search = ["/opt/homebrew/lib", "/usr/local/lib"]


def run(*args):
    return subprocess.run(args, check=True, capture_output=True, text=True).stdout


def deps(path):
    lines = run("otool", "-L", path).splitlines()[1:]
    return [line.strip().split(" (")[0] for line in lines if line.strip()]


def is_macho(path):
    try:
        with open(path, "rb") as f:
            return f.read(4) in (b"\xcf\xfa\xed\xfe", b"\xca\xfe\xba\xbe", b"\xfe\xed\xfa\xcf")
    except OSError:
        return False


def find_lib(name):
    for directory in search:
        candidate = os.path.join(directory, name)
        if os.path.exists(candidate):
            return os.path.realpath(candidate)
    # keg-only formulae live under /opt/homebrew/opt/<name>/lib
    opt = "/opt/homebrew/opt"
    if os.path.isdir(opt):
        for formula in os.listdir(opt):
            candidate = os.path.join(opt, formula, "lib", name)
            if os.path.exists(candidate):
                return os.path.realpath(candidate)
    return None


def needs_fix(dep):
    return dep.startswith("@rpath/") or dep.startswith("/opt/homebrew") or dep.startswith("/usr/local/")


changed = True
passes = 0
while changed and passes < 10:
    changed = False
    passes += 1
    files = []
    for root, _, names in os.walk(app):
        for name in names:
            path = os.path.join(root, name)
            if not os.path.islink(path) and is_macho(path):
                files.append(path)
    for path in files:
        for index, dep in enumerate(deps(path)):
            if not needs_fix(dep):
                continue
            name = os.path.basename(dep)
            if index == 0 and name == os.path.basename(path):
                # the library's own id: only cosmetic, point it inside the bundle
                run("install_name_tool", "-id", "@executable_path/../" + os.path.relpath(path, os.path.join(app, "Contents")), path)
                continue
            if ".framework/" in dep:
                # a Qt framework: macdeployqt bundles these, just point at the bundled one
                rel = dep[dep.rfind("/", 0, dep.rfind(".framework/")) + 1:]
                target = "@executable_path/../Frameworks/" + rel
            else:
                bundled = os.path.join(frameworks, name)
                if not os.path.exists(bundled):
                    source = find_lib(name)
                    if not source:
                        print(f"!! {name} needed by {os.path.relpath(path, app)} not found")
                        continue
                    shutil.copy2(source, bundled)
                    os.chmod(bundled, 0o755)
                    run("install_name_tool", "-id", "@executable_path/../Frameworks/" + name, bundled)
                    print(f"+ bundled {name} (for {os.path.relpath(path, app)})")
                    changed = True
                target = "@executable_path/../Frameworks/" + name
            run("install_name_tool", "-change", dep, target, path)
            changed = True

# anything left pointing outside the bundle?
left = []
for root, _, names in os.walk(app):
    for name in names:
        path = os.path.join(root, name)
        if not os.path.islink(path) and is_macho(path):
            for index, dep in enumerate(deps(path)):
                if needs_fix(dep) and not (index == 0 and os.path.basename(dep) == name):
                    left.append(f"{os.path.relpath(path, app)} -> {dep}")
for line in left:
    print("!! still external:", line)
print(f"done after {passes} passes")
sys.exit(1 if left else 0)
