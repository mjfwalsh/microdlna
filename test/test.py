#!/usr/bin/env python3

import os
import unittest
import re
import signal
import socket
import subprocess
import sys
import threading
from xml.etree.ElementTree import XML

from utils.http import http_client, one_shot_server

# add lldb module to path
sys.path.append(subprocess.check_output(["lldb", "-P"], encoding="utf8").strip())

# and now import
import lldb

# cd to test dir
os.chdir(os.path.dirname(__file__))

# global vars
db = None
server = None
debugger_ready = threading.Event()
register = set()
chunked = None
port = 0


def run_debugger():
    global chunked

    lldb.SBDebugger.Initialize()

    debugger = lldb.SBDebugger.Create()
    debugger.SetAsync(False)

    target = debugger.CreateTarget("")
    error = lldb.SBError()
    listener = lldb.SBListener()
    process = target.AttachToProcessWithID(listener, server.pid, error)
    thread = process.GetThreadAtIndex(0)

    # exit the debugger when the server exists
    unix_signals = process.GetUnixSignals()
    for sig in [signal.SIGINT, signal.SIGTERM, signal.SIGHUP]:
        unix_signals.SetShouldSuppress(sig, False)
        unix_signals.SetShouldStop(sig, False)
        unix_signals.SetShouldNotify(sig, False)

    # set some breakpoints
    target.BreakpointCreateByName("process_name_value_pair")
    target.BreakpointCreateByName("process_post_content")

    debugger_ready.set()
    try:
        while frame := thread.GetFrameAtIndex(0):
            f = frame.GetFunctionName()
            if f == "process_name_value_pair":
                n = frame.variables.GetFirstValueByName("name").GetSummary()
                v = frame.variables.GetFirstValueByName("value").GetSummary()
                register.add(f"{n}={v}")

            elif f == "process_post_content":
                dl = (
                    frame.variables.GetFirstValueByName("h")
                    .GetChildMemberWithName("data_len")
                    .GetValueAsSigned(-1)
                )
                if dl is None or dl == -1:
                    chunked = None
                else:
                    chunked = dl == 0

            process.Continue()

    finally:
        lldb.SBDebugger.Destroy(debugger)
        lldb.SBDebugger.Terminate()


def xml_request(data=None, *, host=None, action="Browse"):
    headers = {
        "CONTENT-TYPE": 'text/xml; charset="utf-8"',
        "SOAPACTION": f'"urn:schemas-upnp-org:service:ContentDirectory:1#{action}"',
    }

    if host is not None:
        headers["HOST"] = host
    else:
        headers["HOST"] = f"127.0.0.1:{port}"

    if data is not None:
        data_bytes = data.encode()
        headers["CONTENT-LENGTH"] = "%d" % len(data_bytes)
    else:
        headers["TRANSFER-ENCODING"] = "chunked"

    s = http_client.send_request_head(
        domain="127.0.0.1",
        port=port,
        method="POST",
        path="/",
        headers=headers,
    )
    if data is not None:
        s.send_bytes(data_bytes)

    return s


def request(*, method, path):
    headers = {
        "HOST": f"127.0.0.1:{port}",
    }
    s = http_client.send_request_head(
        domain="127.0.0.1",
        port=port,
        method=method,
        path=path,
        headers=headers,
    )
    return s.read_http_response()


def setUpModule():
    global server, db, port

    # start the server
    server = subprocess.Popen(
        ["../microdlnad", "-D", "./root", "-di", "lo0", "-p", f"{port}"],
        stdin=subprocess.DEVNULL,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        encoding="utf-8",
    )

    # start the debugger
    db = threading.Thread(target=run_debugger).start()

    # wait until the server is ready to accept connections
    assert "Starting MicroDLNA version" in server.stdout.readline()
    assert (
        m := re.search(r"HTTP listening on port ([0-9]+)", server.stdout.readline())
    )
    assert "Enabling interface" in server.stdout.readline()
    os.set_blocking(server.stdout.fileno(), False)

    # save port
    port = int(m[1])

    # and wait for the debugger
    assert debugger_ready.wait(10), "Debugger started"


def tearDownModule():
    if server is not None:
        os.kill(server.pid, signal.SIGTERM)

    if db is not None:
        db.join()


class Test(unittest.TestCase):
    def setUp(self):
        global chunked
        register.clear()
        chunked = None

        # clear server log
        try:
            server.stdout.read()
        except BlockingIOError:
            pass

    def test_send_valid_request(self):
        s = xml_request()
        s.send_chunk(
            '<s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/"'
            ' s:encodingStyle="http://schemas.xmlsoap.org/soap/encoding/">'
        )
        s.send_chunk(
            '<s:Body><u:Browse xmlns:u="urn:schemas-upnp-org:service:'
            'ContentDirectory:1">'
        )

        # assemble valid and invalid name/value pairs to send to server
        name_value_pairs = []
        name_value_pairs.append([True, "ObjectID", "0"])
        name_value_pairs.append([True, "BrowseFlag", "BrowseDirectChildren"])
        name_value_pairs.append([False, "", "val"])

        for i in range(1, 20):
            tag = "q" * i
            name_value_pairs.append([True, tag, "val"])

        for i in range(20, 25):
            tag = "q" * i
            name_value_pairs.append([False, tag, "val"])

        name_value_pairs.append([True, "Body", ""])
        name_value_pairs.append([True, "Filter", "*"])
        name_value_pairs.append([True, "StartingIndex", "0"])
        name_value_pairs.append([True, "RequestedCount", "5"])
        name_value_pairs.append([True, "SortCriteria", ""])

        for _, tag, value in name_value_pairs:
            s.send_chunk("<%s>%s</%s>" % (tag, value, tag))

        s.send_chunk("<InvalidTag>10</InvalidTagX>")
        s.send_chunk("<AnotherInvalidTag>10</AnotherInvalidTa>")
        s.send_chunk("</u:Browse></s:Body></s:Envelope>")
        s.send_closing_chunk()
        r = s.read_http_response()

        self.assertEqual(r.status_code, 200)
        self.assertEqual(chunked, True)

        xml_data = r.read_body()

        ns = {
            "s": "http://schemas.xmlsoap.org/soap/envelope/",
            "u": "urn:schemas-upnp-org:service:ContentDirectory:1",
            "dc": "http://purl.org/dc/elements/1.1/",
        }

        root = XML(xml_data)

        results = {}
        for x in root.find("s:Body", ns).find("u:BrowseResponse", ns):
            results[x.tag] = x.text

        inner_root = XML(results["Result"])
        files = [child.attrib["id"] for child in inner_root]

        self.assertEqual(results["NumberReturned"], "5")
        self.assertEqual(results["TotalMatches"], "30")
        self.assertEqual(results["UpdateID"], "0")

        self.assertEqual(len(files), 5)

        for i in range(5):
            self.assertEqual(files[i], f"/{i+1:02d}.mkv")

        # check for existence of all valid name/value pairs
        for valid, tag, value in name_value_pairs:
            if valid:
                key = f'"{tag}"="{value}"'
                register.remove(key)

        # after we remove the valid entries there should be none left
        self.assertEqual(len(register), 0)

    def test_valid_unchunked(self):
        s = xml_request("<ObjectID>0</ObjectID>")
        r = s.read_http_response()
        r.close()
        self.assertEqual(r.status_code, 200)
        self.assertEqual(chunked, False)

    def test_large_valid_request(self):
        s = xml_request()
        for i in range(3):
            s.send_chunk("x" * 512)

        s.send_chunk("<ObjectID>0</ObjectID>")
        s.send_closing_chunk()
        r = s.read_http_response()
        r.close()

        self.assertEqual(r.status_code, 200)
        self.assertEqual(chunked, True)

    def test_too_large_invalid_request(self):
        s = xml_request()
        for i in range(4):
            s.send_chunk("x" * 512)

        s.send_chunk("<ObjectID>0</ObjectID>")
        s.send_closing_chunk()
        r = s.read_http_response()
        r.close()

        self.assertEqual(r.status_code, 400)
        self.assertEqual(chunked, True)

    def test_too_large_tag_value(self):
        s = xml_request("<X>" + "x" * 1024 + "</X>")
        s.read_http_response()
        s.close()
        self.assertEqual(len(register), 0)
        self.assertEqual(chunked, False)

    def test_large_but_valid_tag_value(self):
        value = "x" * 1023
        s = xml_request(f"<X>{value}</X>")
        s.read_http_response()
        s.close()
        self.assertEqual(len(register), 1)
        self.assertIn(f'"X"="{value}"', register)
        self.assertEqual(chunked, False)

    def test_invalid_request(self):
        # should reject input with a null char
        s = xml_request()

        s.send_chunk("\x00")
        s.send_chunk("<ObjectID>0</ObjectID>")
        s.send_closing_chunk()
        r = s.read_http_response()
        r.close()

        self.assertEqual(r.status_code, 400)
        self.assertEqual(chunked, True)

    def test_rebinding_attack(self):
        s = xml_request(
            "<ObjectID>0</ObjectID>", host=f"127.0.0.1:{port}.evil-rebinding-attack.com"
        )
        r = s.read_http_response()
        r.close()
        self.assertEqual(r.status_code, 400)
        self.assertIn("Missing or invalid host header", server.stdout.read())

    def test_invalid_host_two(self):
        s = xml_request("<ObjectID>0</ObjectID>", host="")
        r = s.read_http_response()
        r.close()
        self.assertEqual(r.status_code, 400)

    def test_unsupported_action(self):
        s = xml_request("<ObjectID>0</ObjectID>", action="Search")
        r = s.read_http_response()
        r.close()
        self.assertEqual(r.status_code, 708)

    def test_supported_action(self):
        s = xml_request("", action="GetProtocolInfo")
        r = s.read_http_response()
        self.assertEqual(r.status_code, 200)
        self.assertIn("GetProtocolInfoResponse", r.read_body())

    def test_get_request(self):
        r = request(method="GET", path="/MediaItems/11.mkv")
        self.assertEqual(r.status_code, 200)
        self.assertIn("dummy", r.read_body())
        self.assertEqual(r.headers["Content-Type"], "video/x-matroska")

    def test_prohibited_get_request(self):
        r = request(method="GET", path="/MediaItems/01")
        self.assertEqual(r.status_code, 406)
        r.close()

    def test_prohibited_head_request(self):
        r = request(method="HEAD", path="/MediaItems/01")
        self.assertEqual(r.status_code, 406)
        r.close()

    def test_not_found_request(self):
        r = request(method="GET", path="/MediaItems/missing")
        self.assertEqual(r.status_code, 404)
        r.close()

    def test_udp_socket(self):
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.settimeout(0.3)

        s.sendto(
            b"M-SEARCH * HTTP/1.1\r\n"
            b'MAN: "ssdp:discover"\r\n'
            b"MX: 1\r\n"
            b"ST: urn:schemas-upnp-org:device:MediaServer:\r\n\r\n",
            ("127.0.0.1", 1900),
        )

        data, address = s.recvfrom(1024)
        s.close()

        lines = data.split(b"\r\n")

        self.assertIn(b"HTTP/1.1 200 OK", lines)
        self.assertIn((b"LOCATION: http://127.0.0.1:%d/rootDesc.xml" % port), lines)

    def test_udp_socket_missing_header(self):
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.settimeout(0.3)

        s.sendto(  # missing MX header
            b"M-SEARCH * HTTP/1.1\r\n"
            b'MAN: "ssdp:discover"\r\n'
            b"ST: urn:schemas-upnp-org:device:MediaServer:\r\n\r\n",
            ("127.0.0.1", 1900),
        )

        with self.assertRaises(TimeoutError):
            s.recvfrom(1024)

        s.close()

    def test_invalid_chunk_header_one(self):
        s = http_client.send_request_head(
            domain="127.0.0.1",
            port=port,
            method="POST",
            path="/",
            headers={
                "HOST": f"127.0.0.1:{port}",
                "CONTENT-TYPE": 'text/xml; charset="utf-8"',
                "TRANSFER-ENCODING": "chunked",
                "SOAPACTION": '"urn:schemas-upnp-org:service:ContentDirectory:1#Browse"',
            },
        )

        s.send_bytes(
            b"g\r\n"  # invalid hex number
            b"<ObjectID>0</ObjectID>\r\n"
            b"0\r\n\r\n"
        )

        r = s.read_http_response()
        self.assertEqual(r.status_code, 400)
        s.close()

    def test_invalid_chunk_header_two(self):
        s = http_client.send_request_head(
            domain="127.0.0.1",
            port=port,
            method="POST",
            path="/",
            headers={
                "HOST": f"127.0.0.1:{port}",
                "CONTENT-TYPE": 'text/xml; charset="utf-8"',
                "TRANSFER-ENCODING": "chunked",
                "SOAPACTION": '"urn:schemas-upnp-org:service:ContentDirectory:1#Browse"',
            },
        )

        # chunk with invalid size
        s.send_bytes(
            b"-16\r\n"  # negative number
            b"<ObjectID>0</ObjectID>\r\n"
            b"0\r\n\r\n"
        )

        r = s.read_http_response()
        self.assertEqual(r.status_code, 400)
        s.close()

    def test_malformed_content_length(self):
        s = http_client.send_request_head(
            domain="127.0.0.1",
            port=port,
            method="POST",
            path="/",
            headers={
                "HOST": f"127.0.0.1:{port}",
                "CONTENT-TYPE": 'text/xml; charset="utf-8"',
                "CONTENT-LENGTH": "-22",
                "SOAPACTION": '"urn:schemas-upnp-org:service:ContentDirectory:1#Browse"',
            },
        )
        s.send("<ObjectID>0</ObjectID>")

        r = s.read_http_response()
        self.assertEqual(r.status_code, 400)
        s.close()

    def test_subscribe(self):
        callback_server = one_shot_server()

        s = http_client.send_request_head(
            domain="127.0.0.1",
            port=port,
            method="SUBSCRIBE",
            path="/evt/ContentDir",
            headers={
                "HOST": f"127.0.0.1:{port}",
                "Callback": callback_server.get_address(),
                "NT": "upnp:event",
            },
        )
        r = s.read_http_response()
        r.close()
        self.assertEqual(r.status_code, 200)

        callback = callback_server.close()
        self.assertIsNotNone(callback)
        self.assertEqual(callback.method, "NOTIFY")
        self.assertEqual(callback.path, "/")
        self.assertIn("debug: upnp_event_recv: (0bytes)", server.stdout.read())

    def test_subscribe_bad_host(self):
        s = http_client.send_request_head(
            domain="127.0.0.1",
            port=port,
            method="SUBSCRIBE",
            path="/evt/ContentDir",
            headers={
                "HOST": f"127.0.0.1:{port}.evil-rebinding-attack.com",
                "Callback": "http://invalid/",
                "NT": "upnp:event",
            },
        )
        r = s.read_http_response()
        r.close()
        self.assertEqual(r.status_code, 400)
        self.assertIn("Missing or invalid host header", server.stdout.read())


if __name__ == "__main__":
    unittest.main()
