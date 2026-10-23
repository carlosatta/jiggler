"""PlatformIO pre-build hook: regenerate src/dashboard_html.h from web/ before every build and test run."""
import subprocess
import sys

Import("env")  # noqa: F821  (provided by PlatformIO/SCons)

script = env.subst("$PROJECT_DIR/tools/build_web.py")  # noqa: F821
subprocess.check_call([sys.executable, script])
