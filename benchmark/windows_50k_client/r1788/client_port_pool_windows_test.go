package main

import (
	"fmt"
	"net"
	"os"
	"strconv"
	"sync"
	"testing"
	"time"
)

func TestClientPortPoolBounds(t *testing.T) {
	for _, v := range [][2]int{{2, 0}, {10000, 0}, {20000, 20000}, {20000, 45536}, {40000, 1025}, {60000, 1025}, {64000, 1025}} {
		if validateClientPortPool(v[0], v[1]) != nil {
			t.Fatal(v)
		}
	}
	for _, v := range [][2]int{{20000, 0}, {64002, 1025}, {20000, 45537}, {2, 1023}, {3, 20000}} {
		if validateClientPortPool(v[0], v[1]) == nil {
			t.Fatal(v)
		}
	}
}

func TestActualTwentyThousandExclusiveClientPorts(t *testing.T) {
	if os.Getenv("TINYIMX_CALIBRATE_20K_PORTS") != "1" {
		t.Skip("explicit real20K loopback calibration is a separate bounded experiment")
	}
	total := 20000
	if raw := os.Getenv("TINYIMX_CALIBRATION_PORT_COUNT"); raw != "" {
		var err error
		total, err = strconv.Atoi(raw)
		if err != nil || (total != 20000 && total != 40000 && total != 60000 && total != 64000) {
			t.Fatal("exact owned calibration count required", raw)
		}
	}
	const base = 0
	ports, e := readClientPortPool(os.Getenv("TINYIMX_CALIBRATION_PORT_FILE"), total)
	if e != nil {
		t.Fatal(e)
	}
	listeners := []net.Listener{}
	accepted := make(chan net.Conn, total)
	errors := make(chan error, total)
	var acceptWG sync.WaitGroup
	acceptErrors := make(chan error, 2)
	for j := 0; j < 2; j++ {
		ln, e := net.Listen("tcp4", "127.0.0.1:0")
		if e != nil {
			t.Fatal(e)
		}
		listeners = append(listeners, ln)
		acceptWG.Add(1)
		go func(ln net.Listener) {
			defer acceptWG.Done()
			for {
				c, e := ln.Accept()
				if e != nil {
					acceptErrors <- e
					return
				}
				accepted <- c
			}
		}(ln)
	}
	clients := make([]net.Conn, total)
	defer func() {
		for _, c := range clients {
			if c != nil {
				c.Close()
			}
		}
		for _, l := range listeners {
			l.Close()
		}
		acceptWG.Wait()
		close(accepted)
		for c := range accepted {
			c.Close()
		}
	}()
	jobs := make(chan int)
	var workers sync.WaitGroup
	for j := 0; j < 32; j++ {
		workers.Add(1)
		go func() {
			defer workers.Done()
			for i := range jobs {
				c, e := setupDialerForActor(clientPortForActor(base, i, ports), 0)("tcp4", listeners[i%2].Addr().String(), 5*time.Second)
				if e != nil {
					errors <- fmt.Errorf("actor%d: %w", i, e)
					continue
				}
				clients[i] = c
			}
		}()
	}
	started := time.Now()
	for i := 0; i < total; i++ {
		select {
		case e := <-acceptErrors:
			t.Errorf("ACCEPT_EARLY_TERMINAL after%d: %v", i, e)
			i = total
		default:
		}
		if i == total {
			break
		}
		jobs <- i
		time.Sleep(10 * time.Millisecond)
	}
	close(jobs)
	workers.Wait()
	close(errors)
	errorCount := 0
	for e := range errors {
		if errorCount < 10 {
			t.Error(e)
		}
		errorCount++
	}
	if errorCount > 0 {
		t.Errorf("total_client_setup_failures=%d", errorCount)
	}
	if t.Failed() {
		return
	}
	deadline := time.Now().Add(10 * time.Second)
	for len(accepted) != total && time.Now().Before(deadline) {
		time.Sleep(10 * time.Millisecond)
	}
	if len(accepted) != total {
		t.Fatalf("accepted%d expected%d", len(accepted), total)
	}
	seenPorts := map[int]bool{}
	dest := map[string]int{}
	for i, c := range clients {
		p := c.LocalAddr().(*net.TCPAddr).Port
		if p != ports[i] || seenPorts[p] {
			t.Fatal("source pool mismatch", i, p)
		}
		seenPorts[p] = true
		dest[c.RemoteAddr().String()]++
	}
	if len(seenPorts) != total || len(dest) != 2 {
		t.Fatal(len(seenPorts), dest)
	}
	for _, n := range dest {
		if n != total/2 {
			t.Fatal(dest)
		}
	}
	// An exclusive occupied source port must reject another socket, not hijack it.
	if c, e := setupDialerForActor(ports[0], 0)("tcp4", listeners[1].Addr().String(), time.Second); e == nil {
		c.Close()
		t.Fatal("exclusive client binding was reused")
	}
	t.Logf("ACTUAL20K unique_source_ports=%d endpoint_counts=%v setup_seconds=%.3f exclusive_collision_rejected=true", len(seenPorts), dest, time.Since(started).Seconds())
}

func TestExplicitClientPoolInputValidation(t *testing.T) {
	dir := t.TempDir()
	p := dir + "/ports.json"
	for _, raw := range []string{"[10000,10000]", "[1024,10001]", "[10000,65536]", "[10000]", "[10000,10001,10002]", "bad"} {
		if e := os.WriteFile(p, []byte(raw), 0600); e != nil {
			t.Fatal(e)
		}
		if _, e := readClientPortPool(p, 2); e == nil {
			t.Fatal(raw)
		}
	}
	if e := os.WriteFile(p, []byte("[10000,65535]"), 0600); e != nil {
		t.Fatal(e)
	}
	v, e := readClientPortPool(p, 2)
	if e != nil || len(v) != 2 || clientPortForActor(0, 1, v) != 65535 {
		t.Fatal(v, e)
	}
	if clientPortForActor(0, 9, nil) != 0 || clientPortForActor(20000, 9, nil) != 20009 {
		t.Fatal("legacy and contiguous behavior changed")
	}
}
