#!/bin/sh

SCRIPT_PATH=$(dirname "$0")
GDRE_PATH=$(realpath "$SCRIPT_PATH/..")
cd "$SCRIPT_PATH/../../.."

git fetch --all
git checkout master
git pull

HEAD=$(git rev-parse HEAD)
SHORT_HASH=$(git rev-parse --short HEAD)
NEW_BRANCH_NAME="gdre-wb-$SHORT_HASH"

# check for the existence of the 'nikitalita' remote
if ! git remote | grep -q "nikitalita"; then
    git remote add nikitalita https://github.com/nikitalita/godot.git
	git fetch nikitalita
fi

# check if the branch already exists
if git branch -a | grep -q $NEW_BRANCH_NAME; then
    git branch -D $NEW_BRANCH_NAME
fi
git branch -C $NEW_BRANCH_NAME

git checkout $NEW_BRANCH_NAME
git reset --hard $HEAD

BRANCHES_TO_MERGE=(
	material-fix-deprecated-param
	fix-pack-error
	convert-3.x-escn
	fix-compat-array-shapes
	fix-diraccess-windows
	fix-cli-parser
	gltf-fix-skeleton-bone
	gltf-fix-double-precision
	gltf-fix-vertex-colors
	fix-v3-meshes
	fix-clearcoat-gloss
	gltf-mutex-all-document-extensions
)

# set fail on error
for branch in "${BRANCHES_TO_MERGE[@]}"; do
    # merge branch, use a merge commit
    git merge nikitalita/$branch -m "Merge branch '$branch'"
	if [ $? -ne 0 ]; then
		echo "Error: Failed to merge branch '$branch'"
		exit 1
	fi
done

# detect OS for cross-platform sed compatibility
# macOS (BSD sed) requires -i '' while Linux (GNU sed) uses -i
if [ "$(uname)" = "Darwin" ]; then
    sed_in_place() { sed -i '' "$@"; }
else
    sed_in_place() { sed -i "$@"; }
fi

# git push nikitalita $NEW_BRANCH_NAME --set-upstream

# change the branch name in .github/workflows/all_builds.yml and the README.md
sed_in_place "s/GODOT_MAIN_SYNC_REF: .*/GODOT_MAIN_SYNC_REF: $NEW_BRANCH_NAME/" "$GDRE_PATH/.github/workflows/all_builds.yml"
sed_in_place "s/ @ branch \`.*\`/ @ branch \`$NEW_BRANCH_NAME\`/" "$GDRE_PATH/README.md"
