#!/usr/bin/env python3
"""
Prosty serwer WWW do podglądu symulatora interfejsu SP9PDF Antenna Switch.

Uruchomienie:
    python serve.py                    # HTTP 8765 (lub pierwszy wolny wyżej), OTRSP-TCP 4534
    python serve.py 8080                # wskazany port HTTP
    python serve.py 8080 5000           # port HTTP i port OTRSP-TCP

Skrypt serwuje katalog, w którym się znajduje (tools/), otwiera websim.html
w domyślnej przeglądarce i działa aż do Ctrl+C. Nasłuchuje tylko na 127.0.0.1
(lokalnie). Aby udostępnić w sieci LAN, zmień HOST na "0.0.0.0".

Dodatkowo uruchamia most OTRSP-TCP<->WebSocket (SQ9FK): prawdziwy program typu
N1MM+ może połączyć się surowym TCP na porcie OTRSP_TCP_PORT (domyślnie 4534,
jak w src/main.ino) i "rozmawiać" z symulatorem w przeglądarce (websim.html),
dokładnie jak z prawdziwym urządzeniem w wariancie OTRSP_TCP. Most jest
"głupą rurką" bajtów - cała logika protokołu (OTRSP_parse w JS) siedzi po
stronie przeglądarki, żeby nie duplikować jej w Pythonie.
"""
import base64
import hashlib
import http.server
import json
import os
import socket
import socketserver
import struct
import sys
import threading
import webbrowser

HOST = "127.0.0.1"
DEFAULT_PORT = 8765
DEFAULT_OTRSP_TCP_PORT = 4534
PAGE = "websim.html"
WS_PATH = "/otrsp-ws"
WS_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

# SQ9FK: stan anteny wybranej dla TRX1/TRX2 (0=OFF, 1..6), sluzy wylacznie do symulacji
# /?J i /?S{bank}{kod} dla testu integracji z rotator_wifi_bridge - patrz make_handler().
antenna_state = [0, 0]
flex_state = [0, 0]  # Radio Flex / "PWR" output per TRX (see /?F below)
# SQ9FK: nazwy anten 1..6 dla symulacji /?K (patrz make_handler) - domyslne jak antDefault[]
# w src/main.ino.
antenna_names = ["ANT1", "ANT2", "ANT3", "ANT4", "ANT5", "ANT6"]
# SQ9FK: nazwa stacji (siteName() w src/main.ino) - 7-me pole w /?K, po nazwach anten.
site_name = "SQ9FK"


class WSConnection:
    """Minimalny serwer WebSocket (RFC 6455) - tylko ramki tekstowe, bez fragmentacji.
    SQ9FK: wystarcza do prostego mostu bajtow tekstowych miedzy TCP (N1MM+) a przegladarka
    (websim.html) - nie potrzeba pelnej biblioteki WS dla tego jednego, prostego kanalu."""

    def __init__(self, sock, rfile):
        self.sock = sock
        self.rfile = rfile   # SQ9FK: ten sam bufor co naglowki HTTP - uniknij podwojnego buforowania

    def send_text(self, text):
        payload = text.encode("utf-8")
        length = len(payload)
        if length < 126:
            header = struct.pack("!BB", 0x81, length)
        elif length < 65536:
            header = struct.pack("!BBH", 0x81, 126, length)
        else:
            header = struct.pack("!BBQ", 0x81, 127, length)
        self.sock.sendall(header + payload)

    def recv_text(self):
        """Zwraca tekst jednej ramki, albo None przy zamknieciu/bledzie polaczenia."""
        hdr = self._recv_exact(2)
        if not hdr:
            return None
        b0, b1 = hdr[0], hdr[1]
        opcode = b0 & 0x0F
        masked = (b1 & 0x80) != 0
        length = b1 & 0x7F
        if length == 126:
            ext = self._recv_exact(2)
            if ext is None:
                return None
            length = struct.unpack("!H", ext)[0]
        elif length == 127:
            ext = self._recv_exact(8)
            if ext is None:
                return None
            length = struct.unpack("!Q", ext)[0]
        mask_key = self._recv_exact(4) if masked else None
        payload = self._recv_exact(length) if length else b""
        if payload is None:
            return None
        if masked and payload:
            payload = bytes(b ^ mask_key[i % 4] for i, b in enumerate(payload))
        if opcode == 0x8:          # close
            return None
        if opcode == 0x9:          # ping -> pong, potem czekaj na kolejna ramke
            self._send_pong(payload)
            return self.recv_text()
        if opcode == 0xA:          # pong (nieoczekiwany, ignoruj)
            return self.recv_text()
        return payload.decode("utf-8", "replace")

    def _send_pong(self, payload):
        header = struct.pack("!BB", 0x8A, len(payload))
        self.sock.sendall(header + payload)

    def _recv_exact(self, n):
        buf = b""
        while len(buf) < n:
            chunk = self.rfile.read(n - len(buf))
            if not chunk:
                return None
            buf += chunk
        return buf


def websocket_handshake(handler):
    """Jesli zadanie to WebSocket upgrade, wykonuje handshake i zwraca WSConnection
    (przejmujac surowe polaczenie handlera), inaczej None."""
    if handler.headers.get("Upgrade", "").lower() != "websocket":
        return None
    key = handler.headers.get("Sec-WebSocket-Key")
    if not key:
        return None
    accept = base64.b64encode(hashlib.sha1((key + WS_GUID).encode()).digest()).decode()
    resp = (
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        f"Sec-WebSocket-Accept: {accept}\r\n\r\n"
    )
    handler.connection.sendall(resp.encode())
    return WSConnection(handler.connection, handler.rfile)


class OtrspBridge:
    """SQ9FK: mostkuje surowe polaczenie TCP (np. N1MM+, port jak OTRSP_TCP_PORT w firmware)
    z WebSocketem przegladarki. Jeden aktywny klient TCP naraz - kolejny zastepuje biezacy,
    tak jak w firmware (patrz otrspClient w loop() blok "OTRSP TCP")."""

    def __init__(self, host, port):
        self.host = host
        self.port = port
        self.ws = None
        self.ws_lock = threading.Lock()
        self.tcp_conn = None
        self.tcp_lock = threading.Lock()

    def start(self):
        threading.Thread(target=self._accept_loop, daemon=True).start()

    def _accept_loop(self):
        srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        try:
            srv.bind((self.host, self.port))
        except OSError as e:
            print(f"  UWAGA: nie można nasłuchiwać na porcie OTRSP-TCP {self.port}: {e}")
            return
        srv.listen(1)
        print(f"  Most OTRSP-TCP: nasłuchuje na {self.host}:{self.port} (dla N1MM+ itp.)")
        while True:
            conn, addr = srv.accept()
            with self.tcp_lock:
                if self.tcp_conn is not None:
                    try:
                        self.tcp_conn.close()          # nowe polaczenie zastepuje biezace
                    except OSError:
                        pass
                self.tcp_conn = conn
            self._notify_ws({"type": "connected", "peer": f"{addr[0]}:{addr[1]}"})
            threading.Thread(target=self._read_loop, args=(conn,), daemon=True).start()

    def _read_loop(self, conn):
        try:
            while True:
                data = conn.recv(256)
                if not data:
                    break
                self._notify_ws({"type": "data", "data": data.decode("latin-1")})
        except OSError:
            pass
        finally:
            with self.tcp_lock:
                if self.tcp_conn is conn:
                    self.tcp_conn = None
            self._notify_ws({"type": "disconnected"})
            try:
                conn.close()
            except OSError:
                pass

    def send_to_tcp_client(self, text):
        with self.tcp_lock:
            conn = self.tcp_conn
        if conn is None:
            return
        try:
            conn.sendall(text.encode("latin-1"))
        except OSError:
            pass

    def set_ws(self, ws):
        with self.ws_lock:
            self.ws = ws

    def _notify_ws(self, obj):
        with self.ws_lock:
            ws = self.ws
        if ws is not None:
            try:
                ws.send_text(json.dumps(obj))
            except OSError:
                pass


def make_handler(bridge):
    class Handler(http.server.SimpleHTTPRequestHandler):
        def do_GET(self):
            # SQ9FK: symuluje wylacznie /?J, /?K, /?S{bank}{kod} i /?F{bank}{0|1} - stan/nazwy
            # anten (TRX1/TRX2) i stan Radio Flex ("PWR"), zeby rotator_wifi_bridge dalo sie
            # przetestowac bez sprzetu. Nie modeluje calego urzadzenia (kolizje/PTT/BCD) - to
            # jedyne punkty, ktorych most uzywa.
            if self.path.startswith("/?"):
                query = self.path[2:]
                if query == "J":
                    body = (f"A={antenna_state[0]},{antenna_state[1]},"
                            f"{flex_state[0]},{flex_state[1]}").encode()
                    self.send_response(200)
                    self.send_header("Content-Type", "text/plain")
                    self.send_header("Content-Length", str(len(body)))
                    self.end_headers()
                    self.wfile.write(body)
                    return
                if query == "K":
                    body = ("K=" + ",".join(antenna_names) + "," + site_name).encode()
                    self.send_response(200)
                    self.send_header("Content-Type", "text/plain")
                    self.send_header("Content-Length", str(len(body)))
                    self.end_headers()
                    self.wfile.write(body)
                    return
                if len(query) == 4 and query[0] == "S" and query[1:].isdigit():
                    bank = int(query[1]) - 1
                    code = int(query[2:])
                    if 0 <= bank <= 1 and 0 <= code <= 6:
                        antenna_state[bank] = code
                    self.send_response(200)
                    self.send_header("Content-Type", "text/plain")
                    self.end_headers()
                    return
                if len(query) == 3 and query[0] == "F" and query[1:].isdigit():
                    bank = int(query[1]) - 1
                    value = int(query[2])
                    if 0 <= bank <= 1 and value in (0, 1):
                        flex_state[bank] = value
                    self.send_response(200)
                    self.send_header("Content-Type", "text/plain")
                    self.end_headers()
                    return
            if self.path == WS_PATH:
                ws = websocket_handshake(self)
                if ws is None:
                    self.send_error(400, "Expected WebSocket upgrade")
                    return
                bridge.set_ws(ws)
                print("  Przeglądarka połączona z mostem OTRSP-TCP.")
                try:
                    while True:
                        msg = ws.recv_text()
                        if msg is None:
                            break
                        try:
                            obj = json.loads(msg)
                        except ValueError:
                            continue
                        if obj.get("type") == "data":
                            bridge.send_to_tcp_client(obj.get("data", ""))
                finally:
                    bridge.set_ws(None)
                    print("  Przeglądarka rozłączona od mostu OTRSP-TCP.")
                return
            super().do_GET()

        def log_message(self, fmt, *args):
            if self.path != WS_PATH:
                super().log_message(fmt, *args)

    return Handler


def main():
    # Katalog skryptu = katalog serwowany (działa niezależnie od bieżącego katalogu).
    root = os.path.dirname(os.path.abspath(__file__))
    os.chdir(root)

    start_port = DEFAULT_PORT
    otrsp_tcp_port = DEFAULT_OTRSP_TCP_PORT
    if len(sys.argv) > 1:
        try:
            start_port = int(sys.argv[1])
        except ValueError:
            print(f"Nieprawidłowy port '{sys.argv[1]}', używam {DEFAULT_PORT}.")
    if len(sys.argv) > 2:
        try:
            otrsp_tcp_port = int(sys.argv[2])
        except ValueError:
            print(f"Nieprawidłowy port OTRSP-TCP '{sys.argv[2]}', używam {DEFAULT_OTRSP_TCP_PORT}.")

    if not os.path.exists(PAGE):
        print(f"UWAGA: nie znaleziono {PAGE} w {root}")

    bridge = OtrspBridge(HOST, otrsp_tcp_port)
    handler = make_handler(bridge)
    socketserver.ThreadingTCPServer.allow_reuse_address = True

    # Znajdź pierwszy wolny port od start_port (np. gdy 8765 jest zajęty). ThreadingTCPServer
    # (nie zwykly TCPServer) - most OTRSP-TCP (WebSocket) trzyma polaczenie otwarte i nie moze
    # blokowac obslugi zwyklych zadan HTTP na tym samym serwerze.
    httpd = None
    port = start_port
    for p in range(start_port, start_port + 20):
        try:
            httpd = socketserver.ThreadingTCPServer((HOST, p), handler)
            port = p
            break
        except OSError:
            continue
    if httpd is None:
        print(f"Brak wolnego portu w zakresie {start_port}..{start_port + 19}.")
        sys.exit(1)
    httpd.daemon_threads = True

    url = f"http://{HOST}:{port}/{PAGE}"
    print("=" * 54)
    print("  Symulator interfejsu WWW - SP9PDF Antenna Switch")
    print(f"  Katalog: {root}")
    print(f"  Adres:   {url}")
    print("  Zatrzymanie: Ctrl+C")
    print("=" * 54)

    bridge.start()

    try:
        webbrowser.open(url)
    except Exception:
        pass  # brak przeglądarki / środowisko bez GUI - nie przerywaj serwera

    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("\nZatrzymano.")
    finally:
        httpd.server_close()


if __name__ == "__main__":
    main()
