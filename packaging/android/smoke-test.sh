#!/bin/sh
# Installs an Orion APK on the attached emulator or device, launches it, and
# checks that it is still running after it has painted. Writes screen.png and
# logcat.txt to OUTPUT_DIR.
#   packaging/android/smoke-test.sh APK APPLICATION_ID OUTPUT_DIR
set -eu
apk=$1 id=$2 out=$3
mkdir -p "$out"
adb wait-for-device
adb install -r "$apk"
adb logcat -c
adb shell am start -W -n "$id/org.orion.OrionActivity"
sleep 20
adb exec-out screencap -p > "$out/screen.png" || true
adb logcat -d > "$out/logcat.txt" || true
if ! adb shell pidof "$id" > /dev/null || grep -q 'Fatal signal\|FATAL EXCEPTION' "$out/logcat.txt"; then
  echo "$id did not stay up:" >&2
  grep -E 'orion|AndroidRuntime|DEBUG|libc|linker' "$out/logcat.txt" | tail -n 120 >&2 || true
  exit 1
fi
grep ' orion' "$out/logcat.txt" | tail -n 40 || true
echo "$id is running"
