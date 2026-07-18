import http from "node:http";
import { readFile } from "node:fs/promises";
import path from "node:path";
import { fileURLToPath } from "node:url";

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const host = process.env.FREESHOW_HOST || "127.0.0.1";
const port = Number(process.env.FREESHOW_PORT || "5506");
const listenPort = Number(process.env.PORT || "8787");

const indexHtml = await readFile(path.join(__dirname, "index.html"), "utf8");

function cors(res) {
  res.setHeader("Access-Control-Allow-Origin", "*");
  res.setHeader("Access-Control-Allow-Methods", "GET,POST,OPTIONS");
  res.setHeader("Access-Control-Allow-Headers", "Content-Type");
}

function proxyToFreeShow(req, res, body) {
  const url = new URL(req.url, `http://${host}:${port}`);
  const upstream = http.request(
    {
      hostname: host,
      port,
      path: `${url.pathname}${url.search}`,
      method: req.method,
      headers: {
        "Content-Type": req.headers["content-type"] || "application/json",
      },
    },
    (upstreamRes) => {
      cors(res);
      res.writeHead(upstreamRes.statusCode || 502, {
        "Content-Type": upstreamRes.headers["content-type"] || "application/json",
      });
      upstreamRes.pipe(res);
    }
  );

  upstream.on("error", (err) => {
    cors(res);
    res.writeHead(502, { "Content-Type": "application/json" });
    res.end(JSON.stringify({ error: String(err) }));
  });

  if (body) upstream.write(body);
  upstream.end();
}

const server = http.createServer((req, res) => {
  cors(res);

  if (req.method === "OPTIONS") {
    res.writeHead(204);
    res.end();
    return;
  }

  if (req.url && (req.url === "/" || req.url.startsWith("/?"))) {
    res.writeHead(200, { "Content-Type": "text/html; charset=utf-8" });
    res.end(indexHtml);
    return;
  }

  if (req.url && req.url.startsWith("/api")) {
    const chunks = [];
    req.on("data", (chunk) => chunks.push(chunk));
    req.on("end", () => {
      const body = chunks.length ? Buffer.concat(chunks) : null;
      proxyToFreeShow(req, res, body);
    });
    return;
  }

  if (req.url && req.url === "/health") {
    res.writeHead(200, { "Content-Type": "application/json" });
    res.end(JSON.stringify({ ok: true }));
    return;
  }

  res.writeHead(404, { "Content-Type": "application/json" });
  res.end(JSON.stringify({ error: "not found" }));
});

server.listen(listenPort, () => {
  console.log(`FreeShow launcher running on http://127.0.0.1:${listenPort}`);
  console.log(`Proxying to FreeShow at http://${host}:${port}`);
});
