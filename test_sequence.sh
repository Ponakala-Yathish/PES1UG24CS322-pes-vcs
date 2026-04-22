#!/usr/bin/env bash
# test_sequence.sh — End-to-end integration test for PES-VCS
set -e

PASS=0
FAIL=0

check() {
    local desc="$1"
    local expected="$2"
    local actual="$3"
    if echo "$actual" | grep -qF "$expected"; then
        echo "PASS: $desc"
        PASS=$((PASS + 1))
    else
        echo "FAIL: $desc"
        echo "  Expected to find: $expected"
        echo "  Got: $actual"
        FAIL=$((FAIL + 1))
    fi
}

# Clean slate
rm -rf .pes test_file*.txt bye.txt hello.txt

# Init
OUT=$(./pes init)
check "init creates .pes" "Initialized empty PES repository" "$OUT"
[ -f .pes/HEAD ] || { echo "FAIL: HEAD file missing"; exit 1; }

# Add files
echo "Hello" > hello.txt
echo "World" > world.txt
./pes add hello.txt
./pes add world.txt

# Status
OUT=$(./pes status)
check "status shows staged hello.txt" "hello.txt" "$OUT"
check "status shows staged world.txt" "world.txt" "$OUT"

# First commit
OUT=$(./pes commit -m "Initial commit")
check "first commit succeeds" "Committed:" "$OUT"

# Second commit
echo "More text" >> hello.txt
./pes add hello.txt
OUT=$(./pes commit -m "Add more text")
check "second commit succeeds" "Committed:" "$OUT"

# Third commit
echo "Goodbye" > bye.txt
./pes add bye.txt
OUT=$(./pes commit -m "Add farewell")
check "third commit succeeds" "Committed:" "$OUT"

# Log
OUT=$(./pes log)
check "log shows Initial commit"  "Initial commit"  "$OUT"
check "log shows Add more text"   "Add more text"   "$OUT"
check "log shows Add farewell"    "Add farewell"    "$OUT"

# HEAD and refs
HEAD_CONTENT=$(cat .pes/HEAD)
check "HEAD is symbolic ref" "ref: refs/heads/main" "$HEAD_CONTENT"
[ -f .pes/refs/heads/main ] || { echo "FAIL: refs/heads/main missing"; FAIL=$((FAIL+1)); }

# Object store exists and is non-empty
OBJ_COUNT=$(find .pes/objects -type f 2>/dev/null | wc -l)
if [ "$OBJ_COUNT" -ge 6 ]; then
    echo "PASS: object store has $OBJ_COUNT objects (≥6 expected)"
    PASS=$((PASS + 1))
else
    echo "FAIL: object store has only $OBJ_COUNT objects (expected ≥6)"
    FAIL=$((FAIL + 1))
fi

# Cleanup
rm -f hello.txt world.txt bye.txt

echo ""
echo "Results: $PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ] && echo "All integration tests passed!" && exit 0
exit 1
