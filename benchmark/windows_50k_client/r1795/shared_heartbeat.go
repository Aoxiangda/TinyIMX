package main

import (
	"container/heap"
	"fmt"
	"runtime"
	"sync"
	"sync/atomic"
	"time"
)

type heartbeatDue struct {
	actor *actor
	due   time.Time
}
type heartbeatHeap []heartbeatDue

func (h heartbeatHeap) Len() int           { return len(h) }
func (h heartbeatHeap) Less(i, j int) bool { return h[i].due.Before(h[j].due) }
func (h heartbeatHeap) Swap(i, j int)      { h[i], h[j] = h[j], h[i] }
func (h *heartbeatHeap) Push(v any)        { *h = append(*h, v.(heartbeatDue)) }
func (h *heartbeatHeap) Pop() any {
	old := *h
	n := len(old)
	v := old[n-1]
	old[n-1] = heartbeatDue{}
	*h = old[:n-1]
	return v
}

type heartbeatRegistration struct {
	actor    *actor
	accepted chan bool
}

// One timer owns the due heap; a bounded pool waits for real protocol replies.
// Every actor keeps its own login-relative15s phase. Neither payload, response
// timeout, measured sender nor ACK/delivery timing changes. Admission failures
// and missed whole heartbeat periods fail the run instead of dropping work.
type sharedHeartbeatSchedule struct {
	registrations                                       chan heartbeatRegistration
	jobs                                                chan heartbeatDue
	stop                                                <-chan struct{}
	done                                                chan struct{}
	doneOnce                                            sync.Once
	workers, maxActors                                  int
	period                                              time.Duration
	run                                                 func(*actor) error
	failed                                              func(uint64, error)
	registered, scheduled, started, completed, failures atomic.Uint64
	active, peakActive, peakQueue, maxLatenessNS        atomic.Uint64
}

func atomicPeak(value *atomic.Uint64, candidate uint64) {
	for prior := value.Load(); candidate > prior; prior = value.Load() {
		if value.CompareAndSwap(prior, candidate) {
			return
		}
	}
}
func newSharedHeartbeatSchedule(maxActors, workers int, period time.Duration, queue int, stop <-chan struct{}, wg *sync.WaitGroup, run func(*actor) error, failed func(uint64, error)) (*sharedHeartbeatSchedule, error) {
	if maxActors < 1 || maxActors > 64000 || workers < 1 || workers > 64 || queue < 1 || queue > 512 || period < time.Millisecond || period > time.Minute || stop == nil || wg == nil || run == nil || failed == nil {
		return nil, fmt.Errorf("invalid bounded heartbeat scheduler")
	}
	s := &sharedHeartbeatSchedule{registrations: make(chan heartbeatRegistration, 64), jobs: make(chan heartbeatDue, queue), stop: stop, done: make(chan struct{}), workers: workers, maxActors: maxActors, period: period, run: run, failed: failed}
	wg.Add(workers + 1)
	for i := 0; i < workers; i++ {
		go func() {
			defer wg.Done()
			for job := range s.jobs {
				active := s.active.Add(1)
				atomicPeak(&s.peakActive, active)
				if late := time.Since(job.due); late > 0 {
					atomicPeak(&s.maxLatenessNS, uint64(late))
				}
				s.started.Add(1)
				e := s.run(job.actor)
				s.active.Add(^uint64(0))
				if e != nil {
					s.fail(job.actor.uid, e)
				} else {
					s.completed.Add(1)
				}
			}
		}()
	}
	go func() { defer wg.Done(); s.loop() }()
	return s, nil
}
func (s *sharedHeartbeatSchedule) fail(uid uint64, e error) {
	s.failures.Add(1)
	s.doneOnce.Do(func() { close(s.done) })
	s.failed(uid, e)
}
func (s *sharedHeartbeatSchedule) Register(a *actor) bool {
	if a == nil || a.uid == 0 {
		return false
	}
	r := heartbeatRegistration{actor: a, accepted: make(chan bool, 1)}
	select {
	case <-s.stop:
		return false
	case <-s.done:
		return false
	case s.registrations <- r:
	}
	select {
	case <-s.stop:
		return false
	case <-s.done:
		return false
	case accepted := <-r.accepted:
		return accepted
	}
}
func (s *sharedHeartbeatSchedule) loop() {
	defer close(s.jobs)
	defer s.doneOnce.Do(func() { close(s.done) })
	due := make(heartbeatHeap, 0, s.maxActors)
	known := make(map[uint64]struct{}, s.maxActors)
	timer := time.NewTimer(time.Hour)
	defer timer.Stop()
	reset := func() {
		if !timer.Stop() {
			select {
			case <-timer.C:
			default:
			}
		}
		delay := time.Hour
		if len(due) > 0 {
			delay = time.Until(due[0].due)
			if delay < 0 {
				delay = 0
			}
		}
		timer.Reset(delay)
	}
	for {
		select {
		case <-s.stop:
			return
		case <-s.done:
			return
		case r := <-s.registrations:
			_, duplicate := known[r.actor.uid]
			if duplicate || len(known) >= s.maxActors {
				r.accepted <- false
				s.fail(r.actor.uid, fmt.Errorf("duplicate or excess heartbeat actor"))
				return
			}
			known[r.actor.uid] = struct{}{}
			heap.Push(&due, heartbeatDue{r.actor, time.Now().Add(s.period)})
			s.registered.Add(1)
			r.accepted <- true
			reset()
		case <-timer.C:
			now := time.Now()
			for len(due) > 0 && !due[0].due.After(now) {
				job := heap.Pop(&due).(heartbeatDue)
				s.scheduled.Add(1)
				if now.Sub(job.due) >= s.period {
					s.fail(job.actor.uid, fmt.Errorf("heartbeat scheduler missed an entire period"))
					return
				}
				select {
				case <-s.stop:
					return
				case <-s.done:
					return
				case s.jobs <- job:
					atomicPeak(&s.peakQueue, uint64(len(s.jobs)))
				default:
					s.fail(job.actor.uid, fmt.Errorf("bounded heartbeat work queue overloaded"))
					return
				}
				heap.Push(&due, heartbeatDue{job.actor, job.due.Add(s.period)})
			}
			reset()
		}
	}
}
func (s *sharedHeartbeatSchedule) Snapshot() map[string]any {
	return map[string]any{"mode": "one_due_heap_one_timer_bounded_reply_workers", "workers": s.workers, "queue_capacity": cap(s.jobs), "period_seconds": s.period.Seconds(), "registered": s.registered.Load(), "scheduled": s.scheduled.Load(), "started": s.started.Load(), "completed": s.completed.Load(), "failures": s.failures.Load(), "active": s.active.Load(), "peak_active": s.peakActive.Load(), "peak_queue": s.peakQueue.Load(), "maximum_schedule_lateness_ms": float64(s.maxLatenessNS.Load()) / 1e6, "response_timeout_seconds": 3, "no_silent_drop_or_protocol_retry": true}
}

// Read before offered traffic and after sockets close; never force GC, trim a
// process working set, or change the measured message scheduler.
func heartbeatClientMemorySnapshot() map[string]any {
	var m runtime.MemStats
	runtime.ReadMemStats(&m)
	var maxPause uint64
	for _, pause := range m.PauseNs {
		if pause > maxPause {
			maxPause = pause
		}
	}
	return map[string]any{
		"goroutines": runtime.NumGoroutine(), "GOMAXPROCS": runtime.GOMAXPROCS(0),
		"heap_allocated_bytes": m.HeapAlloc, "heap_inuse_bytes": m.HeapInuse,
		"heap_reserved_bytes": m.HeapSys, "heap_released_bytes": m.HeapReleased,
		"stack_inuse_bytes": m.StackInuse, "GC_count": m.NumGC,
		"GC_total_pause_ns": m.PauseTotalNs, "GC_recent_max_pause_ns": maxPause,
		"forced_GC_or_trimming": false,
	}
}
