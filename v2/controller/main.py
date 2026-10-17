"""RicinoNext controller entry point.

  PC     : python3 main.py [--port 8080] [--laptime 6] [--speed 1]   (simulated gates and cars)
  ESP32  : run automatically by MicroPython at boot (real I2C gates, Wi-Fi, OTA)

On the ESP32, if the application fails to start (e.g. after a bad file update), a minimal
recovery page is served so a fixed file can be uploaded without USB.
"""
import sys

ESP32 = sys.implementation.name == "micropython" and sys.platform == "esp32"


def recovery(err):
    """Standalone (no ricino import): Wi-Fi from /config.json, then upload form + reboot."""
    import json, machine, network, os, socket, time
    try:
        cfg = json.load(open("/config.json"))
    except (OSError, ValueError):
        cfg = {}
    sta = network.WLAN(network.STA_IF)
    sta.active(True)
    ip = None
    if cfg.get("ssid"):
        sta.connect(cfg["ssid"], cfg.get("password", ""))
        for _ in range(75):
            if sta.isconnected():
                ip = sta.ifconfig()[0]
                break
            time.sleep_ms(200)
    if not ip:
        ap = network.WLAN(network.AP_IF)
        ap.active(True)
        ap.config(essid="RicinoNext-recovery")
        ip = ap.ifconfig()[0]
    print("RECOVERY MODE on http://%s/" % ip)
    page = ("<!doctype html><meta charset=utf-8><meta name=viewport content='width=device-width'>"
            "<title>RicinoNext - secours</title><body style='font:16px system-ui;max-width:40em;margin:2em auto;padding:0 1em'>"
            "<h1>Mode secours</h1><p>L'application n'a pas démarré :</p><pre style='white-space:pre-wrap;background:#eee;padding:1em'>%s</pre>"
            "<p>Renvoyer un fichier corrigé (chemin sur l'ESP32, ex. <code>/ricino/core.py</code>) :</p>"
            "<input type=file id=f> <input id=p value='/ricino/'> <button onclick=\"up()\">Envoyer</button> "
            "<button onclick=\"fetch('/reboot',{method:'POST'})\">Redémarrer</button><p id=m></p>"
            "<script>async function up(){const f=document.getElementById('f').files[0],p=document.getElementById('p');"
            "if(!f)return;let path=p.value.endsWith('/')?p.value+f.name:p.value;"
            "const r=await fetch('/upload?path='+encodeURIComponent(path),{method:'POST',body:f});"
            "document.getElementById('m').textContent=r.ok?'OK : '+path:'Erreur '+r.status}</script>") % (
                err.replace("&", "&amp;").replace("<", "&lt;"))
    s = socket.socket()
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.bind(("0.0.0.0", 80))
    s.listen(2)
    while True:
        c, _ = s.accept()
        try:
            f = c.makefile("rb")
            line = f.readline().decode()
            length = 0
            while True:
                h = f.readline()
                if h in (b"\r\n", b"", b"\n"):
                    break
                if h.lower().startswith(b"content-length:"):
                    length = int(h.split(b":")[1])
            method, target = line.split(" ")[:2]
            if method == "POST" and target.startswith("/upload?path="):
                path = target.split("=", 1)[1].replace("%2F", "/").replace("%2f", "/")
                if ".." in path or not path.startswith("/"):
                    raise ValueError("bad path")
                with open(path, "wb") as out:
                    while length > 0:
                        chunk = f.read(min(1024, length))
                        if not chunk:
                            break
                        out.write(chunk)
                        length -= len(chunk)
                c.send(b"HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nOK")
            elif method == "POST" and target == "/reboot":
                c.send(b"HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nOK")
                c.close()
                machine.reset()
            else:
                body = page.encode()
                c.send(b"HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: %d\r\n\r\n" % len(body))
                c.send(body)
        except Exception as e:
            print("recovery:", e)
        finally:
            c.close()


if ESP32:
    # Start the Wi-Fi driver FIRST: it needs internal RAM for its buffers, and the MicroPython heap grows
    # (into that same RAM) while the application modules are compiled. Started later: "WiFi Out of Memory".
    import gc, network
    network.WLAN(network.STA_IF).active(True)
    gc.collect()
    try:
        from ricino import esp32
        esp32.run()
    except KeyboardInterrupt:
        raise
    except Exception as e:
        import io
        buf = io.StringIO()
        sys.print_exception(e, buf)
        err = buf.getvalue()
        print(err)
        try:
            with open("/error.log", "w") as f:
                f.write(err)
        except OSError:
            pass
        recovery(err)
else:
    from ricino import pc
    pc.run()
