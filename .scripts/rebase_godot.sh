#!/bin/sh

SCRIPT_PATH=$(dirname "$0")
cd "$SCRIPT_PATH/../../.."

git fetch --all
git checkout master
git pull

HEAD=$(git rev-parse HEAD)

# check for the existence of the 'nikitalita' remote
if ! git remote | grep -q "nikitalita"; then
    git remote add nikitalita https://github.com/nikitalita/godot.git
	git fetch nikitalita
fi

git checkout working-branch
git reset --hard $HEAD

BRANCHES_TO_MERGE=(
	gltf-use-orm-texture
	convert-3.x-escn
)

for branch in "${BRANCHES_TO_MERGE[@]}"; do
    # merge branch, use a merge commit
    git merge nikitalita/$branch -m "Merge branch '$branch'"
done
