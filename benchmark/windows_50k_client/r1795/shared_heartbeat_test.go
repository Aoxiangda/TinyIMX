package main

import (
	"runtime"
	"sync"
	"testing"
	"time"
)

func TestSharedHeartbeatEveryRegisteredActorUsesRealTimer(t *testing.T) {
	stop := make(chan struct{})
	var wg sync.WaitGroup
	var mu sync.Mutex
	seen := map[uint64]int{}
	all := make(chan struct{})
	var once sync.Once
	errors := make(chan error, 8)
	s, e := newSharedHeartbeatSchedule(128, 16, 250*time.Millisecond, 512, stop, &wg, func(a *actor) error {
		mu.Lock()
		defer mu.Unlock()
		seen[a.uid]++
		if len(seen) == 128 {
			once.Do(func() { close(all) })
		}
		return nil
	}, func(_ uint64, e error) {
		select {
		case errors <- e:
		default:
		}
	})
	if e != nil {
		t.Fatal(e)
	}
	defer func() { close(stop); wg.Wait() }()
	for i := uint64(1); i <= 128; i++ {
		if !s.Register(&actor{uid: 500000 + i}) {
			t.Fatal("registration failed")
		}
	}
	select {
	case <-all:
	case e := <-errors:
		t.Fatal(e)
	case <-time.After(3 * time.Second):
		t.Fatal("some registered actors missed the first timer")
	}
	if s.registered.Load() != 128 || s.started.Load() < 128 {
		t.Fatal("not every actor scheduled")
	}
}

func TestSharedHeartbeat50000ActorsUseAtMost65Workers(t *testing.T) {
	baseline := runtime.NumGoroutine()
	stop := make(chan struct{})
	var wg sync.WaitGroup
	s, e := newSharedHeartbeatSchedule(50000, 64, 15*time.Second, 512, stop, &wg, func(*actor) error { return nil }, func(_ uint64, e error) { t.Error(e) })
	if e != nil {
		t.Fatal(e)
	}
	for i := uint64(1); i <= 50000; i++ {
		if !s.Register(&actor{uid: 500000 + i}) {
			close(stop)
			wg.Wait()
			t.Fatal("registration failed")
		}
	}
	after := runtime.NumGoroutine()
	close(stop)
	wg.Wait()
	if s.registered.Load() != 50000 {
		t.Fatal("50K actor registration lost")
	}
	if after-baseline > 66 {
		t.Fatalf("unbounded per-actor workers: %d", after-baseline)
	}
	if s.started.Load() != 0 {
		t.Fatal("15s heartbeat incorrectly sent during fast registration")
	}
	t.Logf("50000 registered actors, extra goroutines=%d; every worker stopped", after-baseline)
}

func TestSharedHeartbeatOverloadIsExplicitFailure(t *testing.T) {
	stop := make(chan struct{})
	block := make(chan struct{})
	var wg sync.WaitGroup
	errors := make(chan error, 8)
	s, e := newSharedHeartbeatSchedule(10, 1, 50*time.Millisecond, 1, stop, &wg, func(*actor) error { <-block; return nil }, func(_ uint64, e error) {
		select {
		case errors <- e:
		default:
		}
	})
	if e != nil {
		t.Fatal(e)
	}
	for i := uint64(1); i <= 10; i++ {
		if !s.Register(&actor{uid: 500000 + i}) {
			close(block)
			close(stop)
			wg.Wait()
			t.Fatal("registration failed")
		}
	}
	var failure error
	select {
	case failure = <-errors:
	case <-time.After(time.Second):
	}
	close(block)
	close(stop)
	wg.Wait()
	if failure == nil || s.failures.Load() == 0 {
		t.Fatal("overload was silently dropped")
	}
	if s.scheduled.Load() <= s.started.Load() {
		t.Fatal("test did not exercise a bounded queue rejection")
	}
}

func TestSharedHeartbeatStopDrainsAlreadyQueuedRequests(t *testing.T) {
	stop := make(chan struct{})
	block := make(chan struct{})
	var wg sync.WaitGroup
	s, e := newSharedHeartbeatSchedule(3, 1, 100*time.Millisecond, 16, stop, &wg, func(*actor) error { <-block; return nil }, func(_ uint64, e error) { t.Error(e) })
	if e != nil {
		t.Fatal(e)
	}
	for i := uint64(1); i <= 3; i++ {
		if !s.Register(&actor{uid: 500000 + i}) {
			t.Fatal("registration failed")
		}
	}
	deadline := time.Now().Add(time.Second)
	for (s.started.Load() != 1 || len(s.jobs) != 2) && time.Now().Before(deadline) {
		time.Sleep(time.Millisecond)
	}
	close(stop)
	close(block)
	wg.Wait()
	if s.scheduled.Load() != 3 || s.started.Load() != 3 || s.completed.Load() != 3 || s.failures.Load() != 0 {
		t.Fatalf("queued requests were lost at shutdown: %+v", s.Snapshot())
	}
	if s.Register(&actor{uid: 600000}) {
		t.Fatal("registration after shutdown accepted")
	}
}

func TestSharedHeartbeatRejectsDuplicateActor(t *testing.T) {
	stop := make(chan struct{})
	var wg sync.WaitGroup
	s, e := newSharedHeartbeatSchedule(2, 1, time.Second, 16, stop, &wg, func(*actor) error { return nil }, func(uint64, error) {})
	if e != nil {
		t.Fatal(e)
	}
	if !s.Register(&actor{uid: 500001}) {
		t.Fatal("first registration failed")
	}
	if s.Register(&actor{uid: 500001}) {
		t.Fatal("duplicate actor accepted")
	}
	close(stop)
	wg.Wait()
	if s.failures.Load() != 1 {
		t.Fatal("duplicate was not recorded")
	}
}
