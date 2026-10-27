package main

import "testing"

func TestActualConfiguredOwnedSenderBounds(t *testing.T) {
	for _, n := range []int{2, 10000, 20000, 40000, 60000, 64000} {
		for _, uid := range []uint64{500001, 500000 + uint64(n)} {
			if !validOwnedSender(uid, n) {
				t.Fatal(n, uid)
			}
		}
		for _, uid := range []uint64{0, 500000, 500001 + uint64(n), ^uint64(0)} {
			if validOwnedSender(uid, n) {
				t.Fatal(n, uid)
			}
		}
	}
	for _, n := range []int{-2, 0, 1, 3, 64002} {
		if validOwnedSender(500001, n) {
			t.Fatal(n)
		}
	}
}
