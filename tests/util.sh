set -euo pipefail

PRG=$(dirname "${BASH_SOURCE[0]}")/../lc

function testcase() {
	local out
	out=$(echo -e "$2" | "$PRG" | tail -n2 | head -n1 | sed "s/λ/'/g")
	echo -n "$1... "
	if [[ "$out" != "$3" ]]; then
		echo -e "\tFAIL"
		echo "   Expected: $3"
		echo "   But got:  $out"
		echo
		echo "Entering debugger."
		(echo "!step on" && echo -e "$2" && cat) | "$PRG" || true
		echo
	else
		echo -e "\tOK"
	fi
}
