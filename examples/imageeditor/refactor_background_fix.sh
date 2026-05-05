#!/bin/bash
# Fix remaining background_color and show_background references

for f in *.c; do
  echo "Processing $f..."
  sed -i '' \
    -e 's/->background_color\([^.]\)/->background.color\1/g' \
    -e 's/->show_background\([^.]\)/->background.show\1/g' \
    "$f"
done
echo "Done!"
