package main

import (
	"fmt"
	"io"
	"net"
	"os"
	"syscall"
	"testing"
	"time"
)

func TestActual50000ExclusivePinsNoNetworkTraffic(t *testing.T) {
	path := os.Getenv("TINYIMX_50K_CANDIDATE_PROOF")
	if path == "" {
		t.Skip("explicit finite local reservation proof not requested")
	}
	ports, e := readExclusiveCandidates(path, 50000)
	if e != nil {
		t.Fatal(e)
	}
	p, e := reserveExclusivePorts(ports, 50000)
	if e != nil {
		t.Fatal(e)
	}
	defer p.close()
	if len(p.actorPorts()) != 50000 || p.initialReserved < 50000 {
		t.Fatal("50K pins missing")
	}
	for _, actor := range []int{0, 25000, 49999} {
		l, e := net.Listen("tcp4", fmt.Sprint("0.0.0.0:", p.actorPorts()[actor]))
		if e == nil {
			l.Close()
			t.Fatal("actual50K reservation lost isolation")
		}
	}
	t.Logf("actual exclusive bound sockets=%d actors=50000; no Listen/Connect or packets", p.initialReserved)
}

func TestHandoffCollisionConsumesDeclaredReservedSpare(t *testing.T) {
	server, e := net.Listen("tcp4", "127.0.0.1:0")
	if e != nil {
		t.Fatal(e)
	}
	defer server.Close()
	port, spare := freeCandidate(t), freeCandidate(t)
	for spare == port {
		spare = freeCandidate(t)
	}
	p, e := reserveExclusivePorts([]int{port, spare}, 1)
	if e != nil {
		t.Fatal(e)
	}
	defer p.close()
	// Reproduce the observed close/bind race with a real ordinary listener.
	if e = syscall.Closesocket(p.actors[0].handle); e != nil {
		t.Fatal(e)
	}
	p.actors[0].handle = syscall.InvalidHandle
	foreign, e := net.Listen("tcp4", fmt.Sprint("0.0.0.0:", port))
	if e != nil {
		t.Fatal(e)
	}
	defer foreign.Close()
	c, e := p.dialer(0)("tcp4", server.Addr().String(), time.Second)
	if e != nil {
		t.Fatal(e)
	}
	defer c.Close()
	accepted, e := server.Accept()
	if e != nil {
		t.Fatal(e)
	}
	accepted.Close()
	if c.LocalAddr().(*net.TCPAddr).Port != spare || len(p.events) != 1 || p.events[0].Port != port {
		t.Fatal("did not preserve collision and use declared spare")
	}
	stillForeign, e := net.DialTimeout("tcp4", fmt.Sprint("127.0.0.1:", port), time.Second)
	if e != nil {
		t.Fatal("foreign listener damaged:", e)
	}
	stillForeign.Close()
}

func freeCandidate(t *testing.T) int {
	t.Helper()
	l, e := net.Listen("tcp4", "127.0.0.1:0")
	if e != nil {
		t.Fatal(e)
	}
	p := l.Addr().(*net.TCPAddr).Port
	l.Close()
	return p
}
func TestExclusiveReservationDoesNotOverrideOrdinaryListener(t *testing.T) {
	l, e := net.Listen("tcp4", "0.0.0.0:0")
	if e != nil {
		t.Fatal(e)
	}
	defer l.Close()
	port := l.Addr().(*net.TCPAddr).Port
	p, e := reserveExclusivePorts([]int{port}, 1)
	if e == nil {
		p.close()
		t.Fatal("overrode an existing ordinary application listener")
	}
	c, e := net.DialTimeout("tcp4", fmt.Sprint("127.0.0.1:", port), time.Second)
	if e != nil {
		t.Fatalf("foreign listener damaged: %v", e)
	}
	c.Close()
}
func TestExclusiveReservationHoldsFuturePort(t *testing.T) {
	port := freeCandidate(t)
	p, e := reserveExclusivePorts([]int{port}, 1)
	if e != nil {
		t.Fatal(e)
	}
	defer p.close()
	l, e := net.Listen("tcp4", fmt.Sprint("0.0.0.0:", port))
	if e == nil {
		l.Close()
		t.Fatal("future actor port was not protected")
	}
	if p.snapshot()["initial_reserved"] != 1 {
		t.Fatal("reservation missing")
	}
}
func TestExclusiveReservationRealHandoffAndPayload(t *testing.T) {
	l, e := net.Listen("tcp4", "127.0.0.1:0")
	if e != nil {
		t.Fatal(e)
	}
	defer l.Close()
	done := make(chan error, 1)
	go func() {
		c, e := l.Accept()
		if e != nil {
			done <- e
			return
		}
		defer c.Close()
		c.SetDeadline(time.Now().Add(2 * time.Second))
		b := make([]byte, 16)
		_, e = io.ReadFull(c, b)
		if e == nil {
			_, e = c.Write(b)
		}
		done <- e
	}()
	port := freeCandidate(t)
	p, e := reserveExclusivePorts([]int{port}, 1)
	if e != nil {
		t.Fatal(e)
	}
	defer p.close()
	c, e := p.dialer(0)("tcp4", l.Addr().String(), time.Second)
	if e != nil {
		t.Fatal(e)
	}
	defer c.Close()
	c.SetDeadline(time.Now().Add(2 * time.Second))
	if c.LocalAddr().(*net.TCPAddr).Port != port {
		t.Fatal("source changed")
	}
	raw := []byte("exclusive-echo!!")
	if len(raw) != 16 {
		t.Fatal("bad fixture")
	}
	if _, e = c.Write(raw); e != nil {
		t.Fatal(e)
	}
	got := make([]byte, 16)
	if _, e = io.ReadFull(c, got); e != nil || string(got) != string(raw) {
		t.Fatalf("payload mismatch %v", e)
	}
	if e = <-done; e != nil {
		t.Fatal(e)
	}
	if p.snapshot()["handoffs"] != 1 {
		t.Fatal("handoff not recorded")
	}
}
func TestExclusiveReservationRecordsCandidateCollision(t *testing.T) {
	l, e := net.Listen("tcp4", "0.0.0.0:0")
	if e != nil {
		t.Fatal(e)
	}
	defer l.Close()
	busy := l.Addr().(*net.TCPAddr).Port
	free := freeCandidate(t)
	p, e := reserveExclusivePorts([]int{busy, free}, 1)
	if e != nil {
		t.Fatal(e)
	}
	defer p.close()
	if p.actorPorts()[0] != free || len(p.events) != 1 || p.events[0].Port != busy {
		t.Fatal("collision was not preserved")
	}
}
func TestExclusiveReservationReleasesAllUnconnectedPins(t *testing.T) {
	port := freeCandidate(t)
	p, e := reserveExclusivePorts([]int{port}, 1)
	if e != nil {
		t.Fatal(e)
	}
	p.close()
	p.close()
	h, e := syscall.Socket(syscall.AF_INET, syscall.SOCK_STREAM, syscall.IPPROTO_TCP)
	if e != nil {
		t.Fatal(e)
	}
	defer syscall.Closesocket(h)
	if e = syscall.SetsockoptInt(h, syscall.SOL_SOCKET, -5, 1); e != nil {
		t.Fatal(e)
	}
	if e = syscall.Bind(h, &syscall.SockaddrInet4{Port: port}); e != nil {
		t.Fatalf("reservation leaked: %v", e)
	}
}
