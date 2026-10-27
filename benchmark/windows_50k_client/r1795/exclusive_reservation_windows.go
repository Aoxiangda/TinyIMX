package main

import (
	"encoding/json"
	"errors"
	"fmt"
	"net"
	"os"
	"sync"
	"syscall"
	"time"
)

type reservationEvent struct {
	Actor int    `json:"actor_index"`
	Port  int    `json:"port"`
	Code  int    `json:"local_bind_error_code"`
	Stage string `json:"stage"`
}
type reservedPort struct {
	port   int
	handle syscall.Handle
}
type exclusivePortReservations struct {
	mu              sync.Mutex
	actors          []reservedPort
	spares          []reservedPort
	events          []reservationEvent
	initialReserved int
	handoffs        int
	closed          bool
}

func readExclusiveCandidates(path string, actors int) ([]int, error) {
	raw, e := os.ReadFile(path)
	if e != nil {
		return nil, e
	}
	if len(raw) > 524288 {
		return nil, fmt.Errorf("bounded candidate file required")
	}
	var ports []int
	if e = json.Unmarshal(raw, &ports); e != nil {
		return nil, e
	}
	if actors != 50000 || len(ports) != actors+512 {
		return nil, fmt.Errorf("exact50K actors plus512 declared candidates required")
	}
	seen := make(map[int]bool, len(ports))
	for _, p := range ports {
		if p < 1025 || p > 65535 || seen[p] {
			return nil, fmt.Errorf("invalid or repeated exclusive candidate")
		}
		seen[p] = true
	}
	return ports, nil
}

func reserveExclusivePorts(ports []int, actors int) (*exclusivePortReservations, error) {
	p := &exclusivePortReservations{}
	for _, port := range ports {
		h, e := syscall.Socket(syscall.AF_INET, syscall.SOCK_STREAM, syscall.IPPROTO_TCP)
		if e != nil {
			p.close()
			return nil, e
		}
		e = syscall.SetsockoptInt(h, syscall.SOL_SOCKET, -5, 1)
		if e == nil {
			e = syscall.Bind(h, &syscall.SockaddrInet4{Port: port})
		}
		if e != nil {
			syscall.Closesocket(h)
			var code syscall.Errno
			if errors.As(e, &code) && (int(code) == 10013 || int(code) == 10048) {
				p.events = append(p.events, reservationEvent{-1, port, int(code), "reservation"})
				continue
			}
			p.close()
			return nil, e
		}
		item := reservedPort{port, h}
		p.initialReserved++
		if len(p.actors) < actors {
			p.actors = append(p.actors, item)
		} else {
			p.spares = append(p.spares, item)
		}
	}
	if len(p.actors) != actors {
		p.close()
		return nil, fmt.Errorf("declared candidate reservations insufficient before authentication")
	}
	return p, nil
}

func (p *exclusivePortReservations) close() {
	p.mu.Lock()
	defer p.mu.Unlock()
	if p.closed {
		return
	}
	p.closed = true
	for i := range p.actors {
		if p.actors[i].handle != syscall.InvalidHandle {
			syscall.Closesocket(p.actors[i].handle)
			p.actors[i].handle = syscall.InvalidHandle
		}
	}
	for _, v := range p.spares {
		syscall.Closesocket(v.handle)
	}
	p.spares = nil
}
func (p *exclusivePortReservations) actorPorts() []int {
	p.mu.Lock()
	defer p.mu.Unlock()
	out := make([]int, len(p.actors))
	for i, v := range p.actors {
		out[i] = v.port
	}
	return out
}
func (p *exclusivePortReservations) snapshot() map[string]any {
	p.mu.Lock()
	defer p.mu.Unlock()
	events := append([]reservationEvent{}, p.events...)
	return map[string]any{"actors": len(p.actors), "initial_reserved": p.initialReserved, "remaining_reserved_spares": len(p.spares), "handoffs": p.handoffs, "closed": p.closed, "events": events, "exclusive_only": true, "SO_REUSEADDR": false, "OS_settings_changed": false, "scope": "setup before login only; no authentication or business retry; all source ports remain distinct"}
}

// Reserve through the entire login ramp. Release a single pin only inside the
// replacement socket's Control callback, immediately before Go's exclusive bind.
// The unavoidable close/bind gap can only consume a declared, still-reserved
// spare. It never switches to shared binding or retries a successful connect.
func (p *exclusivePortReservations) dialer(actor int) func(string, string, time.Duration) (net.Conn, error) {
	return func(network, address string, timeout time.Duration) (net.Conn, error) {
		for attempt := 0; attempt < 4; attempt++ {
			p.mu.Lock()
			if p.closed || actor < 0 || actor >= len(p.actors) {
				p.mu.Unlock()
				return nil, fmt.Errorf("invalid or closed source reservation")
			}
			port := p.actors[actor].port
			p.mu.Unlock()
			d := net.Dialer{Timeout: timeout, LocalAddr: &net.TCPAddr{Port: port}, Control: func(_, _ string, raw syscall.RawConn) error {
				var optionErr error
				e := raw.Control(func(fd uintptr) {
					optionErr = syscall.SetsockoptInt(syscall.Handle(fd), syscall.SOL_SOCKET, -5, 1)
					if optionErr != nil {
						return
					}
					p.mu.Lock()
					defer p.mu.Unlock()
					item := &p.actors[actor]
					if item.handle != syscall.InvalidHandle {
						optionErr = syscall.Closesocket(item.handle)
						item.handle = syscall.InvalidHandle
						p.handoffs++
					}
				})
				if e != nil {
					return e
				}
				return optionErr
			}}
			conn, e := d.Dial(network, address)
			if e == nil {
				return conn, nil
			}
			if conn != nil {
				conn.Close()
			}
			code := setupBindErrorCode(e)
			if code != 10013 && code != 10048 {
				return nil, e
			}
			p.mu.Lock()
			p.events = append(p.events, reservationEvent{actor, port, code, "exclusive_handoff_bind"})
			if attempt == 3 || len(p.spares) == 0 {
				p.mu.Unlock()
				return nil, e
			}
			p.actors[actor] = p.spares[0]
			p.spares = p.spares[1:]
			p.mu.Unlock()
		}
		return nil, fmt.Errorf("bounded exclusive handoff attempts exhausted")
	}
}
