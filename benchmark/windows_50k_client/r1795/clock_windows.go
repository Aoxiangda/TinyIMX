package main

import (
	"sync"
	"syscall"
	"unsafe"
)

var clockDLL = syscall.NewLazyDLL("kernel32.dll")
var clockCounter = clockDLL.NewProc("QueryPerformanceCounter")
var clockFrequency = clockDLL.NewProc("QueryPerformanceFrequency")
var clockOnce sync.Once
var frequency int64

func absoluteClock() (int64, int64) {
	clockOnce.Do(func() {
		ok, _, _ := clockFrequency.Call(uintptr(unsafe.Pointer(&frequency)))
		if ok == 0 || frequency <= 0 {
			panic("QPC frequency unavailable")
		}
	})
	var counter int64
	ok, _, _ := clockCounter.Call(uintptr(unsafe.Pointer(&counter)))
	if ok == 0 {
		panic("QPC unavailable")
	}
	return counter/frequency*1_000_000_000 + (counter%frequency)*1_000_000_000/frequency, frequency
}
