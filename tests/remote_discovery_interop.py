"""Exercise Apple Bonjour and the Android/Windows mDNS backend in both directions."""
import os
import select
import subprocess
import sys
import uuid


def check(advertiser, browser):
    name = "PW interop " + uuid.uuid4().hex[:8]
    env = dict(os.environ)
    env.pop("PW_REMOTE_DISCOVERY_EXPECT", None)
    env["PW_REMOTE_ADVERTISE_ONLY"] = name
    server = subprocess.Popen(
        [advertiser], env=env, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
        stderr=subprocess.PIPE, text=True,
    )
    try:
        ready, _, _ = select.select([server.stdout], [], [], 10)
        if not ready or server.stdout.readline().strip() != "ready":
            raise RuntimeError("Advertiser did not start")
        env.pop("PW_REMOTE_ADVERTISE_ONLY")
        env["PW_REMOTE_DISCOVERY_EXPECT"] = name
        subprocess.run([browser], env=env, check=True, timeout=20)
    finally:
        try:
            server.communicate("done\n", timeout=5)
        except subprocess.TimeoutExpired:
            server.kill()
            server.communicate()
    if server.returncode != 0:
        raise RuntimeError("Advertiser did not shut down cleanly")


check(sys.argv[1], sys.argv[2])
check(sys.argv[2], sys.argv[1])
print("Native/portable discovery and WebSocket interoperability passed")
