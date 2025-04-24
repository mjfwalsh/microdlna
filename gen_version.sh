#!/bin/sh

date=$(date -uR)
hash=UNKNOWN
branch=UNKNOWN
repo=UNKNOWN

# gather git metadata
ref=$(git symbolic-ref -q HEAD)
hash=$(git rev-parse --short $ref)
branch=$(echo $ref | sed -E 's|^refs/heads/||')
upstream=$(git for-each-ref --format='%(upstream:short)' $ref)

if ! test -z "$(git status --porcelain)"
then
    hash="$hash (dirty)"
fi

if ! test -z "$upstream"
then
    short_repo=$(echo $upstream |  sed -E "s|/$branch$||")
    repo=$(git config remote.$short_repo.url)
fi

# save a new version_info.h
cat <<OUT
#ifndef __VERSION_H__
#define __VERSION_H__
#define VERSION_DATE "$date"
#define VERSION_HASH "$hash"
#define VERSION_BRANCH "$branch"
#define VERSION_REPO "$repo"
#endif
OUT

