#!/bin/bash
# report.sh [board ...]: DRC finding counts per type, and what the last routing run left unrouted.
HW=$(git -C "$(dirname "$0")" rev-parse --show-toplevel)/hardware
for b in ${@:-stack-bottom stack-top mini}; do
  echo "== $b"
  grep "^\[" "$HW/$b/samplooler-$b-drc.rpt" | sed 's/\].*/]/' | sort | uniq -c
  awk '/Auto-routing stage started/{buf=""} {buf=buf"\n"$0} END{print buf}' "$HW/$b/freerouting.log" 2>/dev/null \
    | grep -A3 "Net '" | grep -E "Net '|->" | paste - - | cut -c1-160 | head
done
