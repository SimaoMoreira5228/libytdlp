import sys
import time
import socketserver
from http.server import BaseHTTPRequestHandler, HTTPServer

PAYLOAD = b""
RATE = 0


class _Server(HTTPServer):
    # NOTE: HTTPServer.server_bind calls socket.getfqdn, which stalls ~35s
    # per spawn on machines without a localhost reverse-DNS entry. The
    # server name only feeds response headers here, so bind it directly.
    def server_bind(self):
        socketserver.TCPServer.server_bind(self)
        self.server_name = self.server_address[0]
        self.server_port = self.server_address[1]


class _Handler(BaseHTTPRequestHandler):
    def _serve(self):
        total = len(PAYLOAD)

        start = 0
        end = total - 1
        status = 200

        if self.headers.get("Range"):
            spec = self.headers["Range"].strip()
            if spec.startswith("bytes="):
                try:
                    first, _, _ = spec[len("bytes=") :].partition("-")
                    start = max(0, int(first))
                except ValueError:
                    start = 0
                if start < total:
                    status = 206
                else:
                    start = total

        body = PAYLOAD[start:]

        self.send_response(status)
        self.send_header("Content-Type", "application/octet-stream")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Accept-Ranges", "bytes")
        if status == 206:
            self.send_header("Content-Range", f"bytes {start}-{end}/{total}")
        self.end_headers()

        if self.command == "HEAD":
            return

        chunk = 65536
        offset = 0
        deadline = time.monotonic()

        while offset < len(body):
            piece = body[offset : offset + chunk]
            try:
                self.wfile.write(piece)
                self.wfile.flush()
            except (BrokenPipeError, ConnectionResetError):
                return
            offset += len(piece)
            if RATE > 0:
                deadline += len(piece) / RATE
                delay = deadline - time.monotonic()
                if delay > 0:
                    time.sleep(delay)

    def do_GET(self):
        self._serve()

    def do_HEAD(self):
        self._serve()

    def log_message(self, *args):
        pass


def main(argv):
    if len(argv) != 4:
        print("usage: slow_server.py <port> <bytes> <bytes_per_sec>", file=sys.stderr)
        return 2

    port = int(argv[1])
    size = int(argv[2])

    global PAYLOAD, RATE
    PAYLOAD = bytes((i % 251 for i in range(size)))
    RATE = int(argv[3])

    server = _Server(("127.0.0.1", port), _Handler)
    print(f"PORT {server.server_port}", flush=True)
    server.serve_forever()
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
