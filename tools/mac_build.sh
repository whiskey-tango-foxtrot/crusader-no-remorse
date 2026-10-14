#!/bin/bash
# Standalone Mac build of the Crusader remake (Development, cooked + pak, archived to build/Mac).
#
# UBT's own "finalize the .app with Xcode" step fails when UBT runs from a non-interactive shell: the nested UBT
# process that runs xcodebuild has no stdin, and Xcode then logs "Failed to launch task /bin/sh: Bad file descriptor"
# for the scheme pre-action (xcodebuild run by hand with stdin closed, `<&-`, reproduces it; with any open stdin it
# works). So this script compiles with Build.sh, ignores that last step, runs the same xcodebuild itself with an open
# stdin, writes the .target receipt with UBT's WriteMetadata mode (the step that was skipped after the failure), then
# cooks and stages with RunUAT -skipbuild. The maps to cook are listed in Config/DefaultGame.ini (the 25 *_3D_HD levels).
set -u
UE=${UE_ROOT:-"/Users/Shared/Epic Games/UE_5.8"}
ROOT=${CRUSADER_ROOT:-$(cd "$(dirname "$0")/.." && pwd)}
PROJ=$ROOT/unreal/CrusaderRemake.uproject
CONFIG=${1:-Development}
OUT=${2:-$ROOT/build}

cd $ROOT/unreal
echo "== compile $CONFIG"
$UE/Engine/Build/BatchFiles/Mac/Build.sh CrusaderRemake Mac $CONFIG -Project=$PROJ -WaitMutex 2>&1 | grep -E "error:|warning: .*CrusaderRemake|Succeeded|Failed|Total time|Finalize" || true

echo "== finalize the .app with xcodebuild"
cat /dev/null | UBT_NO_POST_DEPLOY=true /Applications/Xcode.app/Contents/Developer/usr/bin/xcodebuild build \
  -workspace "$ROOT/unreal/Intermediate/ProjectFiles/CrusaderRemake_Mac_CrusaderRemake.xcworkspace" \
  -scheme CrusaderRemake -configuration $CONFIG -destination generic/platform=macOS \
  CODE_SIGN_ALLOW_ENTITLEMENTS_MODIFICATION=YES -allowProvisioningUpdates UE_XCODE_BUILD_MODE=PostBuildSync 2>&1 | grep -E "BUILD (SUCCEEDED|FAILED)|error:" || true
test -x "$ROOT/unreal/Binaries/Mac/CrusaderRemake.app/Contents/MacOS/CrusaderRemake" || { echo "no app produced"; exit 1; }

echo "== write the target receipt"
(cd $UE && bash -c "source Engine/Build/BatchFiles/Mac/SetupEnvironment.sh -dotnet Engine/Build/BatchFiles/Mac; dotnet Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.dll -Mode=WriteMetadata -Input=$ROOT/unreal/Intermediate/Build/Mac/arm64/CrusaderRemake/$CONFIG/TargetMetadata.json -Version=2" 2>&1 | grep -v "^Setting up\|DotNet/" )
test -f "$ROOT/unreal/Binaries/Mac/CrusaderRemake.target" || { echo "no receipt"; exit 1; }

echo "== cook, stage, pak, archive -> $OUT"
$UE/Engine/Build/BatchFiles/RunUAT.sh BuildCookRun -project=$PROJ -platform=Mac -clientconfig=$CONFIG \
  -skipbuild -cook -stage -pak -archive -archivedirectory=$OUT -nop4 -utf8output -unattended 2>&1 | tee $ROOT/build_mac.log | grep -E "LogCook: Display: Cook|Cooked|Error|error:|BUILD (SUCCESSFUL|FAILED)|AutomationTool exiting|Archive|took" | tail -40
# -archive only copies the bare app; the complete self-contained app (Contents/UE with the pak) is the staged one
rm -rf "$OUT/Mac/CrusaderRemake.app"; mkdir -p "$OUT/Mac"
cp -R "$ROOT/unreal/Saved/StagedBuilds/Mac/CrusaderRemake.app" "$OUT/Mac/CrusaderRemake.app"
echo "== done: $OUT/Mac/CrusaderRemake.app ($(du -sh "$OUT/Mac/CrusaderRemake.app" | cut -f1))"
