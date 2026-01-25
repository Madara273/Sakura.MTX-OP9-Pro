#!/bin/bash
#
# EXAMPLES
#	./cherry-pick.sh <commit-hash>
#

if [ $# -eq 0 ]; then
	echo "Usage: $0 <commit-hash>"
	exit 1
fi

commit_hash="$1"

git cherry-pick "$@" -x -s -m 1
rc=$?

if [ $rc -ne 0 ]; then
	echo "Cherry-pick failed"
	exit 1
fi

git_name=$(git config user.name)
git_email=$(git config user.email)

if [ -z "$git_name" ] || [ -z "$git_email" ]; then
	echo "Error: git user.name or user.email is not set"
	exit 1
fi

target_signature="Signed-off-by: $git_name <$git_email>"
tmp=$(mktemp)

# clean commit message
while IFS= read -r line; do
	# remove duplicate Signed-off-by
	if [[ "$line" == "$target_signature" ]]; then
		continue
	fi

	# remove ALL cherry-pick markers
	if [[ "$line" =~ ^\(cherry\ picked\ from\ commit\ .* ]]; then
		continue
	fi

	echo "$line"
done <<< "$(git log -1 --pretty=%B)" > "$tmp"

# normalize + add ONE correct footer
{
	git stripspace < "$tmp"
	echo
	echo "(cherry picked from commit $commit_hash)"
	echo "$target_signature"
} > "${tmp}.final"

git commit --amend --no-edit -F "${tmp}.final"

rm -f "$tmp" "${tmp}.final"

echo "Cherry-pick successful! Message cleaned."
exit 0
