# tools/uf2_postbuild.py — emit firmware.uf2 alongside firmware.hex (xiao_sx1262 only).
#
# Why: the Seeed/Adafruit XIAO nRF52840 UF2 bootloader is reached by double-tapping RESET; it
# mounts as a USB mass-storage drive (XIAO-SENSE) and accepts a .uf2 dropped onto it — NO COM port
# required. That is the reliable first-flash path (and the only one when the board shows no port).
# But our nrfutil build only emits .hex/.zip, never a .uf2 — so we convert the .hex here, every build.
#
# Cross-machine: runs uf2conv.py with PlatformIO's bundled python ($PYTHONEXE), so it behaves the
# same on Linux and Windows without needing a system python.
#
# Wired in via  extra_scripts = post:tools/uf2_postbuild.py  in [env:xiao_sx1262].

Import("env")  # noqa: F821 — injected by PlatformIO/SCons
import os

# Adafruit nRF52840 UF2 family id the XIAO bootloader expects. uf2conv's name table has no
# "NRF52840" entry (only the generic "NRF52" 0x1b57745f), so pass the raw number — NOT a name.
UF2_FAMILY = "0xADA52840"


def _uf2conv_path(env):
    pkg = env.PioPlatform().get_package_dir("framework-arduinoadafruitnrf52")
    return os.path.join(pkg, "tools", "uf2conv", "uf2conv.py")


def make_uf2(source, target, env):
    build_dir = env.subst("$BUILD_DIR")
    progname = env.subst("$PROGNAME")
    hexf = os.path.join(build_dir, progname + ".hex")
    uf2f = os.path.join(build_dir, progname + ".uf2")
    uf2conv = _uf2conv_path(env)
    if not os.path.isfile(uf2conv):
        print("uf2_postbuild: uf2conv.py not found (%s) — skipping .uf2" % uf2conv)
        return
    if not os.path.isfile(hexf):
        print("uf2_postbuild: %s not built — skipping .uf2" % hexf)
        return
    # Convert from .hex (carries correct absolute addresses; app starts at 0x27000, above S140 v7).
    env.Execute('"$PYTHONEXE" "%s" -c -f %s -o "%s" "%s"' % (uf2conv, UF2_FAMILY, uf2f, hexf))
    print("uf2_postbuild: wrote %s — double-tap RESET, then drag this onto the XIAO-SENSE drive" % uf2f)


# Fire after the .hex is produced (PROGNAME is normally 'firmware').
env.AddPostAction("$BUILD_DIR/${PROGNAME}.hex", make_uf2)
