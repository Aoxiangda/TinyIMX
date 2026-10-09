#!/usr/bin/env python3
import argparse
import json
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

class Handler(BaseHTTPRequestHandler):
    server_version = "TinyIMXFakeAI/1"

    def log_message(self, fmt, *args):
        print("[fake-ai] " + (fmt % args), flush=True)

    def send_json(self, status, payload):
        data = json.dumps(payload, separators=(",", ":")).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Connection", "close")
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self):
        if self.path == "/health":
            self.send_json(200, {"status":"ok","service":"tinyimx-fake-ai"})
        else:
            self.send_json(404, {"error":"not found"})

    def do_POST(self):
        if self.path != "/v1/chat/completions":
            self.send_json(404, {"error":"not found"})
            return
        try:
            length = int(self.headers.get("Content-Length", "0"))
            if length <= 0 or length > 1024 * 1024:
                raise ValueError("invalid content length")
            body = json.loads(self.rfile.read(length))
            messages = body.get("messages", [])
            tools = body.get("tools", [])
            names = [x.get("function", {}).get("name") for x in tools]
            if "tinyimx.system.echo" not in names:
                self.send_json(400, {"error":{"message":"echo tool missing"}})
                return
            tool_messages = [m for m in messages if m.get("role") == "tool"]
            if not tool_messages:
                payload = {
                    "id":"fake-1","object":"chat.completion","model":body.get("model","fake"),
                    "choices":[{
                        "index":0,"finish_reason":"tool_calls",
                        "message":{
                            "role":"assistant","content":None,
                            "tool_calls":[{
                                "id":"call_echo_1","type":"function",
                                "function":{"name":"tinyimx.system.echo","arguments":"{\"text\":\"hello-ai-runtime\"}"}
                            }]
                        }
                    }]
                }
            else:
                tool_payload = json.loads(tool_messages[-1].get("content", "{}"))
                echoed = tool_payload.get("structuredContent", {}).get("text", "")
                principal = tool_payload.get("structuredContent", {}).get("principal", "")
                payload = {
                    "id":"fake-2","object":"chat.completion","model":body.get("model","fake"),
                    "choices":[{
                        "index":0,"finish_reason":"stop",
                        "message":{"role":"assistant","content":f"AI_RUNTIME_SMOKE_OK echo={echoed} principal={principal}"}
                    }]
                }
            self.send_json(200, payload)
        except Exception as exc:
            self.send_json(400, {"error":{"message":str(exc)}})


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=18181)
    args = parser.parse_args()
    server = ThreadingHTTPServer((args.host, args.port), Handler)
    print(f"FAKE_AI_READY=http://{args.host}:{args.port}", flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()

if __name__ == "__main__":
    main()
