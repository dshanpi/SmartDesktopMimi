# Developer build selection

This is a developer-host tool. There is no corresponding desktop screen.

Run `python3 tools/sdkbuild/configure.py configure --interactive --output PROFILE.json`
and provide the SDK path, independently reviewed SDK lock hash, platform,
target OS and comma-separated screen list. Configuration checks every SDK
input and compiles/links a dependency probe against the selected target SDK.
It never runs a target executable on the host. Reviewed profiles cannot be
silently overwritten.

Use `check --profile PROFILE.json --sdk SDK_DIRECTORY` to repeat validation;
use `build --profile PROFILE.json --sdk SDK_DIRECTORY --output NEW_BUILD_DIRECTORY`
for a clean-source Tina application build. The output includes a build profile
and the desktop receipt records the compiled screen list. A device outside that
list cannot report healthy startup.

Current qualified compiler adapter: A133 Tina aarch64. T527/RK3576 remain
unavailable. Tina SDK input is rejected for Debian/Ubuntu packaging. A Debian
SDK/toolchain and actual target OS must be qualified before .deb delivery;
that packaging adapter is not implemented yet. Enabling voice_control also
fails closed until the independent speech provider is integrated. Selecting a
resolution is not layout or hardware acceptance: only 1024x768 has current
A133 evidence; further responsive layouts and physical matrices remain open.
