package main

import (
	"bytes"
	"io"
	"net"
	"testing"
	"time"
)

func TestOwnedKernelBuffersAndLargeSegmentedFrame(t *testing.T) {
	for _, kib := range []int{0, 4, 8, 16} {
		t.Run(string(rune('a'+kib)), func(t *testing.T) {
			listener, err := net.Listen("tcp4", "127.0.0.1:0")
			if err != nil {
				t.Fatal(err)
			}
			defer listener.Close()
			payload := bytes.Repeat([]byte("bounded-large-frame-"), 4096)
			done := make(chan error, 1)
			go func() {
				conn, err := listener.Accept()
				if err != nil {
					done <- err
					return
				}
				defer conn.Close()
				conn.SetDeadline(time.Now().Add(5 * time.Second))
				got := make([]byte, len(payload))
				_, err = io.ReadFull(conn, got)
				if err == nil && !bytes.Equal(got, payload) {
					err = io.ErrUnexpectedEOF
				}
				if err == nil {
					_, err = conn.Write(got)
				}
				done <- err
			}()
			conn, err := net.DialTimeout("tcp4", listener.Addr().String(), 2*time.Second)
			if err != nil {
				t.Fatal(err)
			}
			defer conn.Close()
			conn.SetDeadline(time.Now().Add(5 * time.Second))
			defaultRecv, defaultSend, err := observedKernelBuffers(conn.(*net.TCPConn))
			if err != nil {
				t.Fatal(err)
			}
			if err = configureKernelBuffers(conn, kib); err != nil {
				t.Fatal(err)
			}
			actualRecv, actualSend, err := observedKernelBuffers(conn.(*net.TCPConn))
			if err != nil {
				t.Fatal(err)
			}
			if kib == 0 && (actualRecv != defaultRecv || actualSend != defaultSend) {
				t.Fatal("default buffers changed")
			}
			if kib > 0 && (actualRecv < kib*1024 || actualSend < kib*1024) {
				t.Fatal("requested buffers not applied")
			}
			if _, err = conn.Write(payload); err != nil {
				t.Fatal(err)
			}
			got := make([]byte, len(payload))
			if _, err = io.ReadFull(conn, got); err != nil {
				t.Fatal(err)
			}
			if !bytes.Equal(got, payload) {
				t.Fatal("segmented frame changed")
			}
			if err = <-done; err != nil {
				t.Fatal(err)
			}
			t.Logf("requestedKiB=%d defaults=%d/%d actual=%d/%d", kib, defaultRecv, defaultSend, actualRecv, actualSend)
		})
	}
}

func TestInvalidKernelBufferRejectedBeforeSocketUse(t *testing.T) {
	for _, value := range []int{-1, 1, 2, 32, 1024} {
		if configureKernelBuffers(nil, value) == nil {
			t.Fatalf("accepted unqualified value%d", value)
		}
	}
}
