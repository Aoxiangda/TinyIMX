#!/usr/bin/env python3
"""Audit and optionally insert three new local demo accounts; never update users."""
import argparse
import datetime
import getpass
import hashlib
import json
import os
from pathlib import Path
import re
import secrets
import subprocess


def names_for(prefix):
    names = [prefix + suffix for suffix in ("alice", "bob", "carol")]
    if not all(re.fullmatch(r"[A-Za-z0-9_]{1,64}", name) for name in names):
        raise ValueError("Use an ASCII letter/digit/underscore prefix; username limit is 64.")
    return names


def insert_sql(names, password):
    values = []
    for name, nickname in zip(names, ("Alice", "Bob", "Carol")):
        salt = secrets.token_bytes(16)
        digest = hashlib.pbkdf2_hmac("sha256", password.encode("utf-8"), salt, 100000, 32)
        values.append("('%s','%s',1,'%s','%s')" % (name, nickname, salt.hex(), digest.hex()))
    return ("START TRANSACTION;\n"
            "INSERT INTO im_users(username,nickname,status,password_salt,password_hash) VALUES "
            + ",".join(values) + ";\nCOMMIT;\n")


def mysql(container, statement):
    command = ["docker", "exec", "-i", container, "sh", "-c",
               'MYSQL_PWD="$MYSQL_ROOT_PASSWORD" exec mysql -uroot --batch --raw --skip-column-names "$MYSQL_DATABASE"']
    result = subprocess.run(command, input=statement + "\n", text=True,
                            encoding="utf-8", capture_output=True, timeout=30)
    if result.returncode:
        raise RuntimeError("MySQL command failed; audit retained. Existing accounts are never reset.")
    return result.stdout.strip()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mysql-container", required=True, help="Owned local deployment's MySQL container")
    parser.add_argument("--prefix", default="demo_", help="New usernames, e.g. demo_alice/bob/carol")
    parser.add_argument("--apply", action="store_true", help="Insert only after preflight; prompt for password")
    args = parser.parse_args()
    names = names_for(args.prefix)
    os.umask(0o077)
    root = Path(__file__).resolve().parents[3]
    audit = root / ".local" / "demo-accounts" / datetime.datetime.now(datetime.timezone.utc).strftime("%Y%m%dT%H%M%S%fZ")
    audit.mkdir(parents=True, mode=0o700)
    record = {"operation": "insert_three_new_demo_users" if args.apply else "read_only_preflight",
              "container": args.mysql_container, "names": names, "existing_rows_modified": False,
              "relations_inserted": False, "delete_update_upsert": False,
              "password_policy": "interactive input; independently salted PBKDF2-HMAC-SHA256; no plaintext persisted",
              "recovery": "Preserve inserted owned users and audit; never automatically delete data"}
    audit_file = audit / "audit.json"
    audit_file.write_text(json.dumps(record, ensure_ascii=False, indent=2), encoding="utf-8")
    quoted = ",".join("'" + name + "'" for name in names)
    existing = mysql(args.mysql_container, "SELECT COUNT(*) FROM im_users WHERE username IN (" + quoted + ");")
    if existing != "0":
        raise RuntimeError("A requested username exists. Use another --prefix; no row was changed.")
    if not args.apply:
        print(json.dumps({"status": "PREFLIGHT_PASS", "names": names, "audit": str(audit), "inserted": 0}))
        return
    password = getpass.getpass("Password for these three new local demo accounts: ")
    if not password or len(password.encode("utf-8")) > 256:
        raise ValueError("Password must contain 1-256 UTF-8 bytes.")
    if password != getpass.getpass("Confirm password: "):
        raise ValueError("Password confirmation does not match.")
    statement = insert_sql(names, password)
    (audit / "insert.sql").write_text(statement, encoding="utf-8")
    mysql(args.mysql_container, statement)
    rows = mysql(args.mysql_container, "SELECT user_id,username,nickname FROM im_users WHERE username IN (" + quoted + ") ORDER BY user_id;")
    record.update(status="PASS", accounts=[row.split("\t") for row in rows.splitlines()])
    if len(record["accounts"]) != 3:
        raise RuntimeError("Unexpected inserted-user result; retain audit and inspect manually.")
    audit_file.write_text(json.dumps(record, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps({"status": "PASS", "accounts": record["accounts"], "audit": str(audit)}, ensure_ascii=False))


if __name__ == "__main__":
    main()
