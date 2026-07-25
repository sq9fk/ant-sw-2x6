#!/usr/bin/env python3
"""
Prosty serwer WWW do podglądu symulatora interfejsu SP9PDF Antenna Switch.

Uruchomienie:
    python serve.py            # domyślny port 8765 (lub pierwszy wolny wyżej)
    python serve.py 8080       # wskazany port

Skrypt serwuje katalog, w którym się znajduje (tools/), otwiera websim.html
w domyślnej przeglądarce i działa aż do Ctrl+C. Nasłuchuje tylko na 127.0.0.1
(lokalnie). Aby udostępnić w sieci LAN, zmień HOST na "0.0.0.0".
"""
import http.server
import os
import socketserver
import sys
import webbrowser

HOST = "127.0.0.1"
DEFAULT_PORT = 8765
PAGE = "websim.html"


def main():
    # Katalog skryptu = katalog serwowany (działa niezależnie od bieżącego katalogu).
    root = os.path.dirname(os.path.abspath(__file__))
    os.chdir(root)

    start_port = DEFAULT_PORT
    if len(sys.argv) > 1:
        try:
            start_port = int(sys.argv[1])
        except ValueError:
            print(f"Nieprawidłowy port '{sys.argv[1]}', używam {DEFAULT_PORT}.")

    if not os.path.exists(PAGE):
        print(f"UWAGA: nie znaleziono {PAGE} w {root}")

    handler = http.server.SimpleHTTPRequestHandler
    socketserver.TCPServer.allow_reuse_address = True

    # Znajdź pierwszy wolny port od start_port (np. gdy 8765 jest zajęty).
    httpd = None
    port = start_port
    for p in range(start_port, start_port + 20):
        try:
            httpd = socketserver.TCPServer((HOST, p), handler)
            port = p
            break
        except OSError:
            continue
    if httpd is None:
        print(f"Brak wolnego portu w zakresie {start_port}..{start_port + 19}.")
        sys.exit(1)

    url = f"http://{HOST}:{port}/{PAGE}"
    print("=" * 54)
    print("  Symulator interfejsu WWW - SP9PDF Antenna Switch")
    print(f"  Katalog: {root}")
    print(f"  Adres:   {url}")
    print("  Zatrzymanie: Ctrl+C")
    print("=" * 54)

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
