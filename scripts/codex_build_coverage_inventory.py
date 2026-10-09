#!/usr/bin/env python3
"""Discover acceptance obligations from source; discovery is never a PASS."""
import argparse
import json
import pathlib
import re
import subprocess


def family(name):
    if "File" in name or "Upload" in name or "Download" in name:
        return "file"
    if "Group" in name:
        return "group_message" if "Message" in name else "group_control"
    if "Friend" in name:
        return "social"
    if "Profile" in name:
        return "profile"
    if "Login" in name or "Authenticate" in name or "Heartbeat" in name:
        return "auth_session_presence"
    return "private_message_and_reads"


def inventory(root):
    packet = root / "common/protocol/Packet.h"
    enums = dict(re.findall(r"\b(k\w+)\s*=\s*(\d+)", packet.read_text()))
    public = []
    acknowledgments = {"kChatMessage", "kChatDeliveryAck", "kGroupMessageDeliveryAck", "kHeartbeat"}
    for name, value in enums.items():
        if "GatewayForward" in name:
            continue
        if not name.endswith("Request") and name not in acknowledgments:
            continue
        response = name.replace("Request", "Response") if name.endswith("Request") else {
            "kChatMessage": "kChatAck", "kHeartbeat": "kHeartbeat"
        }.get(name)
        public.append({"name": name, "type": int(value), "response": response,
                       "family": family(name), "source": "common/protocol/Packet.h",
                       "status": "NOT_RUN"})
    rpcs = []
    for proto in sorted((root / "proto").rglob("*.proto")):
        text = re.sub(r"//[^\n]*", "", proto.read_text())
        for service, body in re.findall(r"service\s+(\w+)\s*\{([^}]+)\}", text, re.S):
            for method, request, response in re.findall(
                    r"rpc\s+(\w+)\s*\(\s*(\w+)\s*\)\s*returns\s*\(\s*(\w+)\s*\)", body):
                rpcs.append({"service": service, "method": method, "request": request,
                             "response": response, "source": str(proto.relative_to(root)),
                             "status": "NOT_RUN"})
    chains = [
        {"name": "authentication-and-session", "steps": ["login", "profile", "heartbeat", "session replacement", "reconnect", "presence ownership"], "checks": ["authenticated identity", "old-session fencing", "heartbeat and presence convergence"]},
        {"name": "friendship-and-private-state", "steps": ["friend request create/list", "accept/reject", "private send", "delivery ACK", "history", "read", "conversation list"], "checks": ["permission change ordering", "logical idempotency", "actual receiver confirmation", "unread/read projection reconciliation"]},
        {"name": "group-membership-and-delivery", "steps": ["create/update", "join/invite", "role/mute", "group send", "recipient ACK", "kick/leave", "ownership transfer", "disband"], "checks": ["actual recipient set", "membership epoch", "permission transitions", "independent recipient confirmation"]},
        {"name": "file-and-messaging", "steps": ["begin upload", "partial chunks", "crash", "same-identity resume", "finalize", "attachment message", "authorized range download"], "checks": ["unfinished-upload recovery", "SHA integrity", "authorization", "simultaneous private/group latency"]},
        {"name": "event-backbone-and-projections", "steps": ["durable write", "Outbox", "MQ outage", "backlog", "MQ recovery", "unread projection", "history/read comparison"], "checks": ["durable-message preservation", "backlog drain", "per-aggregate projection correctness", "no new quarantine"]},
        {"name": "ai-mcp-and-im", "steps": ["authenticated AI request", "tool permission", "provider timeout", "private/group/file traffic"], "checks": ["actual M21 AI/MCP contracts", "authorization", "IM resource isolation"]},
        {"name": "uncertainty-and-cross-process-recovery", "steps": ["persistence timeout", "same logical ID resolve", "source Gateway exit", "pending rediscovery", "receiver delivery", "late ACK"], "checks": ["no false success", "no new logical ID", "receiver does not need relogin", "cross-process durable recovery", "bounded and fair scheduling"]},
        {"name": "hotspot-backpressure-and-soak", "steps": ["ordinary users", "hot sender/group", "slow receiver", "overload", "release", "recovery probe", "soak"], "checks": ["pressure actually occurred", "bounded resources", "fairness", "post-overload correctness", "resource trends"]}
    ]
    for chain in chains:
        chain["status"] = "NOT_RUN"
    return {"schema": 1, "source_head": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=root, text=True).strip(),
            "kind": "acceptance_obligations_not_execution_evidence", "scales": [10000, 20000, 30000, 50000],
            "public_packet_operations": public, "internal_rpc_operations": rpcs,
            "cross_function_chains": chains,
            "limits": ["Each discovered function still needs actual actor and assertions.",
                       "AI/MCP HTTP/tools require a separate source inventory beyond protobuf.",
                       "Each scale retains separate actual results; discovery alone cannot establish coverage."]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=pathlib.Path, default=pathlib.Path(__file__).resolve().parents[1])
    parser.add_argument("--output", type=pathlib.Path, required=True)
    args = parser.parse_args()
    data = inventory(args.root.resolve())
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(data, ensure_ascii=False, indent=2) + "\n")
    print(json.dumps({"public_packet_operations": len(data["public_packet_operations"]),
                      "internal_rpc_operations": len(data["internal_rpc_operations"]),
                      "cross_function_chains": len(data["cross_function_chains"]), "status": "INVENTORY_ONLY"}))


if __name__ == "__main__":
    main()
