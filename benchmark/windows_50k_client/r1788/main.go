// Native Windows standard-library TIMX benchmark, independent of Python IOCP/GC.
// Typed IDs/exact payload/receiver ACKs retained. Bounded asynchronous result IO during traffic; exact canonical summaries after close.
package main

import (
	"bufio"
	"encoding/binary"
	"encoding/json"
	"flag"
	"fmt"
	"io"
	"math"
	"net"
	"os"
	"path/filepath"
	"regexp"
	"runtime"
	"sort"
	"strconv"
	"strings"
	"sync"
	"sync/atomic"
	"time"
)

var negativeReasonEnumPattern = regexp.MustCompile(`^[a-z_]{1,64}$`)

type body struct {
	Success    bool   `json:"success"`
	UID        uint64 `json:"user_id"`
	Pong       bool   `json:"pong"`
	CID        string `json:"client_message_id"`
	MID        uint64 `json:"message_id"`
	From       uint64 `json:"from"`
	To         uint64 `json:"to"`
	Text       string `json:"text"`
	Persistent bool   `json:"stored_persistent"`
	Reason     string `json:"reason"`
}
type outcome struct {
	requestFinished bool
	Index           int    `json:"index"`
	CID             string `json:"cid"`
	From            uint64 `json:"from"`
	To              uint64 `json:"to"`
	Scheduled       int64  `json:"scheduled_ns"`
	Enqueued        int64  `json:"enqueued_ns"`
	Write           int64  `json:"write_ns"`
	MID             uint64 `json:"mid,omitempty"`
	DeliveryMID     uint64 `json:"delivery_mid,omitempty"`
	Ack             int64  `json:"ack_ns,omitempty"`
	Delivery        int64  `json:"delivery_ns,omitempty"`
	ReceiverAck     int64  `json:"receiver_ack_queued_ns,omitempty"`
	Timeout         bool   `json:"timeout,omitempty"`
	TimeoutNS       int64  `json:"timeout_ns,omitempty"`
	Negative        bool   `json:"negative_ack,omitempty"`
	NegativeReason  string `json:"negative_reason,omitempty"`
	NegativeNS      int64  `json:"negative_ack_ns,omitempty"`
	Invalid         string `json:"invalid,omitempty"`
}
type heartbeat struct {
	UID      uint64 `json:"uid"`
	Sent     int64  `json:"sent_ns"`
	Received int64  `json:"received_ns"`
}
type pending struct {
	expected uint16
	result   chan body
	row      *outcome
	ping     int64
}
type actor struct {
	ctx       *context
	uid, peer uint64
	conn      net.Conn
	reader    *bufio.Reader
	serial    atomic.Uint32
	writeMu   sync.Mutex
	pending   map[uint32]*pending
	busy      bool
}
type context struct {
	continueNegativeACK                    bool
	mu                                     sync.Mutex
	actors                                 []*actor
	journal                                *rowJournal[outcome]
	heartbeatJournal                       *rowJournal[heartbeat]
	ledger                                 *completionLedger
	active                                 int
	journalsClosed                         bool
	hbStopOnce                             sync.Once
	byCID                                  map[string]*outcome
	errors                                 []map[string]any
	abort                                  atomic.Bool
	closing                                atomic.Bool
	hbStop                                 chan struct{}
	readers                                sync.WaitGroup
	heartbeatWorkers                       sync.WaitGroup
	heartbeats, pongs, replays, duplicates int
	run                                    string
	payloadPattern                         string
	payload                                int
	epoch                                  time.Time
	epochQPC                               int64
	logins                                 []float64
}

func (c *context) now() int64 { value, _ := absoluteClock(); return value }
func (c *context) fail(where string, uid uint64, e error) {
	if c.closing.Load() {
		return
	}
	c.mu.Lock()
	defer c.mu.Unlock()
	if len(c.errors) < 100 {
		c.errors = append(c.errors, map[string]any{"where": where, "uid": uid, "type": fmt.Sprintf("%T", e), "detail": e.Error()})
	}
	c.abort.Store(true)
}
func (a *actor) write(kind uint16, seq uint32, value any, row *outcome) error {
	raw, e := json.Marshal(value)
	if e != nil {
		return e
	}
	frame := make([]byte, 20+len(raw))
	binary.BigEndian.PutUint32(frame, 0x54494d58)
	binary.BigEndian.PutUint16(frame[4:], 1)
	binary.BigEndian.PutUint16(frame[6:], kind)
	binary.BigEndian.PutUint32(frame[12:], seq)
	binary.BigEndian.PutUint32(frame[16:], uint32(len(raw)))
	copy(frame[20:], raw)
	a.writeMu.Lock()
	defer a.writeMu.Unlock()
	if row != nil {
		a.ctx.mu.Lock()
		row.Write = a.ctx.now()
		a.ctx.mu.Unlock()
	}
	if e = a.conn.SetWriteDeadline(time.Now().Add(3 * time.Second)); e != nil {
		return e
	}
	for len(frame) > 0 {
		n, e := a.conn.Write(frame)
		if e != nil {
			return e
		}
		if n == 0 {
			return io.ErrShortWrite
		}
		frame = frame[n:]
	}
	return nil
}
func (a *actor) request(kind uint16, value any, row *outcome, timeout time.Duration) (body, error) {
	seq := a.serial.Add(1)
	expected := kind + 1
	if kind == 9001 {
		expected = 9001
	}
	p := &pending{expected: expected, result: make(chan body, 1), row: row}
	if kind == 9001 {
		p.ping = a.ctx.now()
	}
	a.ctx.mu.Lock()
	a.pending[seq] = p
	a.ctx.mu.Unlock()
	if e := a.write(kind, seq, value, row); e != nil {
		return body{}, e
	}
	timer := time.NewTimer(timeout)
	defer timer.Stop()
	select {
	case r := <-p.result:
		return r, nil
	case <-timer.C:
		if row != nil {
			a.ctx.mu.Lock()
			row.Timeout = true
			row.TimeoutNS = a.ctx.now()
			a.ctx.mu.Unlock()
		}
		return body{}, fmt.Errorf("request deadline kind=%d", kind)
	}
}
func (a *actor) readLoop() {
	defer a.ctx.readers.Done()
	for {
		var header [20]byte
		if _, e := io.ReadFull(a.reader, header[:]); e != nil {
			a.ctx.fail("reader", a.uid, e)
			return
		}
		kind := binary.BigEndian.Uint16(header[6:])
		seq := binary.BigEndian.Uint32(header[12:])
		size := binary.BigEndian.Uint32(header[16:])
		if binary.BigEndian.Uint32(header[:]) != 0x54494d58 || binary.BigEndian.Uint16(header[4:]) != 1 || binary.BigEndian.Uint32(header[8:]) != 0 || size > 1048576 {
			a.ctx.fail("header", a.uid, fmt.Errorf("invalid TIMX header"))
			return
		}
		raw := make([]byte, size)
		if _, e := io.ReadFull(a.reader, raw); e != nil {
			a.ctx.fail("body", a.uid, e)
			return
		}
		var b body
		if e := json.Unmarshal(raw, &b); e != nil {
			a.ctx.fail("decode", a.uid, fmt.Errorf("invalid JSON object"))
			return
		}
		arrival := a.ctx.now()
		if kind == 2019 {
			if b.MID == 0 || b.To != a.uid || !validOwnedSender(b.From, len(a.ctx.actors)) {
				a.ctx.fail("delivery", a.uid, fmt.Errorf("owned identity mismatch"))
				return
			}
			cid := b.CID
			if cid == "" {
				cid = strings.SplitN(b.Text, "|", 2)[0]
			}
			var r *outcome
			a.ctx.mu.Lock()
			if !strings.HasPrefix(cid, "w"+a.ctx.run+"-") {
				a.ctx.replays++
			} else {
				r = a.ctx.byCID[cid]
				if r == nil {
					completed, err := a.ctx.completedDelivery(cid, b)
					if !completed || err != nil {
						a.ctx.mu.Unlock()
						if err == nil {
							err = fmt.Errorf("unregistered CID")
						}
						a.ctx.fail("delivery", a.uid, err)
						return
					}
					a.ctx.duplicates++
				} else {
					expected := makePayload(cid, a.ctx.payload, a.ctx.payloadPattern)
					if b.Text != expected || b.From != r.From || b.To != r.To {
						r.Invalid = "delivery_content_identity"
						a.ctx.abort.Store(true)
					}
					if r.Delivery != 0 {
						a.ctx.duplicates++
					} else {
						r.Delivery = arrival
						r.DeliveryMID = b.MID
					}
					if r.MID != 0 && r.MID != b.MID {
						r.Invalid = "ack_delivery_mid_mismatch"
						a.ctx.abort.Store(true)
					}
				}
			}
			a.ctx.mu.Unlock()
			if e := a.write(2020, seq, map[string]uint64{"message_id": b.MID}, nil); e != nil {
				a.ctx.fail("receiver_ack", a.uid, e)
				return
			}
			if r != nil {
				a.ctx.mu.Lock()
				r.ReceiverAck = a.ctx.now()
				a.ctx.finishLocked(r)
				a.ctx.mu.Unlock()
			}
			continue
		}
		if kind == 2051 {
			if b.MID == 0 {
				a.ctx.fail("group_replay", a.uid, fmt.Errorf("missing MID"))
				return
			}
			if e := a.write(2052, seq, map[string]uint64{"message_id": b.MID}, nil); e != nil {
				a.ctx.fail("group_replay_ack", a.uid, e)
				return
			}
			continue
		}
		a.ctx.mu.Lock()
		p := a.pending[seq]
		delete(a.pending, seq)
		if p == nil || p.expected != kind {
			a.ctx.mu.Unlock()
			a.ctx.fail("response", a.uid, fmt.Errorf("unregistered response kind=%d", kind))
			return
		}
		if kind == 9001 {
			if !b.Pong {
				a.ctx.mu.Unlock()
				a.ctx.fail("heartbeat_identity", a.uid, fmt.Errorf("heartbeat pong must be true"))
				return
			}
			a.ctx.pongs++
			if err := a.ctx.heartbeatJournal.try(heartbeat{a.uid, p.ping, arrival}); err != nil {
				a.ctx.journalFailureLocked("heartbeat_journal", err)
				if saved := a.ctx.heartbeatJournal.final(heartbeat{a.uid, p.ping, arrival}); saved != nil {
					a.ctx.journalFailureLocked("heartbeat_journal_IO", saved)
				}
			}
		}
		if r := p.row; r != nil {
			if !b.Success {
				r.Negative = true
				r.NegativeNS = arrival
				if negativeReasonEnumPattern.MatchString(b.Reason) {
					r.NegativeReason = b.Reason
				} else {
					r.NegativeReason = "redacted_non_enum"
				}
				if !a.ctx.continueNegativeACK {
					a.ctx.abort.Store(true)
				}
			} else if b.MID == 0 || !b.Persistent || b.CID != r.CID || b.From != r.From || b.To != r.To {
				r.Invalid = "durable_ack_identity"
				a.ctx.abort.Store(true)
			} else {
				r.MID = b.MID
				r.Ack = arrival
				if r.DeliveryMID != 0 && r.DeliveryMID != b.MID {
					r.Invalid = "ack_delivery_mid_mismatch"
					a.ctx.abort.Store(true)
				}
			}
		}
		a.ctx.mu.Unlock()
		p.result <- b
	}
}

func (c *context) journalFailureLocked(where string, e error) {
	if len(c.errors) < 100 {
		c.errors = append(c.errors, map[string]any{"where": where, "type": "client_journal_capacity_or_IO", "detail": e.Error()})
	}
	c.abort.Store(true)
}
func (c *context) finishLocked(r *outcome) {
	// Negatives/timeouts remain mutable until the reader cutover. A late delivery
	// must still appear in that exact failed outcome, not disappear into a counter.
	if !r.requestFinished || r.Negative || r.Ack == 0 || r.ReceiverAck == 0 || c.ledger.isFinished(r.Index) {
		return
	}
	if err := c.journal.try(*r); err != nil {
		c.journalFailureLocked("request_journal", err)
		return
	}
	if err := c.ledger.finish(r.Index, r.MID); err != nil {
		c.journalFailureLocked("completion_ledger", err)
		return
	}
	delete(c.byCID, r.CID)
}
func (c *context) completedDelivery(cid string, b body) (bool, error) {
	cut := strings.LastIndex(cid, "-n")
	if cut < 0 {
		return false, nil
	}
	index, err := strconv.Atoi(cid[cut+2:])
	if err != nil {
		return false, nil
	}
	mid, ok := c.ledger.completedMID(index)
	if !ok {
		return false, nil
	}
	from := uint64(500001 + index%c.active)
	to := uint64(500001 + ((index % c.active) ^ 1))
	if cid != fmt.Sprintf("w%s-u%d-n%d", c.run, from, index) {
		return false, nil
	}
	expected := makePayload(cid, c.payload, c.payloadPattern)
	if mid == 0 || b.MID != mid || b.From != from || b.To != to || b.Text != expected {
		return true, fmt.Errorf("completed delivery CID/MID/body/owner mismatch: %s", cid)
	}
	return true, nil
}
func (c *context) closeJournals() error {
	if c.journalsClosed {
		return nil
	}
	for _, r := range c.byCID {
		if err := c.journal.final(*r); err != nil {
			return err
		}
		if err := c.ledger.finish(r.Index, r.MID); err != nil {
			return err
		}
	}
	c.byCID = nil
	first := c.journal.close()
	second := c.heartbeatJournal.close()
	c.journalsClosed = true
	if first != nil {
		return first
	}
	return second
}

func quantile(values []float64) map[string]any {
	if len(values) == 0 {
		return map[string]any{"count": 0}
	}
	sort.Float64s(values)
	sum := 0.
	for _, v := range values {
		sum += v
	}
	return map[string]any{"count": len(values), "p50_ms": values[int(math.Ceil(float64(len(values))*.5))-1], "p99_ms": values[int(math.Ceil(float64(len(values))*.99))-1], "max_ms": values[len(values)-1], "mean_ms": sum / float64(len(values))}
}
func save(path string, value any) error {
	f, e := os.OpenFile(path, os.O_CREATE|os.O_EXCL|os.O_WRONLY, 0600)
	if e != nil {
		return e
	}
	defer f.Close()
	enc := json.NewEncoder(f)
	enc.SetIndent("", "  ")
	return enc.Encode(value)
}
func lines(path string, values any) error {
	f, e := os.OpenFile(path, os.O_CREATE|os.O_EXCL|os.O_WRONLY, 0600)
	if e != nil {
		return e
	}
	defer f.Close()
	enc := json.NewEncoder(f)
	switch v := values.(type) {
	case []*outcome:
		for _, r := range v {
			if e = enc.Encode(r); e != nil {
				return e
			}
		}
	case []heartbeat:
		for _, r := range v {
			if e = enc.Encode(r); e != nil {
				return e
			}
		}
	}
	return nil
}
func main() {
	if e := run(); e != nil {
		fmt.Fprintln(os.Stderr, e)
		os.Exit(2)
	}
}
func run() error {
	host := flag.String("host", "192.168.220.128", "")
	port := flag.Int("port", 9000, "")
	out := flag.String("out", "", "")
	runID := flag.String("run", "", "")
	connections := flag.Int("connections", 10000, "")
	active := flag.Int("active-connections", 0, "")
	rate := flag.Int("rate", 100, "")
	seconds := flag.Int("seconds", 60, "")
	ramp := flag.Int("ramp", 100, "")
	payload := flag.Int("payload-bytes", 128, "")
	payloadPattern := flag.String("payload-pattern", "repeat", "repeat or reproducible xorshift64")
	stopFile := flag.String("stop-file", "", "owned controller-stop file; gracefully preserve every final outcome")
	bufferKiB := flag.Int("read-buffer-kib", 16, "")
	startFile := flag.String("start-file", "", "")
	_ = flag.String("gc-policy", "baseline", "Python-only flag ignored; Go concurrent GC remains enabled")
	continueNegativeACK := flag.Bool("continue-on-negative-ack", false, "Sustain offered pressure after typed business rejection; every rejection still fails the run")
	setupBindRetries := flag.Int("setup-local-bind-retries", 0, "Retry only local bind WSAENOBUFS during setup; allowed0 or3, never retry protocol/auth/message failures")
	setupOrder := flag.String("setup-uid-order", "adjacent", "Setup only: adjacent, interleave4 or outerpair4; measured sender/pair mapping unchanged")
	secondPort := flag.Int("second-gateway-port", 0, "Owned direct test endpoint;0 retains historical single ingress")
	gatewayPlacement := flag.String("gateway-placement", "loadbalance", "loadbalance, cross, local")
	localPortBase := flag.Int("local-port-base", 0, "0 preserves dynamic ports; otherwise one exclusive client port per actor, no OS setting change")
	reserveCandidates := flag.String("reserve-client-port-candidates", "", "Bounded exact50K exclusive reservation candidates; no OS changes")
	portPoolFile := flag.String("local-port-pool-file", "", "JSON array of exact exclusive source ports, one per actor")
	tcpBufferKiB := flag.Int("tcp-buffer-kib", 0, "0 preserves OS defaults;8 or16 explicitly bounds kernel buffers")
	flag.Parse()
	if *payloadPattern != "repeat" && *payloadPattern != "xorshift64" {
		return fmt.Errorf("unknown payload pattern")
	}
	if *stopFile != "" && filepath.Clean(*stopFile) != filepath.Join(filepath.Clean(*out), "controller-stop") {
		return fmt.Errorf("stop file must belong to the output directory")
	}
	if e := validateKernelBufferKiB(*tcpBufferKiB); e != nil {
		return e
	}
	var selectedPorts []int
	if *reserveCandidates != "" {
		if *portPoolFile != "" || *localPortBase != 0 || *connections != 50000 {
			return fmt.Errorf("exclusive reservation requires exactly50000 actors and no other port mode")
		}
	} else if *portPoolFile != "" {
		if *localPortBase != 0 {
			return fmt.Errorf("choose either port pool or base")
		}
		var e error
		selectedPorts, e = readClientPortPool(*portPoolFile, *connections)
		if e != nil {
			return e
		}
	} else if e := validateClientPortPool(*connections, *localPortBase); e != nil {
		return e
	}
	if e := validateGatewayPlacement(*port, *secondPort, *gatewayPlacement); e != nil {
		return e
	}
	if *setupBindRetries != 0 && *setupBindRetries != 3 {
		return fmt.Errorf("setup-local-bind-retries must be0 or3")
	}
	if e := validateSetupOrder(*setupOrder, *connections); e != nil {
		return e
	}
	if *active == 0 {
		*active = *connections
	}
	if runtime.GOOS != "windows" || (*host != "192.168.220.128" && *host != "127.0.0.1") || !regexp.MustCompile(`^[a-z0-9]{3,16}$`).MatchString(*runID) || *connections < 2 || *connections > 64000 || *connections%2 != 0 || *active < 1 || *active > *connections || *rate < 1 || *rate > 10000 || *seconds < 1 || *seconds > 1800 || *rate**seconds > maxPlannedRows || *ramp < 1 || *ramp > 500 || (*payload != 128 && *payload != 256 && *payload != 1024 && *payload != 4096 && *payload != 16384) || (*bufferKiB != 2 && *bufferKiB != 16 && *bufferKiB != 64) {
		return fmt.Errorf("native owned workload bounds exceeded")
	}
	if e := os.Mkdir(*out, 0700); e != nil {
		return e
	}
	var reserved *exclusivePortReservations
	if *reserveCandidates != "" {
		candidates, e := readExclusiveCandidates(*reserveCandidates, *connections)
		if e != nil {
			return e
		}
		reserved, e = reserveExclusivePorts(candidates, *connections)
		if e != nil {
			return e
		}
		defer reserved.close()
		selectedPorts = reserved.actorPorts()
		if e = save(filepath.Join(*out, "source-port-reservation-before-auth.json"), reserved.snapshot()); e != nil {
			return e
		}
	}
	epoch := time.Now()
	qpc, freq := absoluteClock()
	c := &context{continueNegativeACK: *continueNegativeACK, run: *runID, payload: *payload, payloadPattern: *payloadPattern, epoch: epoch, epochQPC: qpc, byCID: map[string]*outcome{}, hbStop: make(chan struct{})}
	stopDone := make(chan struct{})
	defer close(stopDone)
	if *stopFile != "" {
		go watchControllerStop(*stopFile, stopDone, func() { c.fail("controller_stop", 0, fmt.Errorf("owned controller requested a bounded stop")) })
	}
	ledger, err := newCompletionLedger(*rate * *seconds)
	if err != nil {
		return err
	}
	c.ledger = ledger
	c.active = *active
	c.journal, err = newRowJournal[outcome](filepath.Join(*out, "requests.completed.jsonl"), journalQueueRows)
	if err != nil {
		return err
	}
	c.heartbeatJournal, err = newRowJournal[heartbeat](filepath.Join(*out, "heartbeats.jsonl"), journalQueueRows)
	if err != nil {
		_ = c.journal.close()
		return err
	}
	defer func() {
		c.closing.Store(true)
		c.hbStopOnce.Do(func() { close(c.hbStop) })
		for _, a := range c.actors {
			if a != nil && a.conn != nil {
				a.conn.Close()
			}
		}
		c.heartbeatWorkers.Wait()
		c.readers.Wait()
		if err := c.closeJournals(); err != nil {
			fmt.Fprintln(os.Stderr, "journal cleanup:", err)
		}
	}()
	c.actors = make([]*actor, *connections)
	var sends sync.WaitGroup
	var setup sync.WaitGroup
	sem := make(chan struct{}, 32)
	var auth atomic.Int64
	password := os.Getenv("TINYIMX_BENCH_PASSWORD")
	if password == "" {
		password = "123456"
	}
	setupAttemptErrors := []setupBindAttempt{}
	begin := time.Now()
	failure := ""
	started := int64(0)
	planned := *rate * *seconds
	scheduled, skipped := 0, 0
	for i := 0; i < *connections; i++ {
		if wait := time.Until(begin.Add(time.Duration(i) * time.Second / time.Duration(*ramp))); wait > 0 {
			time.Sleep(wait)
		}
		if c.abort.Load() {
			break
		}
		sem <- struct{}{}
		setup.Add(1)
		go func(i int) {
			defer setup.Done()
			defer func() { <-sem }()
			a := &actor{ctx: c, uid: 500001 + uint64(i), peer: 500001 + uint64(i^1), pending: map[uint32]*pending{}}
			a.serial.Store(100)
			c.actors[i] = a
			dial := setupDialerForActor(clientPortForActor(*localPortBase, i, selectedPorts), 0)
			if reserved != nil {
				dial = reserved.dialer(i)
			}
			conn, e := dialSetupWithLocalBindRetry(net.JoinHostPort(*host, fmt.Sprint(gatewayPortForActor(i, *port, *secondPort, *gatewayPlacement))), *setupBindRetries,
				dial, time.Sleep, c.abort.Load, func(event setupBindAttempt) {
					event.UID = a.uid
					event.QPCNS = c.now()
					c.mu.Lock()
					setupAttemptErrors = append(setupAttemptErrors, event)
					c.mu.Unlock()
				})
			if e != nil {
				c.fail("connect", a.uid, e)
				return
			}
			a.conn = conn
			if e = conn.(*net.TCPConn).SetNoDelay(true); e != nil {
				c.fail("nodelay", a.uid, e)
				return
			}
			if e = configureKernelBuffers(conn, *tcpBufferKiB); e != nil {
				conn.Close()
				c.fail("tcp-buffer", a.uid, e)
				return
			}
			a.reader = bufio.NewReaderSize(conn, *bufferKiB*1024)
			c.readers.Add(1)
			go a.readLoop()
			before := c.now()
			b, e := a.request(1001, map[string]string{"username": fmt.Sprintf("m21b500000_%06d", i+1), "password": password}, nil, 10*time.Second)
			if e != nil || !b.Success || b.UID != a.uid {
				c.fail("login", a.uid, fmt.Errorf("owned authentication failed"))
				return
			}
			c.mu.Lock()
			c.logins = append(c.logins, float64(c.now()-before)/1e6)
			c.mu.Unlock()
			n := auth.Add(1)
			if n%1000 == 0 {
				fmt.Println("AUTH_PROGRESS", n)
			}
			c.heartbeatWorkers.Add(1)
			go func() {
				defer c.heartbeatWorkers.Done()
				ticker := time.NewTicker(15 * time.Second)
				defer ticker.Stop()
				for {
					select {
					case <-c.hbStop:
						return
					case <-ticker.C:
						c.mu.Lock()
						c.heartbeats++
						c.mu.Unlock()
						if _, e := a.request(9001, struct{}{}, nil, 3*time.Second); e != nil {
							c.fail("heartbeat", a.uid, e)
							return
						}
					}
				}
			}()
		}(setupActorIndex(i, *setupOrder))
	}
	setup.Wait()
	if e := save(filepath.Join(*out, "setup-bind-attempts.json"), map[string]any{
		"retry_budget": *setupBindRetries, "attempt_errors": setupAttemptErrors,
		"scope": "Connection setup only. Every error retained. No retry of auth, protocol, negative ACK, or measured message traffic.",
	}); e != nil {
		return e
	}

	if reserved != nil {
		selectedPorts = reserved.actorPorts()
		reserved.close()
		if e := save(filepath.Join(*out, "source-port-reservation-after-auth.json"), reserved.snapshot()); e != nil {
			return e
		}
		if e := save(filepath.Join(*out, "effective-client-ports.json"), selectedPorts); e != nil {
			return e
		}
	}
	endpoints := make([]map[string]any, 0, len(c.actors))
	for i, a := range c.actors {
		if a == nil || a.conn == nil {
			continue
		}
		local := a.conn.LocalAddr().(*net.TCPAddr)
		if (*localPortBase != 0 || len(selectedPorts) > 0) && local.Port != clientPortForActor(*localPortBase, i, selectedPorts) {
			return fmt.Errorf("explicit client port mismatch for actor %d", i)
		}
		endpoints = append(endpoints, map[string]any{"uid": a.uid, "local": local.String(), "remote": a.conn.RemoteAddr().String()})
	}
	if e := save(filepath.Join(*out, "setup-local-endpoints.json"), endpoints); e != nil {
		return e
	}
	if auth.Load() != int64(*connections) || c.abort.Load() {
		failure = "setup_failed"
	} else {
		if e := save(filepath.Join(*out, "ready.json"), map[string]any{"connections": auth.Load(), "loop": "NativeGoStdNet", "setup_seconds": time.Since(begin).Seconds(), "login": quantile(c.logins), "utc": time.Now().UTC(), "actual_read_buffer_sizes": []int{*bufferKiB * 1024}}); e != nil {
			return e
		}
		fmt.Println("ALL_AUTHENTICATED", auth.Load())
		start := c.now() + int64(2*time.Second)
		if *startFile != "" {
			deadline := time.Now().Add(90 * time.Second)
			for {
				raw, e := os.ReadFile(*startFile)
				if e == nil {
					if _, e = fmt.Sscan(string(raw), &start); e != nil {
						return fmt.Errorf("invalid QPC barrier")
					}
					break
				}
				if time.Now().After(deadline) {
					return fmt.Errorf("shared barrier timeout")
				}
				time.Sleep(100 * time.Millisecond)
			}
		}
		if start <= c.now() || start-c.now() > int64(60*time.Second) {
			return fmt.Errorf("fresh future QPC barrier required")
		}
		if wait := time.Duration(start - c.now()); wait > 0 {
			time.Sleep(wait)
		}
		started = start
		for scheduled < planned && !c.abort.Load() {
			due := start + int64(scheduled)*int64(time.Second)/int64(*rate)
			if wait := time.Duration(due - c.now()); wait > 0 {
				time.Sleep(wait)
			}
			if c.now() >= start+int64(*seconds+1)*int64(time.Second) {
				skipped += planned - scheduled
				scheduled = planned
				break
			}
			a := c.actors[scheduled%*active]
			index := scheduled
			scheduled++
			c.mu.Lock()
			if len(c.byCID) >= maxActiveRows {
				skipped++
				c.journalFailureLocked("active_outcome_capacity", fmt.Errorf("bounded active outcome limit reached"))
				c.mu.Unlock()
				break
			}
			if a.busy {
				skipped++
				c.mu.Unlock()
				continue
			}
			cid := fmt.Sprintf("w%s-u%d-n%d", *runID, a.uid, index)
			r := &outcome{Index: index, CID: cid, From: a.uid, To: a.peer, Scheduled: due, Enqueued: c.now()}
			if err := c.ledger.issue(index); err != nil {
				c.mu.Unlock()
				return err
			}
			c.byCID[cid] = r
			a.busy = true
			c.mu.Unlock()
			sends.Add(1)
			go func(a *actor, r *outcome) {
				defer sends.Done()
				text := makePayload(r.CID, *payload, *payloadPattern)
				_, e := a.request(2001, map[string]any{"to": a.peer, "text": text, "client_message_id": r.CID}, r, 3*time.Second)
				if e != nil && !r.Timeout {
					c.fail("send", a.uid, e)
				}
				c.mu.Lock()
				r.requestFinished = true
				a.ctx.finishLocked(r)
				a.busy = false
				c.mu.Unlock()
			}(a, r)
		}
		if wait := time.Duration(start + int64(*seconds)*int64(time.Second) - c.now()); wait > 0 && !c.abort.Load() {
			time.Sleep(wait)
		}
		sends.Wait()
		deadline := time.Now().Add(15 * time.Second)
		for time.Now().Before(deadline) {
			c.mu.Lock()
			done := true
			for _, r := range c.byCID {
				if r.Ack != 0 && r.ReceiverAck == 0 {
					done = false
					break
				}
			}
			c.mu.Unlock()
			if done {
				break
			}
			time.Sleep(20 * time.Millisecond)
		}
	}
	c.hbStopOnce.Do(func() { close(c.hbStop) })
	c.heartbeatWorkers.Wait()
	deadline := time.Now().Add(5 * time.Second)
	for time.Now().Before(deadline) {
		c.mu.Lock()
		done := c.pongs == c.heartbeats
		c.mu.Unlock()
		if done {
			break
		}
		time.Sleep(10 * time.Millisecond)
	}
	c.closing.Store(true)
	for _, a := range c.actors {
		if a != nil && a.conn != nil {
			a.conn.Close()
		}
	}
	c.readers.Wait()
	if err := c.closeJournals(); err != nil {
		return err
	}
	if err := canonicalRows[outcome](filepath.Join(*out, "requests.completed.jsonl"), filepath.Join(*out, "requests.jsonl"), func(r outcome) int { return r.Index }, c.ledger.issuedCount); err != nil {
		return err
	}
	ack, delivery, valid, timeouts, negative, invalid, late, receiverAck := 0, 0, 0, 0, 0, 0, 0, 0
	senders := map[uint64]bool{}
	traffic := map[uint64]bool{}
	ackMS, scheduledAckMS, deliveryMS, lateness, heartbeatMS := []float64{}, []float64{}, []float64{}, []float64{}, []float64{}
	negativeMS := []float64{}
	if err := readRows[outcome](filepath.Join(*out, "requests.jsonl"), func(r outcome) error {
		senders[r.From] = true
		traffic[r.From] = true
		traffic[r.To] = true
		lateness = append(lateness, float64(r.Enqueued-r.Scheduled)/1e6)
		if started > 0 && r.Enqueued >= started+int64(*seconds)*int64(time.Second) {
			late++
		}
		if r.Ack > 0 {
			ack++
			ackMS = append(ackMS, float64(r.Ack-r.Enqueued)/1e6)
			scheduledAckMS = append(scheduledAckMS, float64(r.Ack-r.Scheduled)/1e6)
		}
		if r.Delivery > 0 {
			delivery++
			deliveryMS = append(deliveryMS, float64(r.Delivery-r.Scheduled)/1e6)
		}
		if r.ReceiverAck > 0 {
			receiverAck++
		}
		if r.Timeout {
			timeouts++
		}
		if r.Negative {
			negative++
			if r.NegativeNS > 0 {
				negativeMS = append(negativeMS, float64(r.NegativeNS-r.Enqueued)/1e6)
			}
		}
		if r.Invalid != "" {
			invalid++
		}
		if r.Ack > 0 && r.ReceiverAck > 0 && !r.Timeout && !r.Negative && r.Invalid == "" {
			valid++
		}
		return nil
	}); err != nil {
		return err
	}
	if err := readRows[heartbeat](filepath.Join(*out, "heartbeats.jsonl"), func(h heartbeat) error { heartbeatMS = append(heartbeatMS, float64(h.Received-h.Sent)/1e6); return nil }); err != nil {
		return err
	}
	status := "FAIL"
	if started > 0 && valid == planned && skipped == 0 && len(c.errors) == 0 && c.pongs == c.heartbeats {
		status = "PASS"
	}
	negativePolicy := "abort_first_negative"
	if c.continueNegativeACK {
		negativePolicy = "continue_offered_pressure"
	}
	summary := map[string]any{"kernel_tcp_buffers": kernelBufferSnapshot(*tcpBufferKiB), "client_revision": "R1788_EXCLUSIVE_RESERVED_50K", "go_memory_limit_env": os.Getenv("GOMEMLIMIT"), "journal_queue_capacity": journalQueueRows, "journal_peak_observed_queue": c.journal.peak.Load(), "maximum_active_outcomes": maxActiveRows, "compact_completion_bytes": len(c.ledger.mids)*8 + len(c.ledger.issued)*16, "traffic_result_IO": "bounded asynchronous completion/heartbeat journal; no synchronous normal-path disk wait; canonical sorting and exact quantiles after sockets close", "local_port_base": *localPortBase, "exclusive_local_ports": *localPortBase != 0 || len(selectedPorts) > 0, "port_pool_file": *portPoolFile, "gateway_placement": *gatewayPlacement, "first_gateway_port": *port, "second_gateway_port": *secondPort, "setup_uid_order": *setupOrder, "setup_local_bind_retry_budget": *setupBindRetries, "setup_attempt_error_count": len(setupAttemptErrors), "negative_ack_policy": negativePolicy, "negative_response_latency": quantile(negativeMS), "issue_id": "PERF-005", "status": status, "failure": failure, "loop": "NativeGoStdNet", "client_platform": runtime.GOOS, "run": *runID, "host": *host, "connections": *connections, "active_sender_connections": *active, "observed_sender_connections": len(senders), "observed_traffic_connections": len(traffic), "active_sender_ratio": float64(*active) / float64(*connections), "rate": *rate, "seconds": *seconds, "payload_bytes": *payload, "payload_pattern": *payloadPattern, "controller_stop_file_configured": *stopFile != "", "start_perf_counter_ns": started, "planned": planned, "sent": c.ledger.issuedCount, "positive_ack": ack, "unique_delivery": delivery, "receiver_ack_queued": receiverAck, "completed_valid": valid, "timeouts": timeouts, "negative_ack": negative, "invalid": invalid, "duplicate_deliveries": c.duplicates, "old_owned_replays": c.replays, "heartbeats": c.heartbeats, "pongs": c.pongs, "errors": c.errors, "skipped_busy_or_window": skipped, "not_scheduled_after_abort": planned - scheduled, "late_offered_requests": late, "clock_audit": map[string]any{"clock": "Windows QueryPerformanceCounter", "qpc_frequency": freq, "go": runtime.Version()}, "client_read_buffer_kib": *bufferKiB, "enqueue_to_ack": quantile(ackMS), "scheduled_to_ack": quantile(scheduledAckMS), "scheduled_to_delivery": quantile(deliveryMS), "send_lateness": quantile(lateness), "heartbeat_latency": quantile(heartbeatMS), "limits": "Native Windows Go standard net client. Typed uint64 IDs, exact payload/MID/owner validation and receiver ACK queued after net write. Go concurrent GC enabled. No SQL reconciliation yet, no crash/power-loss proof. Failures and unissued requests retained; observed-response percentiles are not failure latency."}
	if e := save(filepath.Join(*out, "summary.json"), summary); e != nil {
		return e
	}
	fmt.Println("TIMX_GO", status, valid, "/", planned)
	if status != "PASS" {
		return fmt.Errorf("native load failed")
	}
	return nil
}
