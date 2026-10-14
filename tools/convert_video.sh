#!/bin/zsh
# Convert a Crusader FLICS/*.AVI to an H.264 MP4 (frames via tools/video.py, audio via ffmpeg).
# usage: tools/convert_video.sh in.AVI out.mp4 [--palette GAMEPAL.PAL] [--scale N]
set -e
IN=$1; OUT=$2; shift 2
HERE=$(cd "$(dirname "$0")" && pwd)
TMP=$(mktemp -d)
"$HERE/../.venv/bin/python" "$HERE/video.py" "$IN" "$TMP/frames" "$@" >/dev/null
if ffprobe -v error -select_streams a -show_entries stream=codec_type -of csv=p=0 "$IN" 2>/dev/null | grep -q audio; then
  ffmpeg -v error -y -i "$IN" -vn -acodec pcm_s16le "$TMP/audio.wav"
  ffmpeg -v error -y -framerate 15 -i "$TMP/frames/%05d.png" -i "$TMP/audio.wav" -vf "pad=ceil(iw/2)*2:ceil(ih/2)*2" -c:v libx264 -pix_fmt yuv420p -crf 16 -c:a aac -b:a 128k -shortest "$OUT"
else
  ffmpeg -v error -y -framerate 15 -i "$TMP/frames/%05d.png" -vf "pad=ceil(iw/2)*2:ceil(ih/2)*2" -c:v libx264 -pix_fmt yuv420p -crf 16 "$OUT"
fi
rm -rf "$TMP"
echo "$OUT"
