# MeshRoute — tools/ccache_native.py
# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
#
# §harness-speed Part B (owner-ordered 2026-08-24, wired ahead of the parallel-battery slice by explicit owner
# acceptance): route the NATIVE env's host compilations through ccache via Debian's masquerade directory
# (/usr/lib/ccache holds gcc/g++/cc symlinks to ccache; prepending it to the build PATH wraps every host
# compile with zero compiler change — same gcc, cached objects).
#
# ⓘ NATIVE ONLY, by design: the board toolchains (xtensa-esp32, ARM GCC 12.3) are PlatformIO-packaged binaries
#   that do not exist in /usr/lib/ccache, so this script would be a no-op there — it is deliberately not
#   attached to any board env, and board builds are byte-for-byte unaffected.
# ⓘ FAIL-OPEN on a machine without the masquerade dir (ccache not installed): the PATH entry simply resolves
#   nothing and the stock compiler is used — the build never breaks for want of a cache (C2 does not apply:
#   a cache is an accelerator, not a behaviour; silently proceeding without one is the correct honest default).
# ⓘ The cache is the user-default CCACHE_DIR (~/.ccache, 5 GB cap), deliberately shared: the real tree, every
#   battery scratch tree and every worker hit one warm cache.
import os

Import("env")

_MASQ = "/usr/lib/ccache"
if os.path.isdir(_MASQ):
    env.PrependENVPath("PATH", _MASQ)
