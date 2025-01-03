set -euo pipefail

PRG=$(dirname "${BASH_SOURCE[0]}")/../lc

function testcase() {
	local out
	out=$(echo -e "$2" | "$PRG" | tail -n2 | head -n1 | sed "s/λ/'/g")
	printf "%-30s" "$1..."
	if [[ "$out" != "$3" ]]; then
		echo -e "FAIL"
		echo "\tExpected: $3"
		echo "\tBut got:  $out"
		echo
		echo "Entering debugger."
		A=$(echo -e "$2" | head -n -1)
		B=$(echo -e "$2" | tail -n 1)
		(echo -e "$A" && echo "!step on" && echo -e "$B" && cat) | "$PRG" || true
		echo
	else
		echo -e "OK"
	fi
}
