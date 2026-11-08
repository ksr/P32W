#!/usr/bin/env python3
"""p32tls.py IP PORT NAME [CAFILE] -- TLS for the emulators' network model
(D45; docs/net-mailbox.md section 6). The emulator runs this for each TLS
socket P32W opens, on a socket pair as stdin and stdout: it connects to
IP:PORT, does the TLS handshake with NAME for SNI and the certificate's
host-name check, trusting CAFILE (else the Mac's certificates), and writes
one byte saying how that went:

    K  connected: the decrypted stream follows, both ways
    R  refused    T  timed out    U  unreachable    E  the TLS handshake or
                                                       the certificate failed

then carries the data until the server closes, as the coprocessor's
firmware will with mbedTLS. The end of stdin does not close the
connection (a client often shuts down its side and then reads the answer).
"""
import os, select, socket, ssl, sys

def send_all(t, data):
    """all of data on the TLS socket (non-blocking), waiting while it can take no more"""
    while data:
        try:
            data = data[t.send(data):]
        except ssl.SSLWantWriteError:
            select.select([], [t], [])
        except ssl.SSLWantReadError:
            select.select([t], [], [])

def main():
    ip, port, name = sys.argv[1], int(sys.argv[2]), sys.argv[3]
    ca = sys.argv[4] if len(sys.argv) > 4 and sys.argv[4] else None
    if ca is None and os.path.exists("/etc/ssl/cert.pem"):
        ca = "/etc/ssl/cert.pem"        # (the Mac's store; Python's own may be empty)
    try:
        ctx = ssl.create_default_context(cafile=ca)
        raw = socket.create_connection((ip, port), timeout=20)
        t = ctx.wrap_socket(raw, server_hostname=name or ip)
    except ConnectionRefusedError:
        os.write(1, b"R"); return
    except (socket.timeout, TimeoutError):
        os.write(1, b"T"); return
    except (ssl.SSLError, ssl.CertificateError, ValueError):
        os.write(1, b"E"); return
    except OSError:
        os.write(1, b"U"); return
    os.write(1, b"K")
    t.setblocking(False)                # (TLS 1.3 sends records with no data, its session tickets, after
    inputs = [0, t]                     # the handshake: a blocking recv would wait for data never coming)
    try:
        while True:
            if t.pending():             # (decrypted bytes already inside ssl)
                ready = [t]
            else:
                ready, _, _ = select.select(inputs, [], [])
            if t in ready:
                try:
                    data = t.recv(16384)
                except ssl.SSLWantReadError:
                    data = None         # (a record with no data in it)
                if data is not None:
                    if not data:
                        break
                    os.write(1, data)
            if 0 in ready:
                data = os.read(0, 16384)
                if not data:
                    inputs.remove(0)    # (P32W shut down its side: keep reading)
                else:
                    send_all(t, data)
    except (OSError, ssl.SSLError):
        pass
    t.close()

if __name__ == "__main__":
    main()
