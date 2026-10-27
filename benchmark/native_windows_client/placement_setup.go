package main

import "fmt"

func validateSetupOrder(order string, connections int) error {
	if order != "adjacent" && order != "interleave4" && order != "outerpair4" {
		return fmt.Errorf("unknown setup UID order")
	}
	if order != "adjacent" && (connections < 4 || connections%4 != 0) {
		return fmt.Errorf("interleave4 requires a multiple of four connections")
	}
	return nil
}

// Only the setup slot is permuted. actors[], uid, peer, credentials and traffic
// remain indexed by the original UID. Actual gateway placement requires census.
func setupActorIndex(slot int, order string) int {
	if order == "outerpair4" {
		return (slot &^ 3) + [4]int{0, 2, 3, 1}[slot&3]
	}
	if order == "interleave4" {
		return (slot &^ 3) + ((slot & 1) << 1) + ((slot & 2) >> 1)
	}
	return slot
}
