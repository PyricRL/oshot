#!/bin/sh
set -e

OUT="./include/version.h"
TMP="$(mktemp)"

# If we're outside a git checkout and already have a version header, keep it.
if [ ! -d ./.git ] && [ -f "$OUT" ]; then
    rm -f "$TMP"
    exit 0
fi

cp ./include/version.h.in "$TMP"

HASH=${HASH-$(git rev-parse HEAD)}
BRANCH=${BRANCH-$(git branch --show-current)}
MESSAGE=${MESSAGE-$(git log -1 --pretty=%B | head -n1 | sed -e 's/#//g' -e 's/\"//g')}
DATE=${DATE-$(git show --no-patch --format=%cd --date=local)}
DIRTY=${DIRTY-$(git diff-index --quiet HEAD && echo clean || echo dirty)}
TAG=${TAG-$(git describe --tags)}
COMMITS=${COMMITS-$(git rev-list --count HEAD)}

sed -i -e "s#@HASH@#$HASH#" "$TMP"
sed -i -e "s#@BRANCH@#$BRANCH#" "$TMP"
sed -i -e "s#@MESSAGE@#$MESSAGE#" "$TMP"
sed -i -e "s#@DATE@#$DATE#" "$TMP"
sed -i -e "s#@DIRTY@#$DIRTY#" "$TMP"
sed -i -e "s#@TAG@#$TAG#" "$TMP"
sed -i -e "s#@COMMITS@#$COMMITS#" "$TMP"

if [ ! -f "$OUT" ] || ! cmp -s "$TMP" "$OUT"; then
    mv "$TMP" "$OUT"
else
    rm "$TMP"
fi
