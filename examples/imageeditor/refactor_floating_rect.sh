#!/bin/bash
# Refactor floating selection pos+size into rect

for f in *.c; do
  echo "Processing $f..."
  sed -i '' \
    -e 's/\.floating\.pos\.x\([^.]\)/\.floating.rect.x\1/g' \
    -e 's/\.floating\.pos\.y\([^.]\)/\.floating.rect.y\1/g' \
    -e 's/\.floating\.size\.w\([^.]\)/\.floating.rect.w\1/g' \
    -e 's/\.floating\.size\.h\([^.]\)/\.floating.rect.h\1/g' \
    -e 's/\.floating\.pos\([^.]\)/\.floating.rect\1/g' \
    "$f"
done
echo "Done!"
