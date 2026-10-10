#!/bin/bash
# Assignment 1: menu-driven Linux command, scripting and arithmetic demonstration.
set -euo pipefail

cleanup() {
    jobs -pr | xargs -r kill 2>/dev/null || true
    rm -f /dev/shm/er_sim_shm
}
trap cleanup EXIT INT TERM

arithmetic_demo() {
    echo "Assignment 1 arithmetic"
    read -rp "First integer: " a
    read -rp "Second integer: " b
    [[ "$a" =~ ^-?[0-9]+$ && "$b" =~ ^-?[0-9]+$ ]] || { echo "Integers required"; return; }
    echo "sum=$((a+b)) difference=$((a-b)) product=$((a*b))"
    if (( b != 0 )); then echo "quotient=$((a/b)) remainder=$((a%b))"; else echo "quotient/remainder undefined (division by zero)"; fi
}

run_compare() {
    local s f
    s=$(./er_sim --scenario scenarios/classic.csv --policy srtf --no-ui --fast)
    f=$(./er_sim --scenario scenarios/classic.csv --policy fcfs --no-ui --fast)
    echo "Policy Average-TAT Average-WT"
    printf '%s\n' "$s" | awk '/Average TAT/{tat=$3}/Average WT/{print "SRTF",tat,$3}'
    printf '%s\n' "$f" | awk '/Average TAT/{tat=$3}/Average WT/{print "FCFS",tat,$3}'
}

while true; do
    cat <<'MENU'
1. Build
2. Run classic scenario, SRTF
3. Run classic scenario, FCFS
4. Compare SRTF vs FCFS
5. Run ER rush scenario with dashboard
6. Readers-writers sync vs no-sync
7. Cache demo
8. Zombie demo
9. Orphan demo
10. Fork-sort demo
11. Run unit tests
12. Show process tree
13. Clean
14. Assignment 1 arithmetic
0. Exit
MENU
    read -rp "Choice: " choice
    case "$choice" in
        1) make ;;
        2) ./er_sim --scenario scenarios/classic.csv --policy srtf ;;
        3) ./er_sim --scenario scenarios/classic.csv --policy fcfs ;;
        4) run_compare ;;
        5) ./er_sim --scenario scenarios/er_rush.csv ;;
        6)
            echo "With synchronization:"
            ./er_sim --scenario scenarios/er_rush.csv --no-ui --fast | awk '/Records: reads=/{line=$0} END{print line}'
            echo "Without synchronization:"
            ./er_sim --scenario scenarios/er_rush.csv --no-ui --fast --no-sync | awk '/Records: reads=/{line=$0} END{print line}'
            ;;
        7) ./er_sim --demo cache ;;
        8) demos/zombie & sleep 1; ps -el | grep -E 'defunct| Z ' || true; wait ;;
        9) demos/orphan; sleep 4 ;;
        10) demos/fork_sort ;;
        11) make test ;;
        12) pid=$(pgrep -o er_sim || true); [[ -n "$pid" ]] && pstree -p "$pid" || echo "Start er_sim first." ;;
        13) make clean ;;
        14) arithmetic_demo ;;
        0) exit 0 ;;
        *) echo "Invalid choice" ;;
    esac
done
