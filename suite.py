#!/usr/bin/env python3
"""Regression suite. Run before every push, against every build:

    python3 suite.py                                  # default: ./build-asan/redis
    python3 suite.py ./build/redis
    python3 suite.py setarch x86_64 -R ./build-tsan/redis
    python3 suite.py ./build-release/redis

Every scenario gets a fresh server. A scenario fails if its checks fail, if the
server dies when it should not, or if the server log contains a sanitizer or
assert report. Exit code: 0 = all green, 1 = failures (usable as a gate).

SKIPPED at the bottom lists scenarios that are deliberately off until the
matching PLAN.md item is done. Move them up when the item lands.

A scenario also fails if the server log contains a syscall error line
("epoll_ctl: ...", "accept4: ...", "close: ...") it did not declare with
`fn.expects_errors = True`. more_clients_than_fd_limit needs prlimit (util-linux).
"""
import os
import re
import select
import signal
import socket
import struct
import subprocess
import sys
import time

LAUNCH = sys.argv[1:] or ["./build-asan/redis"]
PORT = 6379
PING = b"*1\r\n$4\r\nping\r\n"
PONG = b"+PONG\r\n"
ERR_UNKNOWN = b"-ERR unknown command\r\n"
SYSCALL_ERR = re.compile(r"^(epoll_ctl|epoll_wait|accept4|close|dup|bind|listen|socket|fcntl|setsockopt): ")


class Fail(Exception):
    pass


def expect(cond, detail):
    if not cond:
        raise Fail(detail)


def conn(timeout=3.0):
    return socket.create_connection(("127.0.0.1", PORT), timeout=timeout)


def recv_until(sock, nbytes, deadline=3.0):
    """Collect exactly-or-less nbytes until the deadline; returns what arrived."""
    sock.settimeout(0.2)
    buf = b""
    end = time.monotonic() + deadline
    while len(buf) < nbytes and time.monotonic() < end:
        try:
            chunk = sock.recv(65536)
        except socket.timeout:
            continue
        if not chunk:
            break
        buf += chunk
    return buf


def cpu_ticks(pid):
    with open(f"/proc/{pid}/stat") as f:
        fields = f.read().rsplit(")", 1)[1].split()
    return int(fields[11]) + int(fields[12])


def start_server():
    p = subprocess.Popen(LAUNCH, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    for _ in range(100):
        if p.poll() is not None:
            break
        try:
            conn(0.5).close()
            return p
        except OSError:
            time.sleep(0.1)
    raise Fail(f"server did not start (rc={p.poll()})")


def alive(p):
    expect(p.poll() is None, f"server died, rc={p.returncode}")


def serves(p):
    s = conn()
    s.sendall(PING)
    r = recv_until(s, len(PONG))
    s.close()
    expect(r == PONG, f"not serving: {r!r}")


# --- scenarios ---------------------------------------------------------------

def single_ping(p):
    s = conn(); s.sendall(PING)
    expect(recv_until(s, len(PONG)) == PONG, "no PONG")
    s.close(); alive(p)


def five_sequential_same_conn(p):
    s = conn()
    for i in range(5):
        s.sendall(PING)
        expect(recv_until(s, len(PONG)) == PONG, f"command #{i + 1} got no PONG")
    s.close(); alive(p)


def pipelined_mbulk(p):
    s = conn(); s.sendall(PING * 2)
    r = recv_until(s, 2 * len(PONG))
    expect(r == PONG * 2, f"expected 2 PONGs, got {r!r}")
    s.close(); alive(p)


def split_mid_payload(p):
    s = conn(); s.sendall(b"*1\r\n$4\r\npi"); time.sleep(0.25); s.sendall(b"ng\r\n")
    expect(recv_until(s, len(PONG)) == PONG, "no PONG after reassembly")
    s.close(); alive(p)


def split_mid_header(p):
    s = conn(); s.sendall(b"*1\r\n$"); time.sleep(0.25); s.sendall(b"4\r\nping\r\n")
    expect(recv_until(s, len(PONG)) == PONG, "no PONG after reassembly")
    s.close(); alive(p)


def inline_single(p):
    s = conn(); s.sendall(b"ping\r\n")
    expect(recv_until(s, len(PONG)) == PONG, "no PONG for inline command")
    s.close(); alive(p)


def inline_pipelined(p):
    s = conn(); s.sendall(b"ping\r\nping\r\n")
    r = recv_until(s, 2 * len(PONG))
    expect(r == PONG * 2, f"expected 2 PONGs, got {r!r}")
    s.close(); alive(p)


def empty_array_is_noop(p):
    s = conn(); s.sendall(b"*0\r\n" + PING)
    expect(recv_until(s, len(PONG)) == PONG, "no PONG after *0")
    s.close(); alive(p)


def two_clients_interleaved(p):
    a, b = conn(), conn()
    for s in (a, b, a):
        s.sendall(PING)
        expect(recv_until(s, len(PONG)) == PONG, "interleaved PONG missing")
    a.close(); b.close(); alive(p)


def twenty_concurrent(p):
    clients = [conn() for _ in range(20)]
    for s in clients:
        s.sendall(PING)
    for s in clients:
        expect(recv_until(s, len(PONG)) == PONG, "one of 20 got no PONG")
        s.close()
    alive(p)


def large_single_write(p):
    n = 5000
    s = conn(); s.sendall(PING * n)
    r = recv_until(s, n * len(PONG), deadline=15.0)
    expect(r.count(PONG) == n, f"expected {n} PONGs, got {r.count(PONG)}")
    s.close(); alive(p)


def connect_close_no_data(p):
    conn().close(); time.sleep(0.2)
    alive(p); serves(p)


def rst_with_unread_reply(p):
    s = conn(); s.sendall(PING); time.sleep(0.15)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, struct.pack("ii", 1, 0))
    s.close(); time.sleep(0.3)
    alive(p); serves(p)


def flood_then_hard_kill(p):
    s = conn()
    end = time.monotonic() + 0.4
    s.settimeout(0.1)
    try:
        while time.monotonic() < end:
            s.sendall(PING * 50)
    except OSError:
        pass
    s.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, struct.pack("ii", 1, 0))
    s.close(); time.sleep(1.0)
    alive(p); serves(p)


def writer_that_never_reads(p):
    s = conn()
    s.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 1024)
    s.settimeout(1.0)
    try:
        s.sendall(PING * 60000)  # ~1 MB; replies are never read
    except OSError:
        pass
    alive(p); serves(p)
    s.close()


def slow_reader_gets_all_replies(p):
    # Replies must exceed the kernel's hard send-buffer cap (tcp_wmem max, 4 MB
    # here), otherwise the kernel absorbs everything and EAGAIN never happens —
    # verified empirically: 350 KB passed with no EPOLLOUT code in the server.
    n = 800000
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 1024)  # tiny window from the handshake on
    s.settimeout(60.0)
    s.connect(("127.0.0.1", PORT))
    s.sendall(PING * n)                     # ~5.6 MB of replies > 4 MB kernel cap
    time.sleep(0.5)
    r = recv_until(s, n * len(PONG), deadline=120.0)
    expect(r.count(PONG) == n, f"expected {n} PONGs, got {r.count(PONG)}")
    s.close(); time.sleep(0.5)
    before = cpu_ticks(p.pid); time.sleep(1.2); after = cpu_ticks(p.pid)
    expect(after - before < 8, f"busy loop after drain: {after - before} ticks (EPOLLOUT left armed?)")
    alive(p)


def sigstop_sigcont(p):
    os.kill(p.pid, signal.SIGSTOP); time.sleep(0.2); os.kill(p.pid, signal.SIGCONT)
    time.sleep(0.1)
    alive(p); serves(p)


def hygiene_after_disconnects(p):
    for _ in range(10):
        s = conn(); s.sendall(PING); recv_until(s, len(PONG)); s.close()
    time.sleep(0.5)
    cw = subprocess.run(["ss", "-tan", "state", "close-wait", "( sport = :6379 )"],
                        capture_output=True, text=True).stdout.strip().splitlines()
    expect(len(cw) <= 1, f"{len(cw) - 1} sockets stuck in CLOSE_WAIT")
    before = cpu_ticks(p.pid); time.sleep(1.5); after = cpu_ticks(p.pid)
    expect(after - before < 8, f"idle server burned {after - before} ticks in 1.5 s")
    alive(p)


def unknown_command_gets_err_reply(p):
    s = conn(); s.sendall(b"*2\r\n$4\r\necho\r\n$2\r\nhi\r\n")
    expect(recv_until(s, len(ERR_UNKNOWN)) == ERR_UNKNOWN, "no -ERR for an unknown command")
    s.sendall(PING)  # the error must not cost the connection
    expect(recv_until(s, len(PONG)) == PONG, "connection did not survive the -ERR")
    s.close(); alive(p)


def more_clients_than_fd_limit(p):
    # Lower the live server's fd limit instead of restarting it under ulimit.
    subprocess.run(["prlimit", "--pid", str(p.pid), "--nofile=32:32"], check=True)
    socks = []
    for _ in range(40):
        try:
            socks.append(conn())
        except OSError:
            break
    time.sleep(0.3)
    readable, _, _ = select.select(socks, [], [], 0.5)
    refused = [s for s in readable if s.recv(1) == b""]  # accepted and closed at once
    held = [s for s in socks if s not in refused]
    expect(refused, "nobody was refused: the fd limit was never hit")
    expect(held, "nobody was served")
    alive(p)
    before = cpu_ticks(p.pid); time.sleep(1.2); after = cpu_ticks(p.pid)
    expect(after - before < 8, f"busy loop while out of fds: {after - before} ticks")
    held.pop().close(); time.sleep(0.3)  # one slot back: the listener must serve again
    serves(p)
    for s in socks:
        s.close()
    alive(p)
more_clients_than_fd_limit.expects_errors = True  # "accept4: ... Too many open files" is the point


def uppercase_PING(p):
    # redis folds command names; redis-benchmark and redis-cli send PING as typed.
    s = conn(); s.sendall(b"*1\r\n$4\r\nPING\r\n")
    expect(recv_until(s, len(PONG)) == PONG, "no PONG for uppercase PING")
    s.sendall(b"PiNg\r\n")
    expect(recv_until(s, len(PONG)) == PONG, "no PONG for mixed-case inline PiNg")
    s.close(); alive(p)


def busy_port_exits_with_failure(p):
    # The failure code is what serve.sh (exec) and a supervisor see; 0 would read as success.
    r = subprocess.run(LAUNCH, capture_output=True, text=True, timeout=10)
    out = r.stdout + r.stderr
    expect(r.returncode == 1, f"second instance exited with {r.returncode}, expected 1")
    expect("Address already in use" in out, f"no EADDRINUSE diagnosis: {out!r}")
    alive(p); serves(p)


SCENARIOS = [
    single_ping,
    five_sequential_same_conn,
    pipelined_mbulk,
    split_mid_payload,
    split_mid_header,
    inline_single,
    inline_pipelined,
    empty_array_is_noop,
    two_clients_interleaved,
    twenty_concurrent,
    large_single_write,
    connect_close_no_data,
    rst_with_unread_reply,
    flood_then_hard_kill,
    writer_that_never_reads,
    slow_reader_gets_all_replies,
    sigstop_sigcont,
    hygiene_after_disconnects,
    unknown_command_gets_err_reply,
    more_clients_than_fd_limit,
    uppercase_PING,
    busy_port_exits_with_failure,
]

# Deliberately off until the matching PLAN.md item is done. Move up when it lands.
SKIPPED = [
    ("empty_bulk_string_arg", "needs a non-ping command to carry it"),
    ("inline_empty_line_is_noop", "bare CRLF gets -ERR unknown command here; redis is silent. Decide which"),
    ("protocol_error_closes_only_that_client", "PLAN, in progress: '%' for '$' still kills the process (rc 0)"),
    ("null_array_is_noop", "PLAN, in progress: *-1 trips assert(ec == errc{}) in parser.cpp:94"),
    ("junk_without_crlf_is_linear", "PLAN: deque -> ring buffer; 512 KB of junk costs 2.58 s CPU today"),
    ("input_buffer_is_bounded", "PLAN: client-query-buffer-limit"),
]

# -----------------------------------------------------------------------------

def main():
    if subprocess.run(["ss", "-ltn"], capture_output=True, text=True).stdout.find(f":{PORT} ") != -1:
        print(f"port {PORT} is busy — stop that server first"); return 2
    failures = 0
    for fn in SCENARIOS:
        try:
            p = start_server()
        except Fail as e:
            print(f"FAIL {fn.__name__:32s} {e}"); failures += 1; continue
        try:
            fn(p)
            ok, detail = True, ""
        except Fail as e:
            ok, detail = False, str(e)
        except Exception as e:  # noqa: BLE001 — a scenario must never kill the run
            ok, detail = False, f"{type(e).__name__}: {e}"
        if p.poll() is None:
            p.kill()
        log = p.communicate()[0]
        reports = [l for l in log.splitlines()
                   if ("Sanitizer" in l and "HINT" not in l) or "Assertion" in l or "runtime error" in l]
        if ok and reports:
            ok, detail = False, f"log has a report: {reports[0]!r}"
        if not ok and reports and "log" not in detail:
            detail += f"   [log: {reports[0]!r}]" if reports else ""
        errs = [l for l in log.splitlines() if SYSCALL_ERR.match(l)]
        if ok and errs and not getattr(fn, "expects_errors", False):
            ok, detail = False, f"unexpected syscall error in log: {errs[0]!r}"
        print(f'{"OK  " if ok else "FAIL"} {fn.__name__:32s} {detail}')
        failures += 0 if ok else 1
    for name, why in SKIPPED:
        print(f"SKIP {name:32s} {why}")
    print(f"\n{len(SCENARIOS) - failures}/{len(SCENARIOS)} passed, {len(SKIPPED)} skipped")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
