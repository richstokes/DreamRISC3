#!/usr/bin/env python3
"""Pages for testing !Web on the harness: a redirect, chunked text and links.

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


BIG = b"<title>Big</title><head><script>" + b"var x='<p>not this</p>';" * 14000 + b"</script></head><h1>After the script</h1><p>Visible text.</p>"
LONG = b"<title>Long</title>" + b"".join(b"<p>Paragraph number %d of a long page.</p>" % i for i in range(40000))
FORM = b"""<title>Form</title><form action=/echo><input type=hidden name=h value=1><input name=q value=old>
<input type=submit name=go value=Find></form><form method=post action=/echo><textarea name=t>x</textarea>
<button type=submit>Send it</button></form>"""


class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def do_GET(self):
        if self.path == "/old":
            self.send_response(302)
            self.send_header("Location", "/test.html")
            self.send_header("Content-Length", "0")
            self.end_headers()
            return
        pages = {"/big": BIG, "/long": LONG, "/form": FORM}
        if self.path.startswith("/echo"):
            body = b"<title>Echo</title><p>got " + self.path.encode() + b"</p>"
        else:
            body = pages.get(self.path, SECOND if self.path.endswith("second.html") else PAGE)
        self.send_response(200)
        self.send_header("Content-Type", "text/html")
        self.send_header("Transfer-Encoding", "chunked")
        self.end_headers()
        for start in range(0, len(body), 100):
            chunk = body[start:start + 100]
            self.wfile.write(b"%x\r\n%s\r\n" % (len(chunk), chunk))
        self.wfile.write(b"0\r\n\r\n")

    def do_POST(self):
        data = self.rfile.read(int(self.headers["Content-Length"]))
        print("POST", data, flush=True)
        self.path = "/echo?" + data.decode()
        self.do_GET()

    def log_message(self, *args):
        print(self.path, flush=True)


http.server.ThreadingHTTPServer(("127.0.0.1", int(sys.argv[1]) if len(sys.argv) > 1 else 8123), Handler).serve_forever()
