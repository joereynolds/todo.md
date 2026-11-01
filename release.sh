#!/usr/bin/env bash
# Cut a release: bump project(todomd VERSION …) in CMakeLists.txt, run the tests,
# commit, tag vX.Y.Z and push both. The tag triggers .github/workflows/release.yml,
# which builds the AppImage and attaches it to a GitHub release.
#
#   ./release.sh [-y] [patch|minor|major|X.Y.Z]    (default: patch)
#   make release [BUMP=minor]
#
#   -y   don't ask for confirmation
set -euo pipefail

cd "$(dirname "$0")"

yes=0
bump=patch
for arg in "$@"; do
    case "$arg" in
        -y|--yes) yes=1 ;;
        -h|--help) sed -n '2,9p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) bump=$arg ;;
    esac
done

die() { echo "release: $*" >&2; exit 1; }

current=$(sed -nE 's/^project\(todomd VERSION ([0-9]+\.[0-9]+\.[0-9]+) .*/\1/p' CMakeLists.txt)
[[ -n $current ]] || die "can't find project(todomd VERSION x.y.z) in CMakeLists.txt"
IFS=. read -r major minor patch <<<"$current"

case "$bump" in
    patch) next="$major.$minor.$((patch + 1))" ;;
    minor) next="$major.$((minor + 1)).0" ;;
    major) next="$((major + 1)).0.0" ;;
    [0-9]*.[0-9]*.[0-9]*) next=$bump ;;
    *) die "unknown bump '$bump' (use patch, minor, major or X.Y.Z)" ;;
esac
[[ $next =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || die "bad version '$next'"
tag="v$next"

# Preconditions: on master, clean, in sync with origin, tag unused.
branch=$(git rev-parse --abbrev-ref HEAD)
[[ $branch == master ]] || die "on branch '$branch', not master"
[[ -z $(git status --porcelain) ]] || die "working tree not clean; commit or stash first"
git fetch --quiet --tags origin
[[ -z $(git rev-list HEAD..origin/master) ]] || die "behind origin/master; pull first"
git rev-parse -q --verify "refs/tags/$tag" >/dev/null && die "tag $tag already exists"
git ls-remote --exit-code --tags origin "refs/tags/$tag" >/dev/null 2>&1 && die "tag $tag already exists on origin"

echo "Releasing $current -> $next ($tag)"
echo "Commits since the last release:"
last=$(git describe --tags --abbrev=0 --match 'v*' 2>/dev/null || true)
git --no-pager log --oneline ${last:+"$last"..}HEAD | sed 's/^/  /'
if (( !yes )); then
    read -r -p "Run tests, commit, tag and push? [y/N] " answer
    [[ $answer == [yY]* ]] || die "aborted"
fi

make test

sed -i -E "s/^(project\(todomd VERSION )$current( .*)/\1$next\2/" CMakeLists.txt
grep -q "project(todomd VERSION $next " CMakeLists.txt || die "failed to update CMakeLists.txt"

git commit --quiet -m "Release $tag" CMakeLists.txt
git tag -a "$tag" -m "todomd $next"
git push --atomic origin master "$tag"

url=$(git remote get-url origin | sed -E 's#^git@github.com:#https://github.com/#; s#\.git$##')
echo "Pushed $tag. The release workflow is building the AppImage:"
echo "  $url/actions"
echo "  $url/releases/tag/$tag"
