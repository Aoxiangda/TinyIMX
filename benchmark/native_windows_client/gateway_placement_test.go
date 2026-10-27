package main

import "testing"

func TestGatewayPlacementPortBoundsAndModes(t *testing.T) {
	for _, x := range []struct {
		a, b int
		m    string
	}{{9000, 0, "loadbalance"}, {29110, 29111, "cross"}, {29110, 29111, "local"}} {
		if validateGatewayPlacement(x.a, x.b, x.m) != nil {
			t.Fatal(x)
		}
	}
	for _, x := range []struct {
		a, b int
		m    string
	}{{0, 1, "cross"}, {65536, 1, "local"}, {1, 0, "local"}, {1, 65536, "cross"}, {1, 1, "local"}, {1, 2, "loadbalance"}, {1, 2, "wrong"}} {
		if validateGatewayPlacement(x.a, x.b, x.m) == nil {
			t.Fatal(x)
		}
	}
}

func TestGatewayPlacementFullUIDPairAndConnectionBalance(t *testing.T) {
	for _, mode := range []string{"cross", "local"} {
		counts := map[int]int{}
		for i := 0; i < 10000; i++ {
			port := gatewayPortForActor(i, 29110, 29111, mode)
			peerPort := gatewayPortForActor(i^1, 29110, 29111, mode)
			counts[port]++
			if (port == peerPort) != (mode == "local") {
				t.Fatal(mode, i)
			}
			if gatewayPortForActor(i, 9000, 0, "loadbalance") != 9000 {
				t.Fatal("historical destination changed")
			}
		}
		if counts[29110] != 5000 || counts[29111] != 5000 {
			t.Fatal(counts)
		}
	}
}
