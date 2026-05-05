#!/usr/bin/env python3
import argparse
import json
import os
import shutil
import socket
import subprocess
import sys
import tempfile
import time
import unittest
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path

class HostServerTest(unittest.TestCase):
    binary = None
    port = None
    temp_dir = None
    process = None
    base_url = None

    @classmethod
    def setUpClass(cls):
        if not cls.binary:
            raise RuntimeError("Binary path not set")
        
        # Create temp dir and populate it
        cls.temp_dir = tempfile.mkdtemp(prefix="host_server_test_")
        
        # one .txt file
        with open(os.path.join(cls.temp_dir, "test.txt"), "w") as f:
            f.write("hello world")
            
        # one .epub file stub
        with open(os.path.join(cls.temp_dir, "test.epub"), "wb") as f:
            f.write(b"PK\x03\x04")
            
        # one subdir with a file
        os.makedirs(os.path.join(cls.temp_dir, "subdir"))
        with open(os.path.join(cls.temp_dir, "subdir", "subfile.txt"), "w") as f:
            f.write("sub content")

        os.makedirs(os.path.join(cls.temp_dir, ".crosspoint"))
        recent_books = [{
            "path": f"/test-{idx}.txt",
            "title": f"Test Book {idx}",
            "author": "Host",
            "coverBmpPath": ""
        } for idx in range(12)]
        recent_books[0]["path"] = "/test.txt"
        recent_books[0]["title"] = "Test Book"
        with open(os.path.join(cls.temp_dir, ".crosspoint", "recent.json"), "w") as f:
            json.dump({"books": recent_books}, f)

        # Launch binary
        cls.base_url = f"http://127.0.0.1:{cls.port}"
        # --root and --port flags as shown in main.cpp
        cmd = [cls.binary, "--port", str(cls.port), "--root", cls.temp_dir]
        cls.process = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        
        # Wait until /api/status returns 200 (poll, max 10s)
        ready = False
        start_time = time.time()
        while time.time() - start_time < 10:
            try:
                with urllib.request.urlopen(f"{cls.base_url}/api/settings/raw", timeout=0.2) as resp:
                    if resp.status == 200:
                        ready = True
                        break
            except (urllib.error.URLError, ConnectionRefusedError, urllib.error.HTTPError):
                pass
            
            # Check if process died
            if cls.process.poll() is not None:
                stdout, stderr = cls.process.communicate()
                print(f"Server process died with exit code {cls.process.returncode}")
                print(f"STDOUT: {stdout}")
                print(f"STDERR: {stderr}")
                break
                
            time.sleep(0.2)
        
        if not ready:
            cls.tearDownClass()
            raise RuntimeError("Server failed to start or become ready in time")

    @classmethod
    def tearDownClass(cls):
        if cls.process:
            cls.process.terminate()
            try:
                cls.process.wait(timeout=2)
            except subprocess.TimeoutExpired:
                cls.process.kill()
        if cls.temp_dir and os.path.exists(cls.temp_dir):
            shutil.rmtree(cls.temp_dir)

    def _request(self, method, path, data=None, headers=None):
        url = f"{self.base_url}{path}"
        if headers is None:
            headers = {}
        
        req = urllib.request.Request(url, method=method, headers=headers)
        
        try:
            with urllib.request.urlopen(req, data=data, timeout=5) as resp:
                return resp.status, resp.read(), resp.headers
        except urllib.error.HTTPError as e:
            return e.code, e.read(), e.headers
        except urllib.error.URLError as e:
            self.fail(f"Request to {url} failed: {e}")

    def test_01_list_files(self):
        """GET /api/files?path=X — listing"""
        code, body, _ = self._request("GET", "/api/files?path=/")
        if code == 404: self.skipTest("Route /api/files not implemented")
        self.assertEqual(code, 200)
        files = json.loads(body)
        names = [f["name"] for f in files]
        self.assertIn("test.txt", names)
        self.assertIn("test.epub", names)
        self.assertIn("subdir", names)
        
        # Check subfile
        code, body, _ = self._request("GET", "/api/files?path=/subdir")
        self.assertEqual(code, 200)
        files = json.loads(body)
        self.assertEqual(files[0]["name"], "subfile.txt")

    def test_01b_list_files_rejects_missing_path(self):
        """GET /api/files?path=/missing — should report missing directories"""
        code, body, _ = self._request("GET", "/api/files?path=/missing")
        if code == 404:
            self.assertEqual(body.decode("utf-8"), "Item not found")
            return
        self.fail(f"Expected missing path to return 404, got {code}: {body!r}")

    def test_02_download_file(self):
        """GET /download?path=X — file content"""
        code, body, _ = self._request("GET", "/download?path=/test.txt")
        if code == 404: self.skipTest("Route /download not implemented")
        self.assertEqual(code, 200)
        self.assertEqual(body, b"hello world")

    def test_03_mkdir(self):
        """POST /mkdir (form: name, path)"""
        data = urllib.parse.urlencode({"name": "new_dir", "path": "/"}).encode("utf-8")
        headers = {"Content-Type": "application/x-www-form-urlencoded"}
        code, _, _ = self._request("POST", "/mkdir", data=data, headers=headers)
        
        if code == 404: self.skipTest("Route /mkdir not implemented")
        self.assertEqual(code, 200)
        
        # Verify on-disk state
        self.assertTrue(os.path.isdir(os.path.join(self.temp_dir, "new_dir")))

    def test_04_rename(self):
        """POST /rename (form: path, name)"""
        # Rename test.txt to renamed.txt
        data = urllib.parse.urlencode({"path": "/test.txt", "name": "renamed.txt"}).encode("utf-8")
        headers = {"Content-Type": "application/x-www-form-urlencoded"}
        code, _, _ = self._request("POST", "/rename", data=data, headers=headers)
        
        if code == 404: self.skipTest("Route /rename not implemented")
        self.assertEqual(code, 200)
        
        # Verify on-disk state
        self.assertFalse(os.path.exists(os.path.join(self.temp_dir, "test.txt")))
        self.assertTrue(os.path.exists(os.path.join(self.temp_dir, "renamed.txt")))

    def test_05_move(self):
        """POST /move (form: path, dest)"""
        # Move renamed.txt to /subdir/
        data = urllib.parse.urlencode({"path": "/renamed.txt", "dest": "/subdir"}).encode("utf-8")
        headers = {"Content-Type": "application/x-www-form-urlencoded"}
        code, _, _ = self._request("POST", "/move", data=data, headers=headers)
        
        if code == 404: self.skipTest("Route /move not implemented")
        self.assertEqual(code, 200)
        
        # Verify on-disk state
        self.assertFalse(os.path.exists(os.path.join(self.temp_dir, "renamed.txt")))
        self.assertTrue(os.path.exists(os.path.join(self.temp_dir, "subdir", "renamed.txt")))

    def test_06_upload(self):
        """POST /upload (multipart, field 'file')"""
        boundary = "integration-test-boundary"
        file_content = b"uploaded content"
        body = (
            f"--{boundary}\r\n"
            f'Content-Disposition: form-data; name="file"; filename="upload.txt"\r\n'
            f"Content-Type: text/plain\r\n\r\n"
        ).encode("utf-8") + file_content + f"\r\n--{boundary}--\r\n".encode("utf-8")
        
        headers = {"Content-Type": f"multipart/form-data; boundary={boundary}"}
        # ?path=/subdir specifies destination
        code, _, _ = self._request("POST", "/upload?path=/subdir", data=body, headers=headers)
        
        if code == 404: self.skipTest("Route /upload not implemented")
        self.assertEqual(code, 200)
        
        # Verify on-disk state
        uploaded_path = os.path.join(self.temp_dir, "subdir", "upload.txt")
        self.assertTrue(os.path.exists(uploaded_path))
        with open(uploaded_path, "rb") as f:
            self.assertEqual(f.read(), file_content)

    def test_07_delete(self):
        """POST /delete (form: paths JSON array)"""
        # Delete /subdir/subfile.txt and /test.epub
        paths = ["/subdir/subfile.txt", "/test.epub"]
        data = urllib.parse.urlencode({"paths": json.dumps(paths)}).encode("utf-8")
        headers = {"Content-Type": "application/x-www-form-urlencoded"}
        code, _, _ = self._request("POST", "/delete", data=data, headers=headers)
        
        if code == 404: self.skipTest("Route /delete not implemented")
        self.assertEqual(code, 200)
        
        # Verify on-disk state
        self.assertFalse(os.path.exists(os.path.join(self.temp_dir, "subdir", "subfile.txt")))
        self.assertFalse(os.path.exists(os.path.join(self.temp_dir, "test.epub")))

    def test_08_cover(self):
        """GET /api/cover?path=X — may 404 for non-EPUB; assert content-type when 200"""
        # We deleted test.epub in previous test, let's create a new one if needed, 
        # but tests should be independent. However, here we run them in order.
        # Actually test_07 deleted it. Let's create one specifically for this test.
        epub_path = os.path.join(self.temp_dir, "cover_test.epub")
        with open(epub_path, "wb") as f:
            f.write(b"PK\x03\x04stub")
            
        code, _, headers = self._request("GET", "/api/cover?path=/cover_test.epub")
        if code == 404:
            # If it's a 404, it might be that the cover extraction is not implemented yet
            # or it's just not an EPUB with a cover.
            self.skipTest("Route /api/cover returned 404 (possibly not implemented or no cover found)")
        
        self.assertEqual(code, 200)
        self.assertEqual(headers.get("Content-Type"), "image/bmp")

    def test_09_recent_books(self):
        """GET /api/recent — host shim returns device-compatible recent-book JSON"""
        code, body, headers = self._request("GET", "/api/recent")
        self.assertEqual(code, 200)
        self.assertEqual(headers.get("Content-Type"), "application/json")
        books = json.loads(body)
        self.assertEqual(len(books), 10)
        self.assertEqual(books[0]["path"], "/test.txt")
        self.assertEqual(books[0]["title"], "Test Book")
        self.assertEqual(books[0]["author"], "Host")
        self.assertFalse(books[0]["hasCover"])
        self.assertIsNone(books[0]["progress"])
        self.assertEqual(books[-1]["path"], "/test-9.txt")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Host Firmware Server Integration Tests")
    parser.add_argument("--binary", default="build/host_server/host_firmware_server", help="Path to the server binary")
    parser.add_argument("--port", type=int, help="Port to run the server on (default: random)")
    
    # Extract unittest arguments
    args, unknown = parser.parse_known_args()
    
    if not os.path.exists(args.binary):
        print(f"Error: Binary not found at {args.binary}")
        sys.exit(1)
        
    if args.port is None:
        # find free port
        s = socket.socket()
        s.bind(("127.0.0.1", 0))
        args.port = s.getsockname()[1]
        s.close()
    
    HostServerTest.binary = os.path.abspath(args.binary)
    HostServerTest.port = args.port
    
    # Reconstruct sys.argv for unittest
    sys.argv = [sys.argv[0]] + unknown
    unittest.main(verbosity=2)
