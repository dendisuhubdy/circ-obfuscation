#!/usr/bin/env bash
#
# Test all circuits
#

scriptdir=$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")
prog=$(readlink -f "$scriptdir/../circobf.sh")
circuits=$(readlink -f "$scriptdir/../circuits")

run () {
    circuit=$1
    scheme=$2
    mmap=$3
    debug=$4

    echo
    echo \*\*\*
    echo \*\*\*
    echo \*\*\* $circuit $scheme $mmap $debug
    echo \*\*\*
    echo \*\*\*
    echo
    $prog --mmap $mmap --scheme $scheme --debug $debug $circuit
}

run_sigma () {
    circuit=$1
    scheme=$2
    mmap=$3
    debug=$4

    echo
    echo \*\*\*
    echo \*\*\*
    echo \*\*\* $circuit $scheme $mmap $debug SIGMA
    echo \*\*\*
    echo \*\*\*
    echo
    $prog --mmap $mmap --scheme $scheme --debug $debug $circuit --sigma --symlen 16
}

for circuit in $circuits/*.acirc; do
    echo "$circuit"
    run "$circuit" LIN DUMMY ERROR
    run "$circuit" LZ  DUMMY ERROR
done

for circuit in $circuits/circuits/*.acirc; do
    echo "$circuit"
    run "$circuit" LIN DUMMY ERROR
    run "$circuit" LZ  DUMMY ERROR
done

for circuit in $circuits/circuits/sigma/*.acirc; do
    echo "$circuit"
    run_sigma "$circuit" LIN DUMMY ERROR
    run_sigma "$circuit" LZ  DUMMY ERROR
done
