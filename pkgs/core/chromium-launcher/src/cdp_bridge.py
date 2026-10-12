#!/usr/bin/env python3
"""
CDP Bridge for CodeOS OpenWeb
HTTP server that bridges OpenWeb's HTTP requests to Chromium's CDP WebSocket.

Runs inside the debian-minimal container.
OpenWeb (kernel) → HTTP → this bridge → CDP WebSocket → Chromium
"""

import asyncio
import json
import base64
import sys
import os
from http.server import HTTPServer, BaseHTTPRequestHandler
from urllib.parse import urlparse, parse_qs
import websockets

# Global WebSocket connection to Chromium CDP
cdp_ws = None
cmd_id = 1
pending = {}

async def connect_cdp():
    """Connect to Chromium's CDP WebSocket."""
    global cdp_ws
    try:
        # Connect to CDP WebSocket
        cdp_ws = await websockets.connect("ws://localhost:9222/devtools/browser")
        
        # Enable required domains
        for domain in ["Page", "Runtime", "Network", "DOM", "Target"]:
            await cdp_ws.send(json.dumps({"id": 1, "method": f"{domain}.enable", "params": {}}))
            await cdp_ws.recv()
        
        # Auto-attach to new targets
        await cdp_ws.send(json.dumps({"id": 2, "method": "Target.setAutoAttach", 
            "params": {"autoAttach": True, "waitForDebuggerOnStart": False, "flatten": True}}))
        await cdp_ws.recv()
        
        print("CDP bridge connected to Chromium", file=sys.stderr)
        return True
    except Exception as e:
        print(f"CDP connection failed: {e}", file=sys.stderr)
        return False

async def cdp_send(method, params=None):
    """Send CDP command and wait for response."""
    global cmd_id, cdp_ws, pending
    
    if not cdp_ws:
        return {"error": "CDP not connected"}
    
    cmd_id += 1
    msg = {"id": cmd_id, "method": method, "params": params or {}}
    
    future = asyncio.get_event_loop().create_future()
    pending[cmd_id] = future
    
    await cdp_ws.send(json.dumps(msg))
    
    try:
        result = await asyncio.wait_for(future, timeout=30.0)
        return result
    except asyncio.TimeoutError:
        return {"error": "CDP timeout"}
    finally:
        pending.pop(cmd_id, None)

async def cdp_listener():
    """Listen for CDP responses and events."""
    global cdp_ws, pending
    try:
        async for msg in cdp_ws:
            data = json.loads(msg)
            if "id" in data and data["id"] in pending:
                future = pending.pop(data["id"])
                if "error" in data:
                    future.set_exception(Exception(data["error"]))
                else:
                    future.set_result(data.get("result", {}))
    except Exception as e:
        print(f"CDP listener error: {e}", file=sys.stderr)

class CDPBridgeHandler(BaseHTTPRequestHandler):
    def do_POST(self):
        parsed = urlparse(self.path)
        path = parsed.path
        
        # Read request body
        content_length = int(self.headers.get('Content-Length', 0))
        body = self.rfile.read(content_length).decode('utf-8') if content_length > 0 else "{}"
        
        try:
            params = json.loads(body) if body else {}
        except:
            params = {}
        
        result = {}
        
        try:
            if path == "/navigate":
                url = params.get("url", "")
                result = await_cdp("Page.navigate", {"url": url})
                
            elif path == "/screenshot":
                result = await_cdp("Page.captureScreenshot", {
                    "format": "png",
                    "fromSurface": True,
                    "captureBeyondViewport": True
                })
                # Return base64 PNG
                if "data" in result:
                    result = {"png_base64": result["data"]}
                
            elif path == "/content":
                # Get page HTML
                result = await_cdp("Runtime.evaluate", {
                    "expression": "document.documentElement.outerHTML",
                    "awaitPromise": True
                })
                if "result" in result and "value" in result["result"]:
                    result = {"html": result["result"]["value"]}
                
            elif path == "/eval":
                expr = params.get("expression", "")
                result = await_cdp("Runtime.evaluate", {
                    "expression": expr,
                    "awaitPromise": True
                })
                
            elif path == "/navigate_back":
                result = await_cdp("Page.goBack", {})
                
            elif path == "/navigate_forward":
                result = await_cdp("Page.goForward", {})
                
            elif path == "/reload":
                result = await_cdp("Page.reload", {})
                
            elif path == "/stop":
                result = await_cdp("Page.stopLoading", {})
                
            elif path == "/status":
                result = {"connected": cdp_ws is not None}
                
            else:
                self.send_error(404)
                return
            
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.end_headers()
            self.wfile.write(json.dumps(result).encode())
            
        except Exception as e:
            self.send_response(500)
            self.send_header("Content-Type", "application/json")
            self.end_headers()
            self.wfile.write(json.dumps({"error": str(e)}).encode())

    def do_GET(self):
        if self.path == "/health":
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.end_headers()
            self.wfile.write(json.dumps({"status": "ok", "cdp_connected": cdp_ws is not None}).encode())
        else:
            self.send_error(404)

def await_cdp(method, params):
    """Synchronous wrapper for CDP commands."""
    loop = asyncio.new_event_loop()
    asyncio.set_event_loop(loop)
    try:
        return loop.run_until_complete(cdp_send(method, params))
    finally:
        loop.close()

def run_server():
    # Start CDP connection in background
    loop = asyncio.new_event_loop()
    asyncio.set_event_loop(loop)
    
    # Connect to CDP
    connected = loop.run_until_complete(connect_cdp())
    if not connected:
        print("Failed to connect to CDP, server will return errors", file=sys.stderr)
    
    # Start CDP listener
    loop.create_task(cdp_listener())
    
    # Start HTTP server
    server = HTTPServer(('0.0.0.0', 9223), CDPBridgeHandler)
    print("CDP Bridge HTTP server on port 9223", file=sys.stderr)
    
    try:
        loop.run_until_complete(asyncio.get_event_loop().run_in_executor(None, server.serve_forever))
    except KeyboardInterrupt:
        pass
    finally:
        server.shutdown()

if __name__ == "__main__":
    run_server()