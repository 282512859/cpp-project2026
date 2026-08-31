Summary
- Add a Qt wrapper for ClientCore to support future GUI work (QThread + QObject): ClientWorker + QtClient.
- Make the Qt wrapper optional via CMake option BUILD_QT_CLIENT (default OFF).
- Add cloud_client_qt target (static lib) that links cloud_client_core and Qt6::Core.
- Add CMake presets windows-debug-qt / windows-release-qt and docs (BUILD_WINDOWS.md) describing Qt build.

Files changed
- client_core/include/cloud/client/QtClient.h (new)
- client_core/src/QtClient.cpp (new)
- client_core/CMakeLists.txt (add BUILD_QT_CLIENT + cloud_client_qt)
- CMakePresets.json (add windows-*-qt presets)
- BUILD_WINDOWS.md (Qt build instructions)

Why
- Enables Qt GUI development without forcing Qt dependency for normal builds.

Build & test
1. Ensure Qt6 (MSVC x64) is installed and CMake can find it (Qt6_DIR / CMAKE_PREFIX_PATH).
2. Configure + build:
   - cmake --preset windows-debug-qt
   - cmake --build --preset windows-debug-qt
3. Recommended smoke test: link a minimal Qt executable to cloud_client_qt, call login/list/upload (verify signals/slots, progress).

Notes & follow-ups
- BUILD_QT_CLIENT=OFF by default; no runtime change when disabled.
- Next: add a minimal Qt example app (login + file list) and CI job to build the Qt preset.

Suggested reviewers
- client/network owner (成员3)
- project arch / maintainer (成员1)

Suggested labels
- enhancement, build
