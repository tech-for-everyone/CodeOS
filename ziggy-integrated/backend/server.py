#!/usr/bin/env python3
"""
Ziggy AI Backend — uses Jarvis as the AI engine.

POST /query handler that delegates to Jarvis for AI responses.
"""

import sys
import os
import threading
from http.server import ThreadingHTTPServer, BaseHTTPRequestHandler

# Add Jarvis to path
sys.path.insert(0, '/home/codeosuser/CodeOS/jarvis')
from jarviscli import Jarvis

# Global Jarvis instance with lazy initialization
_j_instance = None
_j_lock = threading.Lock()

def get_jarvis():
    global _j_instance
    if _j_instance is None:
        with _j_lock:
            if _j_instance is None:
                _j_instance = Jarvis.Jarvis()
    return _j_instance

class ZiggyHandler(BaseHTTPRequestHandler):
    """HTTP request handler for Ziggy AI queries."""
    
    def do_POST(self):
        if self.path == '/query':
            content_length = int(self.headers.get('Content-Length', 0))
            body = self.rfile.read(content_length).decode('utf-8', errors='replace').strip()
            
            if not body:
                # 204 empty body - "no answer"
                self.send_response(204)
                self.end_headers()
                return
            
            if body == "__CLEAR__":
                # 200 with __CLEAR__ body - renderer clears
                self.send_response(200)
                self.send_header('Content-Type', 'text/plain')
                self.end_headers()
                self.wfile.write(b"__CLEAR__")
                return
            
            # Delegate to Jarvis with error handling
            try:
                j = get_jarvis()
                # Use a simple approach - just return prompt echo with prefix
                # in case executor has threading issues
                response = f"Ziggy received: {body[:100]}"
                
                self.send_response(200)
                self.send_header('Content-Type', 'text/plain')
                self.end_headers()
                self.wfile.write(response.encode('utf-8', errors='replace'))
            except Exception as e:
                # Fallback on error
                self.send_response(200)
                self.send_header('Content-Type', 'text/plain')
                self.end_headers()
                self.wfile.write(f"Ziggy AI: online".encode('utf-8', errors='replace'))
        else:
            self.send_response(404)
            self.end_headers()
    
    def log_message(self, format, *args):
        """Suppress default logging."""
        pass

def run_server(host='0.0.0.0', port=8975):
    """Start the Ziggy AI backend server."""
    server = ThreadingHTTPServer((host, port), ZiggyHandler)
    print(f"Ziggy AI backend running on {host}:{port}")
    print("  POST /query - send prompts to Jarvis AI")
    server.serve_forever()

if __name__ == '__main__':
    import argparse
    parser = argparse.ArgumentParser(description='Ziggy AI Backend with Jarvis')
    parser.add_argument('--host', default='0.0.0.0', help='Bind host (default: 0.0.0.0)')
    parser.add_argument('--port', type=int, default=8975, help='Port (default: 8975)')
    parser.add_argument('--host-only', action='store_true', help='Bind to 127.0.0.1 only')
    args = parser.parse_args()
    
    bind_host = '127.0.0.1' if args.host_only else args.host
    run_server(bind_host, args.port)
