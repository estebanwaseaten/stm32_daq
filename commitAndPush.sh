#!/bin/bash

MSG="$1"
if [ -z "$MSG" ]; then echo "Usage: $0 <message>"; exit 1; fi

git submodule foreach "git add -A && git commit -m '$MSG' || true"
git add -A
git commit -m "$MSG"

git submodule foreach "git push"
git push
