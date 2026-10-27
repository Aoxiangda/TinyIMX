package main

import (
	"regexp"
	"strings"
	"sync"
	"testing"
)

func oracleEnum(s string) bool {
	if len(s) < 1 || len(s) > 64 {
		return false
	}
	for i := 0; i < len(s); i++ {
		if s[i] != '_' && (s[i] < 'a' || s[i] > 'z') {
			return false
		}
	}
	return true
}
func TestCachedNegativeEnumExactLanguage(t *testing.T) {
	check := func(s string) {
		t.Helper()
		if negativeReasonEnumPattern.MatchString(s) != oracleEnum(s) {
			t.Fatalf("enum language differs: %q", s)
		}
	}
	check("")
	for a := 0; a < 128; a++ {
		check(string([]byte{byte(a)}))
		for b := 0; b < 128; b++ {
			check(string([]byte{byte(a), byte(b)}))
		}
	}
	for _, s := range []string{"overloaded", "message_persistence_uncertain", strings.Repeat("a", 64), strings.Repeat("a", 65), "a\n", "a\r", "é", "中文", "a\x00"} {
		check(s)
	}
}
func TestCachedNegativeEnumConcurrentRead(t *testing.T) {
	var wg sync.WaitGroup
	for worker := 0; worker < 8; worker++ {
		wg.Add(1)
		go func() {
			defer wg.Done()
			for i := 0; i < 10000; i++ {
				if !negativeReasonEnumPattern.MatchString("message_persistence_uncertain") || negativeReasonEnumPattern.MatchString("bad reason") {
					t.Error("concurrent enum result")
				}
			}
		}()
	}
	wg.Wait()
}
func TestCachedNegativeEnumNoAllocation(t *testing.T) {
	n := testing.AllocsPerRun(1000, func() {
		if !negativeReasonEnumPattern.MatchString("message_persistence_uncertain") {
			panic("invalid")
		}
	})
	if n != 0 {
		t.Fatalf("cached match allocated: %v", n)
	}
}
func BenchmarkNegativeEnumCompileEach(b *testing.B) {
	b.ReportAllocs()
	for i := 0; i < b.N; i++ {
		if !regexp.MustCompile(`^[a-z_]{1,64}$`).MatchString("message_persistence_uncertain") {
			b.Fatal("invalid")
		}
	}
}
func BenchmarkNegativeEnumCached(b *testing.B) {
	b.ReportAllocs()
	for i := 0; i < b.N; i++ {
		if !negativeReasonEnumPattern.MatchString("message_persistence_uncertain") {
			b.Fatal("invalid")
		}
	}
}
