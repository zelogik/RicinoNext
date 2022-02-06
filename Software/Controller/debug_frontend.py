import http.server
import socketserver
import os

PORT = 8080
Handler = http.server.SimpleHTTPRequestHandler

data_dir = os.path.join(os.path.dirname(__file__), 'data')
os.chdir(data_dir)

with socketserver.TCPServer(("", PORT), Handler) as httpd:
    print("serving at port", PORT)
    httpd.serve_forever()
