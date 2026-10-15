#!/bin/bash
# htdemucs 的输出（OUT/htdemucs/<id>/{vocals,no_vocals}.wav）→ train.py 要的 SEP/<id>/{vocals,inst,mix}.wav
# 用法：link_demucs.sh CORPUS_DIR   （原曲从 CORPUS_DIR/audio/*/<id>.mp3 解码）
C=$1; mkdir -p $C/sep_ht_lnk
for d in $C/sep_ht/htdemucs/*/; do
  id=$(basename $d); o=$C/sep_ht_lnk/$id
  [ -f $d/no_vocals.wav ] || continue
  [ -f $o/mix.wav ] && continue
  mkdir -p $o; ln -sf $d/vocals.wav $o/vocals.wav; ln -sf $d/no_vocals.wav $o/inst.wav
  src=$(ls $C/audio/*/$id.* | head -1)
  ffmpeg -v error -y -i "$src" -ac 2 -ar 44100 -c:a pcm_f32le $o/mix.wav
done
