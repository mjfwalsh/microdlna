#!/usr/bin/env python3

import re
import socket
from socketserver import TCPServer
from threading import Thread


class abstract_http_message:
    def __init__(self, s):
        self.length = -1
        self.chunked = False
        self.headers = {}
        self.s = s

        # first line
        line = self.readline()
        self.parse_first_line(line)

        # read headers
        while True:
            line = self.readline()

            if line == b"":
                break
            elif m := re.search(rb"^CONTENT-LENGTH: ([0-9]+)$", line, re.I):
                self.length = int(m[1])
            elif re.search(rb"^TRANSFER-ENCODING: chunked$", line, re.I):
                self.chunked = True
            else:
                name, _, value = line.partition(b": ")
                self.headers[name.decode()] = value.decode()

        if self.chunked and self.length != -1:
            raise AssertionError("Can't have content-length and chunked")

    def read_body(self):
        if self.length > 0:
            body = self.s.recv(self.length).decode()
        elif self.chunked:
            chunks = []
            while True:
                line = self.readline()
                size = int(line, 16)
                chunks.append(self.s.recv(size))
                assert self.s.recv(2) == b"\r\n"
                if size == 0:
                    break
            body = b"".join(chunks).decode()
        else:
            body = None

        self.s.close()
        return body

    def close(self):
        self.s.close()

    def readline(self):
        line = []
        while True:
            c = self.s.recv(1)
            if c == b"\r":
                c = self.s.recv(1)
                assert c == b"\n"
                return b"".join(line)
            else:
                line.append(c)


class read_http_response(abstract_http_message):
    def parse_first_line(self, line):
        if m := re.search(rb"^HTTP/1\.1 ([0-9]+) (.*)$", line, re.I):
            self.status_code = int(m[1])
            self.message = m[2].decode()
        else:
            raise AssertionError("Malformed response line")


class read_http_request(abstract_http_message):
    def parse_first_line(self, line):
        if m := re.search(rb"^([A-Z\-]+) ([^ ]+) HTTP/1\.1$", line, re.I):
            self.method = m[1].decode()
            self.path = m[2].decode()
        else:
            raise AssertionError("Malformed request line")


class one_shot_server:
    def __init__(self):
        self.server = TCPServer(("127.0.0.1", 0), self.handler)
        self.server.timeout = 0.3
        self.thread = Thread(target=self.server.handle_request)
        self.thread.start()

    def handler(self, s, *_):
        self.req = read_http_request(s)
        s.send(b"HTTP/1.1 200 OK\r\n")
        s.send(b"Host: localhost\r\n\r\n")

    def get_address(self):
        domain, port = self.server.socket.getsockname()
        return f"http://{domain}:{port}"

    def close(self):
        self.thread.join()
        return self.req


class http_client:
    def __init__(self, domain, port):
        self.s = socket.socket()
        self.s.settimeout(0.3)
        self.s.connect((domain, port))

    @staticmethod
    def send_request_head(*, domain, port, method, path, headers):
        s = http_client(domain, port)
        s.send(f"{method} {path} HTTP/1.1\r\n")
        for name, value in headers.items():
            s.send(f"{name}: {value}\r\n")
        s.send("\r\n")
        return s

    # send raw bytes
    def send_bytes(self, bits):
        self.s.send(bits)

    # send a string
    def send(self, string):
        self.s.send(string.encode())

    def send_chunk(self, b):
        bs = b.encode()
        self.send_bytes(b"%x\r\n" % len(bs))
        self.send_bytes(bs)
        self.send_bytes(b"\r\n")

    def send_closing_chunk(self):
        self.send_bytes(b"0\r\n\r\n")

    def recv(self, n):
        return self.s.recv(n)

    def read_http_response(self):
        return read_http_response(self)

    def close(self):
        self.s.close()
