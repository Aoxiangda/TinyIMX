package main

import (
	"fmt"
	"net"
	"os"
	"syscall"
	"testing"
	"time"
)

func localError(operation string, code int) error {
	return &net.OpError{Op: "dial", Net: "tcp", Err: &os.SyscallError{Syscall: operation, Err: syscall.Errno(code)}}
}

func TestSetupHealthyUsesExactlyOneDial(t *testing.T) {
	left, right := net.Pipe()
	defer left.Close()
	defer right.Close()
	calls, sleeps, records := 0, 0, 0
	connection, err := dialSetupWithLocalBindRetry("owned:9000", 3,
		func(network, address string, timeout time.Duration) (net.Conn, error) {
			calls++
			if network != "tcp" || address != "owned:9000" || timeout != 5*time.Second {
				t.Fatal("dial contract changed")
			}
			return left, nil
		}, func(time.Duration) { sleeps++ }, nil, func(setupBindAttempt) { records++ })
	if err != nil || connection != left || calls != 1 || sleeps != 0 || records != 0 {
		t.Fatal("healthy path altered")
	}
}

func TestSetupBindTransientRetriesRemainVisible(t *testing.T) {
	left, right := net.Pipe()
	defer left.Close()
	defer right.Close()
	calls := 0
	var waits []time.Duration
	var records []setupBindAttempt
	connection, err := dialSetupWithLocalBindRetry("owned:9000", 3,
		func(string, string, time.Duration) (net.Conn, error) {
			calls++
			if calls < 4 {
				return nil, localError("bind", 10055)
			}
			return left, nil
		},
		func(delay time.Duration) { waits = append(waits, delay) }, nil,
		func(event setupBindAttempt) { records = append(records, event) })
	if err != nil || connection != left || calls != 4 || len(waits) != 3 || len(records) != 3 {
		t.Fatal("retry accounting")
	}
	for i, expected := range []time.Duration{50 * time.Millisecond, 100 * time.Millisecond, 200 * time.Millisecond} {
		if waits[i] != expected || records[i].Attempt != i+1 || records[i].Code != 10055 || !records[i].Retry {
			t.Fatal("bounded retry telemetry")
		}
	}
}

func TestSetupExhaustionStillFails(t *testing.T) {
	calls, sleeps := 0, 0
	var records []setupBindAttempt
	_, err := dialSetupWithLocalBindRetry("owned:9000", 3,
		func(string, string, time.Duration) (net.Conn, error) { calls++; return nil, localError("bind", 10055) },
		func(time.Duration) { sleeps++ }, nil, func(event setupBindAttempt) { records = append(records, event) })
	if err == nil || calls != 4 || sleeps != 3 || len(records) != 4 || records[3].Retry {
		t.Fatal("must preserve exhaustion failure")
	}
}

func TestSetupDoesNotRetryOtherFailures(t *testing.T) {
	for _, original := range []error{localError("bind", 10048), localError("connectex", 10055), localError("connectex", 10061), fmt.Errorf("bind10055 text is not a typed local failure")} {
		calls := 0
		_, err := dialSetupWithLocalBindRetry("owned:9000", 3,
			func(string, string, time.Duration) (net.Conn, error) { calls++; return nil, original },
			func(time.Duration) { t.Fatal("other errors must not delay/retry") }, nil, nil)
		if err != original || calls != 1 {
			t.Fatal("nonlocal failure hidden")
		}
	}
}

func TestSetupDefaultAndCancellationRemainFailFast(t *testing.T) {
	for _, budget := range []int{0, 3} {
		calls := 0
		cancelled := budget == 3
		_, err := dialSetupWithLocalBindRetry("owned:9000", budget,
			func(string, string, time.Duration) (net.Conn, error) { calls++; return nil, localError("bind", 10055) },
			func(time.Duration) { t.Fatal("cancel/default cannot sleep") }, func() bool { return cancelled }, nil)
		if err == nil || calls != 1 {
			t.Fatal("fail-fast changed")
		}
	}
}

func TestSetupCancellationDuringBackoffStopsNextDial(t *testing.T) {
	calls := 0
	stopped := false
	_, err := dialSetupWithLocalBindRetry("owned:9000", 3,
		func(string, string, time.Duration) (net.Conn, error) { calls++; return nil, localError("bind", 10055) },
		func(time.Duration) { stopped = true }, func() bool { return stopped }, nil)
	if err == nil || calls != 1 {
		t.Fatal("cancelled retry still dialed")
	}
}
