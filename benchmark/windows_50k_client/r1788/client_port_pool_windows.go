package main

import (
	"encoding/json"
	"fmt"
	"net"
	"os"
	"syscall"
	"time"
)

func validateClientPortPool(connections, base int) error {
	if connections < 2 || connections > 64000 || connections%2 != 0 {
		return fmt.Errorf("owned actor count must be even and within2..64000")
	}
	if base == 0 {
		if connections > 10000 {
			return fmt.Errorf("over10K requires a verified explicit client port pool")
		}
		return nil
	}
	if base < 1024 || base+connections-1 > 65535 {
		return fmt.Errorf("client port pool exceeds unprivileged TCP port range")
	}
	return nil
}

// Each actor owns a distinct local port. SO_EXCLUSIVEADDRUSE prevents sharing
// any pre-existing application binding. No SO_REUSEADDR or system TCP changes.
// Any collision is a recorded setup failure, never silently skipped or reused.
func setupDialerForActor(base, index int) func(string, string, time.Duration) (net.Conn, error) {
	if base == 0 {
		return net.DialTimeout
	}
	return func(network, address string, timeout time.Duration) (net.Conn, error) {
		d := net.Dialer{Timeout: timeout, LocalAddr: &net.TCPAddr{Port: base + index}, Control: func(network, address string, raw syscall.RawConn) error {
			var optionErr error
			e := raw.Control(func(fd uintptr) { optionErr = syscall.SetsockoptInt(syscall.Handle(fd), syscall.SOL_SOCKET, -5, 1) })
			if e != nil {
				return e
			}
			return optionErr
		}}
		return d.Dial(network, address)
	}
}

func clientPortForActor(base, index int, ports []int) int {
	if len(ports) > 0 {
		return ports[index]
	}
	if base == 0 {
		return 0
	}
	return base + index
}

func readClientPortPool(path string, connections int) ([]int, error) {
	if connections < 2 || connections > 64000 || connections%2 != 0 {
		return nil, fmt.Errorf("invalid owned actor count")
	}
	raw, e := os.ReadFile(path)
	if e != nil {
		return nil, e
	}
	if len(raw) > 524288 {
		return nil, fmt.Errorf("port pool file too large")
	}
	var ports []int
	if e = json.Unmarshal(raw, &ports); e != nil {
		return nil, e
	}
	if len(ports) != connections {
		return nil, fmt.Errorf("one explicit source port required per actor")
	}
	seen := map[int]bool{}
	for _, p := range ports {
		if p < 1025 || p > 65535 || seen[p] {
			return nil, fmt.Errorf("invalid or duplicate client pool port")
		}
		seen[p] = true
	}
	return ports, nil
}
