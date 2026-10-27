package main

import "fmt"

func validateGatewayPlacement(first, second int, mode string) error {
	if first < 1 || first > 65535 {
		return fmt.Errorf("first gateway port out of range")
	}
	if mode == "loadbalance" {
		if second != 0 {
			return fmt.Errorf("single ingress requires second port0")
		}
		return nil
	}
	if mode != "cross" && mode != "local" {
		return fmt.Errorf("invalid gateway placement")
	}
	if second < 1 || second > 65535 || second == first {
		return fmt.Errorf("two distinct owned direct gateway ports required")
	}
	return nil
}

// Actor index is still the UID index used for credentials, pairing and sends.
// Only the setup TCP destination changes; route census remains authoritative.
func gatewayPortForActor(index, first, second int, mode string) int {
	selectSecond := false
	if mode == "cross" {
		selectSecond = index%2 == 1
	}
	if mode == "local" {
		selectSecond = (index/2)%2 == 1
	}
	if selectSecond {
		return second
	}
	return first
}
