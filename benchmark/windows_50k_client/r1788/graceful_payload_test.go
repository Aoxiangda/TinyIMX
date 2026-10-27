package main

import (
	"crypto/sha256"
	"fmt"
	"os"
	"path/filepath"
	"strings"
	"sync/atomic"
	"testing"
	"time"
)

func TestPayloadIndependentVectors(t *testing.T) {
	if got := fmt.Sprintf("%x", sha256.Sum256([]byte(makePayload("wtest-u500001-n0", 128, "xorshift64")))); got != "cdced7612513b85a1c5b04ae7064620018819c7bea5efdd465cabf486b11e885" {
		t.Fatal(got)
	}
	if got := fmt.Sprintf("%x", sha256.Sum256([]byte(makePayload("wtest-u500002-n123", 4096, "xorshift64")))); got != "05aea91ca8e701dd1cbb49298419a2bf8aefc7c6196858bae25edc41ee38d353" {
		t.Fatal(got)
	}
	if got := fmt.Sprintf("%x", sha256.Sum256([]byte(makePayload("wtest-u563999-n999", 16384, "xorshift64")))); got != "06246a3d1f54c10d10ab75710a5a9a12cc1ac075646deee62f67fefc7d9dfe13" {
		t.Fatal(got)
	}
	if makePayload("owned", 128, "repeat") != "owned|"+strings.Repeat("x", 122) {
		t.Fatal("old repeat corpus changed")
	}
}
func TestControllerStopExactlyOnceAndCancelable(t *testing.T) {
	path := filepath.Join(t.TempDir(), "controller-stop")
	done := make(chan struct{})
	var called atomic.Int32
	go watchControllerStop(path, done, func() { called.Add(1) })
	time.Sleep(120 * time.Millisecond)
	if called.Load() != 0 {
		t.Fatal("stopped before file")
	}
	if err := os.WriteFile(path, []byte("stop"), 0600); err != nil {
		t.Fatal(err)
	}
	deadline := time.Now().Add(time.Second)
	for called.Load() == 0 && time.Now().Before(deadline) {
		time.Sleep(10 * time.Millisecond)
	}
	if called.Load() != 1 {
		t.Fatal(called.Load())
	}
	time.Sleep(120 * time.Millisecond)
	if called.Load() != 1 {
		t.Fatal("duplicate stop")
	}
	close(done)
	done2 := make(chan struct{})
	close(done2)
	watchControllerStop(path, done2, func() { t.Fatal("canceled watcher fired") })
}
