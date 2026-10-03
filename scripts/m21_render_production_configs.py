#!/usr/bin/env python3
import argparse
import json
import os
from pathlib import Path


def env(name, default=None, required=False, secret=False):
    value = os.environ.get(name, default)
    if required and (value is None or value == ""):
        raise SystemExit(f"ERROR: missing required environment variable: {name}")
    if secret and (value is None or value == "" or "CHANGE_ME" in value):
        raise SystemExit(f"ERROR: unsafe placeholder/empty secret: {name}")
    return value


def deployment_env():
    value = env("TINYIMX_DEPLOYMENT_ENV", "prod")
    allowed = {"dev", "test", "prod"}
    if value not in allowed:
        raise SystemExit(
            "ERROR: TINYIMX_DEPLOYMENT_ENV must be one of: dev, test, prod"
        )
    return value

def write_json(path: Path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_suffix(path.suffix + ".tmp")
    tmp.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
    os.chmod(tmp, 0o600)
    os.replace(tmp, path)


def common_config(instance_id: str, advertise_host: str):
    return {
        "app": {
            "name": "TinyIMX",
            "env": deployment_env(),
            "instance_id": instance_id,
        },
        "server": {
            "host": "0.0.0.0", "port": 9000, "backlog": 16384, "io_thread_count": 2,
        },
        "logger": {
            "level": "info", "console": True, "file": f"/tmp/tinyimx/{instance_id}.log",
            "max_file_size_mb": 20, "max_backup_files": 3,
            "async": False, "flush_each_log": False,
        },
        "thread_pool": {
            "worker_threads": 4, "queue_capacity": 2048,
            "enable_dynamic_resize": False, "min_threads": 2, "max_threads": 8,
            "queue_full_policy": "block", "scale_up_threshold": 0.75,
            "scale_down_threshold": 0.20, "manager_thread_interval_ms": 1000,
            "scale_cooldown_ms": 3000, "worker_idle_timeout_ms": 5000,
        },
        "business_runtime": {
            "worker_threads": 4, "max_pending_tasks": 512, "stripe_count": 64,
            "per_stripe_queue_capacity": 64, "default_deadline_ms": 3000,
            "shutdown_timeout_ms": 30000,
        },
        "protocol": {
            "max_body_size": 1048576, "heartbeat_interval_sec": 30,
            "heartbeat_timeout_sec": 90,
        },
        "rpc": {
            "enable": True, "connect_timeout_ms": 1000, "request_timeout_ms": 3000,
            "compress": "none", "encrypt": False,
        },
        "mysql": {
            "enable": True, "host": "mysql", "port": 3306,
            "database": env("TINYIMX_MYSQL_DATABASE", "tinyimx"),
            "user": env("TINYIMX_MYSQL_USER", "tinyimx"),
            "password": env("TINYIMX_MYSQL_PASSWORD", required=True, secret=True),
            "pool_size": 8,
        },
        "redis": {
            "enable": True, "host": "redis", "port": 6379,
            "password": env("TINYIMX_REDIS_PASSWORD", required=True, secret=True),
            "db": 0, "pool_size": 8,
        },
        "rocketmq": {
            "enable": True, "endpoint": "rocketmq-proxy:8081",
            "message_topic": env("TINYIMX_ROCKETMQ_TOPIC", "tinyimx-message-events"),
            "request_timeout_ms": 3000, "tls": False,
            "access_key": "", "access_secret": "",
        },
        "outbox_relay": {
            "enable": False, "instance_id": f"{instance_id}-outbox",
            "batch_size": 32, "worker_threads": 4, "max_inflight": 128,
            "poll_interval_ms": 100, "lease_ms": 30000,
            "lease_renew_interval_ms": 5000, "retry_base_ms": 200,
            "retry_max_ms": 30000, "published_retention_hours": 168,
            "cleanup_interval_ms": 60000, "cleanup_batch_size": 1000,
            "shutdown_timeout_ms": 10000,
        },
        "unread_projection": {
            "enable": False, "owner": "gateway", "shadow_mode": False,
            "consumer_group": "tinyimx-unread-projector-v1",
            "consumer_request_timeout_ms": 30000, "batch_size": 16,
            "invisible_duration_ms": 30000, "await_duration_ms": 5000,
            "receive_error_backoff_ms": 500,
        },
        "gateway_registry": {
            "enable": False, "advertise_host": advertise_host,
            "lease_ttl_seconds": 15, "heartbeat_interval_seconds": 5,
            "discovery_refresh_interval_seconds": 3,
        },
        "zookeeper": {
            "enable": True, "connect_string": "zookeeper:2181",
            "session_timeout_ms": 10000, "connect_timeout_ms": 5000,
            "registration_timeout_ms": 15000,
            "service_root": "/tinyimx/services", "advertise_host": advertise_host,
            "service_version": "v1",
        },
        "service_discovery": {
            "provider": "zookeeper", "initial_sync_timeout_ms": 10000,
            "snapshot_stale_after_ms": 30000, "retain_last_known_good": True,
        },
        "observability": {
            "enable": True, "metrics_enable": True, "traces_enable": True,
            "otlp_endpoint": "http://otel-collector:4317",
            "metric_export_interval_ms": 5000, "export_timeout_ms": 3000,
            "shutdown_timeout_ms": 5000, "trace_max_queue_size": 2048,
            "trace_max_export_batch_size": 512, "trace_schedule_delay_ms": 1000,
        },
        "mcp": {
            "enable": False, "endpoint": "http://mcp-server:18080/mcp",
            "timeout_ms": 5000, "listen_host": "0.0.0.0", "listen_port": 18080,
            "endpoint_path": "/mcp", "io_threads": 1, "worker_threads": 4,
            "queue_capacity": 128, "max_request_bytes": 1048576,
            "allowed_origins": [], "auth_token_env": "TINYIMX_MCP_TOKEN",
            "static_user_id": int(env("TINYIMX_MCP_STATIC_USER_ID", "1")),
            "static_subject": "tinyimx:mcp:production",
        },
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output-dir", required=True)
    args = parser.parse_args()
    out = Path(args.output_dir)

    for name in (
        "TINYIMX_MYSQL_PASSWORD", "TINYIMX_MYSQL_ROOT_PASSWORD",
        "TINYIMX_REDIS_PASSWORD", "TINYIMX_MCP_TOKEN",
    ):
        env(name, required=True, secret=True)

    services = {
        "user": ("tinyimx-user-1", "user-service"),
        "social": ("tinyimx-social-1", "social-service"),
        "message": ("tinyimx-message-1", "message-service"),
        "group": ("tinyimx-group-1", "group-service"),
        "file": ("tinyimx-file-1", "file-service"),
    }
    for name, (instance_id, host) in services.items():
        cfg = common_config(instance_id, host)
        cfg["app"]["name"] = f"TinyIMX-{name.capitalize()}Service"
        write_json(out / f"{name}.json", cfg)

    for name in ("gateway-a", "gateway-b"):
        cfg = common_config(name, name)
        cfg["app"]["name"] = "TinyIMX-Gateway"
        cfg["gateway_registry"]["enable"] = True
        write_json(out / f"{name}.json", cfg)

    relay = common_config("tinyimx-outbox-relay-1", "outbox-relay")
    relay["app"]["name"] = "TinyIMX-OutboxRelay"
    relay["zookeeper"]["enable"] = False
    relay["service_discovery"]["provider"] = "static"
    relay["outbox_relay"]["enable"] = True
    relay["outbox_relay"]["instance_id"] = "outbox-relay-1"
    write_json(out / "outbox-relay.json", relay)

    projector = common_config("tinyimx-unread-projector-1", "unread-projector")
    projector["app"]["name"] = "TinyIMX-UnreadProjector"
    projector["zookeeper"]["enable"] = False
    projector["service_discovery"]["provider"] = "static"
    projector["unread_projection"]["enable"] = True
    projector["unread_projection"]["owner"] = "projector"
    projector["unread_projection"]["shadow_mode"] = False
    write_json(out / "unread-projector.json", projector)

    mcp = common_config("tinyimx-mcp-1", "mcp-server")
    mcp["app"]["name"] = "TinyIMX-MCPServer"
    mcp["mcp"]["enable"] = True
    write_json(out / "mcp.json", mcp)

    ai = {
        "ai": {
            "provider": "openai_compatible",
            "endpoint": env("TINYIMX_AI_ENDPOINT", "http://host.docker.internal:11434/v1/chat/completions"),
            "api_key_env": "TINYIMX_AI_API_KEY",
            "connect_timeout_ms": 2000, "request_timeout_ms": 30000,
            "max_response_bytes": 4194304,
        },
        "mcp": {
            "endpoint": "http://mcp-server:18080/mcp", "token_env": "TINYIMX_MCP_TOKEN",
            "client_name": "tinyimx-ai-agent", "client_version": "m21",
            "connect_timeout_ms": 2000, "request_timeout_ms": 10000,
            "max_response_bytes": 4194304,
        },
        "agent": {
            "model": env("TINYIMX_AI_MODEL", "qwen3:8b"),
            "system_prompt": "You are the TinyIMX assistant. Use only provided read-only TinyIMX tools. Never invent tool results.",
            "allowed_tools": [
                "tinyimx.user.get_self_profile", "tinyimx.social.list_friends",
                "tinyimx.message.list_conversations", "tinyimx.message.list_history",
                "tinyimx.group.get", "tinyimx.group.list_my_groups",
                "tinyimx.group.list_members", "tinyimx.file.get_metadata",
            ],
            "max_tool_rounds": 4, "max_tool_calls_per_round": 8,
            "max_total_tool_calls": 16, "repeated_identical_call_limit": 3,
        },
        "observability": {
            "enable": True, "metrics_enable": False, "traces_enable": True,
            "otlp_endpoint": "http://otel-collector:4317",
            "metric_export_interval_ms": 5000, "export_timeout_ms": 3000,
            "shutdown_timeout_ms": 5000, "trace_max_queue_size": 2048,
            "trace_max_export_batch_size": 512, "trace_schedule_delay_ms": 1000,
            "service_instance_id": "tinyimx-ai-agent-1",
            "deployment_environment": deployment_env(),
        },
    }
    write_json(out / "ai-agent.json", ai)

    print(f"M21_PRODUCTION_CONFIG_RENDER=PASS output_dir={out}")


if __name__ == "__main__":
    main()
