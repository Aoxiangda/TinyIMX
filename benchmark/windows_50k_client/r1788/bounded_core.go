// Shared by both offline-built clients. Traffic retains only active outcomes,
// compact issued/completed metadata, and a bounded asynchronous journal queue.
// Full exact outcomes remain on disk; no approximate percentile or sampling.
package main

import (
	"bufio"
	"container/heap"
	"crypto/sha256"
	"encoding/json"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"sort"
	"sync/atomic"
)

const maxPlannedRows = 8_000_000
const maxActiveRows = 262_144
const journalQueueRows = 1024

type journalError struct{ err error }
type rowJournal[T any] struct {
	queue    chan T
	done     chan struct{}
	failure  atomic.Pointer[journalError]
	accepted atomic.Int64
	written  atomic.Int64
	peak     atomic.Int64
}

func newRowJournal[T any](path string, capacity int) (*rowJournal[T], error) {
	if capacity < 1 || capacity > journalQueueRows {
		return nil, fmt.Errorf("invalid bounded journal capacity")
	}
	file, err := os.OpenFile(path, os.O_CREATE|os.O_EXCL|os.O_WRONLY, 0600)
	if err != nil {
		return nil, err
	}
	j := &rowJournal[T]{queue: make(chan T, capacity), done: make(chan struct{})}
	go func() {
		defer close(j.done)
		buffer := bufio.NewWriterSize(file, 64*1024)
		encoder := json.NewEncoder(buffer)
		for value := range j.queue {
			if j.failure.Load() != nil {
				continue
			}
			if err := encoder.Encode(value); err != nil {
				j.failure.CompareAndSwap(nil, &journalError{err})
				continue
			}
			j.written.Add(1)
		}
		for _, err := range []error{buffer.Flush(), file.Sync(), file.Close()} {
			if err != nil {
				j.failure.CompareAndSwap(nil, &journalError{err})
			}
		}
	}()
	return j, nil
}
func (j *rowJournal[T]) err() error {
	if err := j.failure.Load(); err != nil {
		return err.err
	}
	return nil
}

// Caller must preserve the active row on backpressure. It must reject the
// measurement explicitly, then drain it after readers/writers have stopped.
func (j *rowJournal[T]) try(value T) error {
	if err := j.err(); err != nil {
		return err
	}
	select {
	case j.queue <- value:
		j.accepted.Add(1)
		n := int64(len(j.queue))
		for old := j.peak.Load(); n > old && !j.peak.CompareAndSwap(old, n); old = j.peak.Load() {
		}
		return nil
	default:
		return fmt.Errorf("bounded journal queue exhausted; client measurement invalid")
	}
}

// Only after measured traffic/callbacks stop may final draining wait on IO.
func (j *rowJournal[T]) final(value T) error {
	if err := j.err(); err != nil {
		return err
	}
	j.queue <- value
	j.accepted.Add(1)
	return nil
}
func (j *rowJournal[T]) close() error {
	close(j.queue)
	<-j.done
	if err := j.err(); err != nil {
		return err
	}
	if j.accepted.Load() != j.written.Load() {
		return fmt.Errorf("journal accepted/written mismatch")
	}
	return nil
}

// Exact compact metadata for late duplicates, never whole historical outcomes.
// Explicit total-workload bound: <=66MiB for eight million planned rows.
type completionLedger struct {
	issued, finished           []uint64
	mids                       []uint64
	issuedCount, finishedCount int
}

func newCompletionLedger(planned int) (*completionLedger, error) {
	if planned < 1 || planned > maxPlannedRows {
		return nil, fmt.Errorf("explicit total workload bound exceeded")
	}
	return &completionLedger{issued: make([]uint64, (planned+63)/64), finished: make([]uint64, (planned+63)/64), mids: make([]uint64, planned)}, nil
}
func (l *completionLedger) valid(index int) bool { return index >= 0 && index < len(l.mids) }
func (l *completionLedger) wasIssued(index int) bool {
	return l.valid(index) && l.issued[index/64]&(uint64(1)<<uint(index%64)) != 0
}
func (l *completionLedger) isFinished(index int) bool {
	return l.valid(index) && l.finished[index/64]&(uint64(1)<<uint(index%64)) != 0
}
func (l *completionLedger) issue(index int) error {
	if !l.valid(index) || l.wasIssued(index) {
		return fmt.Errorf("duplicate or invalid issued row index")
	}
	l.issued[index/64] |= uint64(1) << uint(index%64)
	l.issuedCount++
	return nil
}
func (l *completionLedger) finish(index int, mid uint64) error {
	if !l.wasIssued(index) || l.isFinished(index) {
		return fmt.Errorf("unissued or duplicate completed row index")
	}
	l.finished[index/64] |= uint64(1) << uint(index%64)
	l.mids[index] = mid
	l.finishedCount++
	return nil
}
func (l *completionLedger) completedMID(index int) (uint64, bool) {
	if !l.isFinished(index) {
		return 0, false
	}
	return l.mids[index], true
}

func readRows[T any](path string, visit func(T) error) error {
	f, err := os.Open(path)
	if err != nil {
		return err
	}
	defer f.Close()
	decoder := json.NewDecoder(bufio.NewReaderSize(f, 64*1024))
	for {
		var value T
		err = decoder.Decode(&value)
		if err == io.EOF {
			return nil
		}
		if err != nil {
			return err
		}
		if err = visit(value); err != nil {
			return err
		}
	}
}
func xorRow[T any](digest *[32]byte, value T) error {
	raw, err := json.Marshal(value)
	if err != nil {
		return err
	}
	h := sha256.Sum256(raw)
	for i := range digest {
		digest[i] ^= h[i]
	}
	return nil
}

type mergeCursor[T any] struct {
	value   T
	index   int
	file    *os.File
	decoder *json.Decoder
}
type cursorHeap[T any] []*mergeCursor[T]

func (h cursorHeap[T]) Len() int           { return len(h) }
func (h cursorHeap[T]) Less(i, j int) bool { return h[i].index < h[j].index }
func (h cursorHeap[T]) Swap(i, j int)      { h[i], h[j] = h[j], h[i] }
func (h *cursorHeap[T]) Push(v any)        { *h = append(*h, v.(*mergeCursor[T])) }
func (h *cursorHeap[T]) Pop() any          { old := *h; v := old[len(old)-1]; *h = old[:len(old)-1]; return v }

// Completion-order journal -> canonical offered-index JSONL, with bounded
// chunks. This runs only after connections and measured traffic have stopped.
// Counts, strict unique indices and commutative canonical row hashes must match.
func canonicalRows[T any](spool, output string, indexOf func(T) int, expected int) error {
	const chunkRows = 8192
	var paths []string
	chunk := make([]T, 0, chunkRows)
	inputCount := 0
	var inputHash [32]byte
	flush := func() error {
		if len(chunk) == 0 {
			return nil
		}
		sort.Slice(chunk, func(i, j int) bool { return indexOf(chunk[i]) < indexOf(chunk[j]) })
		p := filepath.Join(filepath.Dir(output), fmt.Sprintf("completed-sort-%06d.jsonl", len(paths)))
		f, err := os.OpenFile(p, os.O_CREATE|os.O_EXCL|os.O_WRONLY, 0600)
		if err != nil {
			return err
		}
		b := bufio.NewWriter(f)
		encoder := json.NewEncoder(b)
		for _, row := range chunk {
			if err = encoder.Encode(row); err != nil {
				f.Close()
				return err
			}
		}
		if err = b.Flush(); err != nil {
			f.Close()
			return err
		}
		if err = f.Close(); err != nil {
			return err
		}
		paths = append(paths, p)
		clear(chunk)
		chunk = chunk[:0]
		return nil
	}
	err := readRows[T](spool, func(v T) error {
		if indexOf(v) < 0 || indexOf(v) >= maxPlannedRows {
			return fmt.Errorf("invalid serialized outcome index")
		}
		inputCount++
		if err := xorRow(&inputHash, v); err != nil {
			return err
		}
		chunk = append(chunk, v)
		if len(chunk) == chunkRows {
			return flush()
		}
		return nil
	})
	if err != nil {
		return err
	}
	if inputCount != expected {
		return fmt.Errorf("journal count differs from issued count")
	}
	if err = flush(); err != nil {
		return err
	}
	f, err := os.OpenFile(output, os.O_CREATE|os.O_EXCL|os.O_WRONLY, 0600)
	if err != nil {
		return err
	}
	defer f.Close()
	b := bufio.NewWriterSize(f, 64*1024)
	encoder := json.NewEncoder(b)
	h := &cursorHeap[T]{}
	var cursors []*mergeCursor[T]
	defer func() {
		for _, c := range cursors {
			c.file.Close()
		}
	}()
	for _, p := range paths {
		file, e := os.Open(p)
		if e != nil {
			return e
		}
		cursor := &mergeCursor[T]{file: file, decoder: json.NewDecoder(bufio.NewReaderSize(file, 4096))}
		cursors = append(cursors, cursor)
		if e = cursor.decoder.Decode(&cursor.value); e != nil {
			return e
		}
		cursor.index = indexOf(cursor.value)
		heap.Push(h, cursor)
	}
	previous := -1
	count := 0
	var outputHash [32]byte
	for h.Len() > 0 {
		cursor := heap.Pop(h).(*mergeCursor[T])
		if cursor.index <= previous {
			return fmt.Errorf("duplicate or unordered canonical outcome index")
		}
		previous = cursor.index
		if err = encoder.Encode(cursor.value); err != nil {
			return err
		}
		if err = xorRow(&outputHash, cursor.value); err != nil {
			return err
		}
		count++
		var next T
		err = cursor.decoder.Decode(&next)
		if err == nil {
			cursor.value = next
			cursor.index = indexOf(next)
			heap.Push(h, cursor)
		} else if err != io.EOF {
			return err
		}
	}
	if count != expected || outputHash != inputHash {
		return fmt.Errorf("canonical row count/hash mismatch")
	}
	if err = b.Flush(); err != nil {
		return err
	}
	if err = f.Sync(); err != nil {
		return err
	}
	if err = f.Close(); err != nil {
		return err
	}
	for _, cursor := range cursors {
		if err = cursor.file.Close(); err != nil {
			return err
		}
	}
	cursors = nil
	for _, p := range paths {
		if filepath.Dir(p) != filepath.Dir(output) {
			return fmt.Errorf("temporary chunk containment")
		}
		if err = os.Remove(p); err != nil {
			return err
		}
	}
	return nil
}
