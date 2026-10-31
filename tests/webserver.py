#!/usr/bin/env python3
"""Pages for testing !Browse on the harness: a redirect, chunked text and links.

usage: tests/webserver.py [port]
"""
import http.server
import sys

PAGE = b"""<html><head><title>Test &amp; page</title><style>p{color:red}</style></head>
<body><script>alert(1)</script><h1>It works</h1><p>Caf&eacute; caf\xc3\xa9 &#169; &mdash; dash</p>
<a href="second.html">Second page</a><ul><li>One<li>Two<ul><li>Nested</ul></ul>
<pre>  fixed   width
text</pre><table><tr><td>a<td>b<tr><td>c<td>d</table></body></html>"""
SECOND = b"<title>Second</title><p>You followed a link.</p><a href='/'>Home</a>"


class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def do_GET(self):
        if self.path == "/old":
            self.send_response(302)
            self.send_header("Location", "/test.html")
            self.send_header("Content-Length", "0")
            self.end_headers()
            return
        body = SECOND if self.path.endswith("second.html") else PAGE
        self.send_response(200)
        self.send_header("Content-Type", "text/html")
        self.send_header("Transfer-Encoding", "chunked")
        self.end_headers()
        for start in range(0, len(body), 100):
            chunk = body[start:start + 100]
            self.wfile.write(b"%x\r\n%s\r\n" % (len(chunk), chunk))
        self.wfile.write(b"0\r\n\r\n")

    def log_message(self, *args):
        print(self.path, flush=True)


http.server.ThreadingHTTPServer(("127.0.0.1", int(sys.argv[1]) if len(sys.argv) > 1 else 8123), Handler).serve_forever()
