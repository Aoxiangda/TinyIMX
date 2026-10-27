package main

import (
	"errors"
	"fmt"
	"net"
	"os"
	"syscall"
	"time"
)

type setupBindAttempt struct {
	UID     uint64 `json:"uid"`
	QPCNS   int64  `json:"qpc_ns"`
	Attempt int    `json:"attempt"`
	Code    int    `json:"local_bind_error_code"`
	Retry   bool   `json:"will_retry"`
	DelayMS int64  `json:"backoff_ms"`
}

func setupBindErrorCode(err error) int {
	var network *net.OpError
	var operation *os.SyscallError
	var code syscall.Errno
	if !errors.As(err, &network) || network.Op != "dial" ||
		!errors.As(network.Err, &operation) || operation.Syscall != "bind" ||
		!errors.As(operation.Err, &code) {
		return 0
	}
	return int(code)
}

// Retry only the observed Windows-local bind/10055 failure before login.
// The protocol sender, auth failures and all measured-message errors are outside
// this function. Every failed attempt remains in setup-bind-attempts.json.
func dialSetupWithLocalBindRetry(address string, budget int,
	dial func(string, string, time.Duration) (net.Conn, error),
	sleep func(time.Duration), stop func() bool, observe func(setupBindAttempt),
) (net.Conn, error) {
	if budget < 0 || budget > 3 {
		return nil, fmt.Errorf("setup bind retry budget outside0..3")
	}
	aborted := func() bool { return stop != nil && stop() }
	var last error
	for attempt := 0; ; attempt++ {
		if attempt > 0 && aborted() {
			return nil, last
		}
		connection, err := dial("tcp", address, 5*time.Second)
		if err == nil {
			return connection, nil
		}
		if connection != nil {
			_ = connection.Close()
		}
		last = err
		code := setupBindErrorCode(err)
		retry := code == 10055 && attempt < budget && !aborted()
		delay := time.Duration(0)
		if retry {
			delay = (50 * time.Millisecond) << attempt
		}
		if observe != nil {
			observe(setupBindAttempt{Attempt: attempt + 1, Code: code, Retry: retry, DelayMS: delay.Milliseconds()})
		}
		if !retry {
			return nil, err
		}
		sleep(delay)
	}
}
