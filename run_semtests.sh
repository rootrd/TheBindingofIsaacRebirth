#!/bin/bash
cd /mnt/f/TheBindingofIsaacRebirth/repentogxm/recomp || exit 1
for t in test_mul_flag_semantics test_narrow_pair_semantics test_gpr_locals_semantics test_sse_lower_semantics test_leaf_inline_semantics; do
  printf '%s: ' "$t"
  if timeout 180 python3 "$t.py" --cc gcc32 > "/tmp/$t.log" 2>&1; then
    echo PASS
  else
    echo FAIL
  fi
done
