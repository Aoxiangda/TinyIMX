package main

import (
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"reflect"
	"strings"
	"testing"
)

type testJournalRow struct {
	Index    int    `json:"index"`
	MID      uint64 `json:"mid,omitempty"`
	Ack      int64  `json:"ack,omitempty"`
	Negative string `json:"negative,omitempty"`
	Text     string `json:"text"`
}

func TestJournalCanonicalKeepsExactMixedAndMissingFields(t *testing.T) {
	dir := t.TempDir()
	spool := filepath.Join(dir, "completion.jsonl")
	out := filepath.Join(dir, "requests.jsonl")
	j, err := newRowJournal[testJournalRow](spool, 1024)
	if err != nil {
		t.Fatal(err)
	}
	const count = 20007
	want := func(i int) testJournalRow {
		row := testJournalRow{Index: i, Text: fmt.Sprintf("精确正文 <%d> ", i) + strings.Repeat("x", 128)}
		if i%3 == 0 {
			row.MID = 9_223_372_036_854_700_000 + uint64(i)
			row.Ack = 9007199254740993 + int64(i)
		}
		if i%3 == 1 {
			row.Negative = "overloaded"
		}
		return row
	}
	for i := count - 1; i >= 0; i-- {
		if err = j.final(want(i)); err != nil {
			t.Fatal(err)
		}
	}
	if err = j.close(); err != nil {
		t.Fatal(err)
	}
	if err = canonicalRows[testJournalRow](spool, out, func(r testJournalRow) int { return r.Index }, count); err != nil {
		t.Fatal(err)
	}
	seen := 0
	if err = readRows[testJournalRow](out, func(row testJournalRow) error {
		expected := want(seen)
		if !reflect.DeepEqual(row, expected) {
			return fmt.Errorf("row%d changed: %+v", seen, row)
		}
		seen++
		return nil
	}); err != nil || seen != count {
		t.Fatalf("%d %v", seen, err)
	}
	paths, _ := filepath.Glob(filepath.Join(dir, "completed-sort-*"))
	if len(paths) != 0 {
		t.Fatal("temporary sort chunks retained after verified output")
	}
	// A conflicting index must invalidate canonicalization rather than silently
	// overwrite another request or report a lower denominator.
	f, _ := os.OpenFile(filepath.Join(dir, "duplicate.jsonl"), os.O_CREATE|os.O_EXCL|os.O_WRONLY, 0600)
	encoder := json.NewEncoder(f)
	encoder.Encode(want(1))
	encoder.Encode(want(1))
	f.Close()
	if canonicalRows[testJournalRow](filepath.Join(dir, "duplicate.jsonl"), filepath.Join(dir, "bad.jsonl"), func(r testJournalRow) int { return r.Index }, 2) == nil {
		t.Fatal("duplicate outcome accepted")
	}
}
func TestJournalBackpressureNeverPretendsWritten(t *testing.T) {
	j := &rowJournal[testJournalRow]{queue: make(chan testJournalRow, 1)}
	if err := j.try(testJournalRow{Index: 1}); err != nil {
		t.Fatal(err)
	}
	if err := j.try(testJournalRow{Index: 2}); err == nil {
		t.Fatal("full queue must reject the measurement")
	}
	if j.accepted.Load() != 1 || j.written.Load() != 0 {
		t.Fatal("unwritten rows counted as persisted")
	}
	row := <-j.queue
	if row.Index != 1 {
		t.Fatal("accepted row lost on backpressure")
	}
}
func TestCompactLedgerDistinguishesNegativeUnissuedAndLargeMID(t *testing.T) {
	ledger, err := newCompletionLedger(257)
	if err != nil {
		t.Fatal(err)
	}
	for _, i := range []int{0, 63, 64, 256} {
		if err = ledger.issue(i); err != nil {
			t.Fatal(err)
		}
	}
	if err = ledger.issue(64); err == nil {
		t.Fatal("duplicate issue accepted")
	}
	if err = ledger.finish(2, 1); err == nil {
		t.Fatal("unissued request completed")
	}
	if err = ledger.finish(0, 0); err != nil {
		t.Fatal(err)
	} // terminal negative, not missing
	const mid = uint64(9_223_372_036_854_775_807)
	if err = ledger.finish(256, mid); err != nil {
		t.Fatal(err)
	}
	if value, ok := ledger.completedMID(256); !ok || value != mid {
		t.Fatal("64-bit MID lost")
	}
	if value, ok := ledger.completedMID(0); !ok || value != 0 {
		t.Fatal("negative completion indistinguishable from unissued")
	}
	if _, ok := ledger.completedMID(63); ok {
		t.Fatal("inflight request prematurely finalized")
	}
	if err = ledger.finish(256, mid); err == nil {
		t.Fatal("duplicate finalization accepted")
	}
	if _, err = newCompletionLedger(maxPlannedRows + 1); err == nil {
		t.Fatal("memory bound omitted")
	}
}
