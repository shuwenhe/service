import http.server
import socketserver
import mimetypes
import os
import re

class MyHandler(http.server.SimpleHTTPRequestHandler):
    def do_GET(self):
        filepath = self.translate_path(self.path)

        # Improved request log with clear formatting
        print("\n" + "="*50)
        print("📥 REQUEST")
        print("="*50)
        print(f"Method: {self.command}")
        print(f"Path: {self.path}")
        print("\nHeaders:")
        for key, val in sorted(self.headers.items()):
            print(f"  {key}: {val}")
        print("="*50 + "\n")

        # Helper function for response logging
        def log_response(status_code,headers):
            print("\n" + "="*50)
            print("📤 RESPONSE")
            print("="*50)
            print(f"Status: {status_code}")
            print("\nHeaders:")
            for key, val in headers.items():
                print(f"  {key}: {val}")
            print("="*50 + "\n")

        if os.path.isfile(filepath):
            try:
                content_type, _ = mimetypes.guess_type(filepath)
                if content_type is None:
                    content_type = 'application/octet-stream'

                with open(filepath, 'rb') as f:
                    fs = os.fstat(f.fileno())
                    filesize = fs.st_size

                    range_header = self.headers.get('Range')

                    if range_header:  # Handle Range requests
                        match = re.match(r'bytes=(\d*)-(\d*)', range_header)
                        if match:
                            start = int(match.group(1))
                            end = int(match.group(2)) if match.group(2) else filesize - 1

                            if start > end or start >= filesize:
                                self.send_error(416, "Requested range not satisfiable")  # Invalid range
                                return

                            length = end - start + 1
                            self.send_response(206)  # Partial Content
                            self.send_header("Content-Type", content_type)
                            self.send_header("Content-Length", str(length))
                            self.send_header("Content-Range", f"bytes {start}-{end}/{filesize}")
                            self.send_header("Accept-Ranges", "bytes")
                            self.end_headers()

                    
                            f.seek(start)
                            chunk_size = 8192
                            bytes_sent = 0
                            while bytes_sent < length:
                                chunk = f.read(min(chunk_size, length - bytes_sent))  # Don't read past the range
                                if not chunk:
                                    break
                                self.wfile.write(chunk)
                                bytes_sent += len(chunk)


                            headers = {"Content-Type": content_type, "Content-Length": str(length), "Content-Range": f"bytes {start}-{end}/{filesize}", "Accept-Ranges": "bytes"}
                            
                            log_response(206,headers)  # Log partial content response


                        else:
                            self.send_error(400, "Invalid Range header")  # Bad range format
                            return

                    else:  # Serve the entire file if no Range header
                        self.send_response(200)
                        self.send_header("Content-Type", content_type)
                        self.send_header("Content-Length", str(filesize))
                        self.send_header("Accept-Ranges", "bytes")
                        self.end_headers()
                        
                        headers = {"Content-Type": content_type, "Content-Length": str(filesize), "Accept-Ranges": "bytes"}
                        log_response(200,headers)  # Log success response
                        
                        chunk_size = 8192
                        while True:
                            chunk = f.read(chunk_size)
                            if not chunk:
                                break
                            self.wfile.write(chunk)


                return

            except Exception as e:
                self.send_error(500, f"Error serving file: {e}")
                return

        elif os.path.isdir(filepath): # Directory handling (unchanged)
            self.send_error(404, "Directories are not supported.")
            return

        self.send_error(404, "File not found.") # File not found (unchanged)


    def translate_path(self, path):
        base_path = '/videos'  # *** IMPORTANT: Change this! ***
        path = os.path.normpath(path)
        if path.startswith('/'):
            path = path[1:]
        return os.path.abspath(os.path.join(base_path, path))



PORT = 8080
Handler = MyHandler
httpd = socketserver.TCPServer(("", PORT), Handler)

print(f"Serving videos from /videos on port {PORT}") # Remind the user to change the path
print(f"Access videos at http://localhost:{PORT}/")

httpd.serve_forever()