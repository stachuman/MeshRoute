# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
#
# §B96 — resolve the active framework's Wire library HOST-INDEPENDENTLY.
#
# WHY A SCRIPT AND NOT A PATH. `lib_extra_dirs = ${platformio.packages_dir}/framework-arduinoespressif32/libraries`
# FIXED the owner's Windows build (bench tests ran) but BROKE Linux — because that directory name is ambiguous:
#   • Windows: it IS the live 3.1.3 framework.
#   • This Linux box: it is a STALE 2.0.0 LEFTOVER (its WiFi cannot compile — `IPv6Address.h: No such file`),
#     while the framework actually in use is `framework-arduinoespressif32@src-<hash>` (the URL-pinned install).
# A hardcoded name therefore picks the wrong package on whichever host has a duplicate. `get_package_dir` asks
# PlatformIO which framework THIS build is using, so the suffix and any stale sibling become irrelevant.
# ⚠ MUST be a PRE-type script: with `post:` PlatformIO answers
#   "The main program is already constructed and the inline source files are not allowed"
#   because BuildSources cannot add TUs after the program is composed. The error names its own fix.
Import("env")
from os.path import join, isdir

framework = env.PioPlatform().get_package_dir("framework-arduinoespressif32")
if not framework:
    raise RuntimeError("§B96: PlatformIO did not resolve framework-arduinoespressif32")

wire = join(framework, "libraries", "Wire", "src")
if not isdir(wire):
    raise RuntimeError("§B96: active framework has no Wire source directory: %s" % wire)

env.Append(CPPPATH=[wire])
env.BuildSources(join("$BUILD_DIR", "WireFw"), wire)
