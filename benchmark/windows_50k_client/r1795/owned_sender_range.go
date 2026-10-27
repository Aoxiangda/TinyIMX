package main

// IDs outside the actual configured actor registry are never accepted.
func validOwnedSender(uid uint64, connections int) bool {
	return connections >= 2 && connections <= 64000 && connections%2 == 0 &&
		uid >= 500001 && uid-500001 < uint64(connections)
}
