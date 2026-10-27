package main

import "testing"

func TestPlacementOrderValidation(t *testing.T) {
	for _, n := range []int{4, 8, 100, 10000} {
		if validateSetupOrder("interleave4", n) != nil {
			t.Fatal(n)
		}
	}
	for _, n := range []int{-4, 0, 2, 6, 9998} {
		if validateSetupOrder("interleave4", n) == nil {
			t.Fatal(n)
		}
	}
	if validateSetupOrder("adjacent", 2) != nil || validateSetupOrder("local", 10000) == nil {
		t.Fatal("validation")
	}
}

func TestPlacementIdentityAndBijection(t *testing.T) {
	for n := 4; n <= 10000; n += 4 {
		seen := make([]bool, n)
		for slot := 0; slot < n; slot++ {
			if setupActorIndex(slot, "adjacent") != slot {
				t.Fatal("default changed")
			}
			i := setupActorIndex(slot, "interleave4")
			if i < 0 || i >= n || seen[i] {
				t.Fatal("not a permutation", n, slot, i)
			}
			seen[i] = true
			if setupActorIndex(i, "interleave4") != slot {
				t.Fatal("not an involution")
			}
		}
	}
}

func TestPlacementBalancedPairsBySetupParity(t *testing.T) {
	for _, mode := range []string{"adjacent", "interleave4"} {
		n := 10000
		parity := make([]int, n)
		for slot := 0; slot < n; slot++ {
			parity[setupActorIndex(slot, mode)] = slot % 2
		}
		counts := [2]int{}
		for i := 0; i < n; i++ {
			counts[parity[i]]++
			same := parity[i] == parity[i^1]
			if same != (mode == "interleave4") {
				t.Fatal("pair", mode, i)
			}
		}
		if counts != [2]int{5000, 5000} {
			t.Fatal(counts)
		}
	}
}

func TestPlacementPreservesCredentialsAndTrafficIndex(t *testing.T) {
	for slot := 0; slot < 10000; slot++ {
		i := setupActorIndex(slot, "interleave4")
		uid, peer := 500001+uint64(i), 500001+uint64(i^1)
		if uid == peer || (peer-500001)^1 != uid-500001 {
			t.Fatal("peer mapping")
		}
		if 500000+(i+1) != int(uid) {
			t.Fatal("credential UID mismatch")
		}
	}
}

func TestOuterPairValidation(t *testing.T) {
	for _, n := range []int{4, 8, 10000} {
		if validateSetupOrder("outerpair4", n) != nil {
			t.Fatal(n)
		}
	}
	for _, n := range []int{-4, 0, 2, 6, 9998} {
		if validateSetupOrder("outerpair4", n) == nil {
			t.Fatal(n)
		}
	}
}

func TestOuterPairPermutationAndPeerMapping(t *testing.T) {
	seen := make([]bool, 10000)
	for slot := 0; slot < 10000; slot++ {
		i := setupActorIndex(slot, "outerpair4")
		if i < 0 || i >= 10000 || seen[i] {
			t.Fatal(slot, i)
		}
		seen[i] = true
		if (i^1)^1 != i {
			t.Fatal("peer")
		}
	}
}

func TestOuterPairCapturedLeastConnPatterns(t *testing.T) {
	// Both tie orientations of the actual captured ABBA/BAAB four-slot pattern.
	// This is a setup calibration, not a guarantee about future Nginx routing.
	for offset := 0; offset < 2; offset++ {
		actual := make([]int, 10000)
		counts := [2]int{}
		for slot := 0; slot < 10000; slot++ {
			gateway := [4]int{0, 1, 1, 0}[slot&3] ^ offset
			i := setupActorIndex(slot, "outerpair4")
			actual[i] = gateway
			counts[gateway]++
		}
		if counts != [2]int{5000, 5000} {
			t.Fatal(counts)
		}
		for i := 0; i < 10000; i++ {
			if actual[i] != actual[i^1] {
				t.Fatal("nonlocal", i)
			}
		}
	}
}
