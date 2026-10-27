package main

import (
	"fmt"
	"net"
	"sync/atomic"
	"syscall"
)

// The 2KiB bufio reader is a user-space buffer. Socket receive/send buffers
// are separate Windows kernel allocations. This opt-in only changes setup;
// it never changes offered pacing, payloads, ACKs, deadlines or GC policy.
var kernelBufferStats struct {
	count, recvMin, recvMax, sendMin, sendMax atomic.Int64
}

func validateKernelBufferKiB(kib int) error {
	if kib != 0 && kib != 4 && kib != 8 && kib != 16 {
		return fmt.Errorf("tcp-buffer-kib must be0(default),4,8 or16")
	}
	return nil
}

func observedKernelBuffers(conn *net.TCPConn) (recv, send int, err error) {
	raw, err := conn.SyscallConn()
	if err != nil {
		return 0, 0, err
	}
	var optionErr error
	err = raw.Control(func(fd uintptr) {
		recv, optionErr = syscall.GetsockoptInt(syscall.Handle(fd), syscall.SOL_SOCKET, syscall.SO_RCVBUF)
		if optionErr == nil {
			send, optionErr = syscall.GetsockoptInt(syscall.Handle(fd), syscall.SOL_SOCKET, syscall.SO_SNDBUF)
		}
	})
	if err == nil {
		err = optionErr
	}
	return
}

func recordKernelRange(lo, hi *atomic.Int64, value int64) {
	for old := lo.Load(); old == 0 || value < old; old = lo.Load() {
		if lo.CompareAndSwap(old, value) {
			break
		}
	}
	for old := hi.Load(); value > old; old = hi.Load() {
		if hi.CompareAndSwap(old, value) {
			break
		}
	}
}

func configureKernelBuffers(conn net.Conn, kib int) error {
	if err := validateKernelBufferKiB(kib); err != nil {
		return err
	}
	if kib == 0 { // Preserve the frozen default without extra socket operations.
		return nil
	}
	tcp, ok := conn.(*net.TCPConn)
	if !ok {
		return fmt.Errorf("TCP connection required for owned buffer configuration")
	}
	if err := tcp.SetReadBuffer(kib * 1024); err != nil {
		return err
	}
	if err := tcp.SetWriteBuffer(kib * 1024); err != nil {
		return err
	}
	recv, send, err := observedKernelBuffers(tcp)
	if err != nil {
		return err
	}
	if recv < kib*1024 || send < kib*1024 {
		return fmt.Errorf("kernel socket buffer smaller than request")
	}
	recordKernelRange(&kernelBufferStats.recvMin, &kernelBufferStats.recvMax, int64(recv))
	recordKernelRange(&kernelBufferStats.sendMin, &kernelBufferStats.sendMax, int64(send))
	kernelBufferStats.count.Add(1)
	return nil
}

func kernelBufferSnapshot(kib int) map[string]any {
	return map[string]any{
		"requested_kib": kib, "configured_sockets": kernelBufferStats.count.Load(),
		"receive_min_bytes": kernelBufferStats.recvMin.Load(), "receive_max_bytes": kernelBufferStats.recvMax.Load(),
		"send_min_bytes": kernelBufferStats.sendMin.Load(), "send_max_bytes": kernelBufferStats.sendMax.Load(),
		"pacing_deadlines_payload_GC_unchanged": true,
	}
}
