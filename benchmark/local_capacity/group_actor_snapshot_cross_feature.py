#!/usr/bin/env python3
"""Original full feature chain plus post-leave/kick/disband fail-closed checks."""
import hashlib
import pathlib
import sys
import cross_feature_actor as original

expected_original_sha256 = "3f4327755929487d58114876cd87cbc21311a858a72a4d44a23acf8c2d3f08a9"
assert hashlib.sha256(pathlib.Path(original.__file__).read_bytes()).hexdigest() == expected_original_sha256
OriginalRun = original.Run

class GroupSnapshotRun(OriginalRun):
    def group(self, c, name, kind, gid, **kw):
        response = super().group(c, name, kind, gid, **kw)
        if name in ("group-leave", "group-kick-d"):
            actor = c if name == "group-leave" else next(x for x in self.clients if x.uid == self.args.users[3])
            label = "left" if name == "group-leave" else "kicked"
            self.request(actor, label+"-member-list-denied", 2045, {"group_id":gid,"limit":20}, success=False)
            mid = self.prefix+"-"+label+"-denied"
            self.request(actor, label+"-group-send-denied", 2049,
                         {"group_id":gid,"client_message_id":mid,"message_type":1,"content":label+"-must-not-persist"},
                         success=False)
            self.authoritative(label+"-send-no-durable-row",
                               f"SELECT COUNT(*) FROM im_group_messages WHERE group_id={gid} AND client_message_id='{mid}'", "0")
        elif name == "group-disband-owned-test-group":
            self.request(c, "disbanded-member-list-denied", 2045, {"group_id":gid,"limit":20}, success=False)
        return response

original.Run = GroupSnapshotRun
# Original main's source audit now records this concrete wrapper; the dependency
# is SHA-bound above and separately bound in the outer pre-mutation audit.
original.__file__ = __file__
if __name__ == "__main__":
    raise SystemExit(original.main())
