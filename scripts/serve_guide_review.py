#!/usr/bin/env python3
"""Serve local guide feedback to inform later source-code improvements.

Python 3.12 standard library only. Responses are stored in the feedback folder.
The server binds exclusively to 127.0.0.1 and accepts its exact origin/Host only.
"""
from __future__ import annotations

import argparse
import hashlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import re
import tempfile
import threading
from urllib.parse import urlsplit


MAX_BODY = 1024 * 1024
KINDS = {"section", "instruction", "count", "catalog-request"}


def issue(field: str, message: str, case_id: str = "") -> dict:
    return {"caseId": case_id, "field": field, "message": message}


def response(ok: bool, review: dict | None = None, errors: list | None = None) -> dict:
    decisions = review.get("decisions", []) if review else []
    if not isinstance(decisions, list):
        decisions = []
    value = {"ok": ok, "savedCount": len(decisions) if ok else 0,
             "requestCount": sum(isinstance(d, dict) and d.get("kind") == "catalog-request" for d in decisions),
             "errors": errors or []}
    if review is not None:
        value["review"] = review
    return value


def unique_object(pairs: list) -> dict:
    value = {}
    for key, item in pairs:
        if key in value:
            raise ValueError("El JSON contiene campos repetidos.")
        value[key] = item
    return value


def reject_constant(_: str) -> None:
    raise ValueError("El JSON contiene un número no finito.")


def read_json(path: Path, maximum: int = MAX_BODY) -> dict:
    if not path.is_file() or path.stat().st_size > maximum:
        raise ValueError("El archivo JSON falta o excede el límite permitido.")
    with path.open(encoding="utf-8") as stream:
        value = json.load(stream, object_pairs_hook=unique_object, parse_constant=reject_constant)
    if not isinstance(value, dict):
        raise ValueError("Se esperaba un objeto JSON.")
    return value


def atomic_json(path: Path, value: dict) -> None:
    if path.is_symlink():
        raise ValueError("El borrador no puede ser un enlace simbólico.")
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(mode="w", encoding="utf-8", dir=path.parent,
                                         prefix=".guide-review-", suffix=".json", delete=False) as stream:
            temporary = Path(stream.name)
            json.dump(value, stream, ensure_ascii=False, indent=2, allow_nan=False)
            stream.write("\n")
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
        temporary = None
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


class ReviewState:
    def __init__(self, args: argparse.Namespace):
        self.directory = args.directory.resolve(strict=True)
        self.manifest_file = args.manifest.resolve(strict=True)
        self.evaluation_file = args.evaluation.resolve(strict=True)
        self.catalogue_file = args.catalogue.resolve(strict=True)
        self.html = self.directory / "guide-recognition-review.html"
        self.audio = self.directory / "all-guide-clips.wav"
        self.draft = self.directory / "guide-review-decisions.json"
        if not self.directory.is_dir() or not self.html.is_file() or not self.audio.is_file():
            raise ValueError("El directorio debe contener guide-recognition-review.html y all-guide-clips.wav.")
        if self.draft.resolve() in {self.manifest_file, self.evaluation_file, self.catalogue_file,
                                   self.html.resolve(), self.audio.resolve()}:
            raise ValueError("El archivo de respuestas debe ser distinto de las entradas.")
        manifest = read_json(self.manifest_file, 16 * MAX_BODY)
        evaluation = read_json(self.evaluation_file, 16 * MAX_BODY)
        self.dataset_id = manifest.get("masterSha256")
        if not isinstance(self.dataset_id, str) or not re.fullmatch(r"[0-9a-f]{64}", self.dataset_id):
            raise ValueError("El manifiesto requiere masterSha256.")
        digest = hashlib.sha256()
        with self.audio.open("rb") as stream:
            for chunk in iter(lambda: stream.read(64 * 1024), b""):
                digest.update(chunk)
        if digest.hexdigest() != self.dataset_id:
            raise ValueError("El audio que se sirve no coincide con el conjunto de revisión.")
        clips, cases = manifest.get("clips"), evaluation.get("cases")
        if not isinstance(clips, list) or not 1 <= len(clips) <= 512 or not isinstance(cases, list) or not cases:
            raise ValueError("Se requieren clips y casos de evaluación no vacíos.")
        self.case_ids = set()
        for clip in clips:
            case_id = clip.get("id") if isinstance(clip, dict) else None
            if not isinstance(case_id, str) or not case_id or len(case_id) > 128 or case_id in self.case_ids:
                raise ValueError("Los IDs del manifiesto deben ser únicos y no vacíos.")
            self.case_ids.add(case_id)
        evaluated = set()
        for case in cases:
            case_id = case.get("id") if isinstance(case, dict) else None
            if not isinstance(case_id, str) or case_id not in self.case_ids or case_id in evaluated:
                raise ValueError("La evaluación debe identificar casos únicos del manifiesto.")
            evaluated.add(case_id)
        if evaluated != self.case_ids:
            raise ValueError("La evaluación debe cubrir los mismos casos que el manifiesto.")
        self.lock = threading.RLock()
        self.catalogue = self.load_catalogue()

    def empty_review(self) -> dict:
        return {"schemaVersion": 1, "datasetId": self.dataset_id, "decisions": []}

    def validate(self, review: object) -> list[dict]:
        errors = []
        if not isinstance(review, dict):
            return [issue("review", "Se esperaba un objeto JSON.")]
        if set(review) - {"schemaVersion", "datasetId", "decisions"}:
            errors.append(issue("review", "El borrador contiene campos desconocidos."))
        if type(review.get("schemaVersion")) is not int or review["schemaVersion"] != 1:
            errors.append(issue("schemaVersion", "La versión del borrador debe ser 1."))
        if review.get("datasetId") != self.dataset_id:
            errors.append(issue("datasetId", "El borrador pertenece a otro conjunto de audio."))
        decisions = review.get("decisions")
        if not isinstance(decisions, list) or len(decisions) > len(self.case_ids):
            errors.append(issue("decisions", "La lista de decisiones no es válida o excede los casos del audio."))
            return errors
        seen = set()
        for decision in decisions:
            if not isinstance(decision, dict):
                errors.append(issue("decisions", "Cada decisión debe ser un objeto."))
                continue
            case_id = decision.get("caseId")
            safe_id = case_id if isinstance(case_id, str) else ""
            if not isinstance(case_id, str) or case_id not in self.case_ids or case_id in seen:
                errors.append(issue("caseId", "El caso no existe o está repetido.", safe_id))
            else:
                seen.add(case_id)
            if set(decision) - {"caseId", "confirmed", "kind", "key", "ordinal", "name", "note"}:
                errors.append(issue("decision", "La decisión contiene campos desconocidos.", safe_id))
            if type(decision.get("confirmed")) is not bool:
                errors.append(issue("confirmed", "La confirmación debe ser explícitamente true o false.", safe_id))
            kind = decision.get("kind")
            if not isinstance(kind, str) or kind not in KINDS:
                errors.append(issue("kind", "El tipo de decisión no es válido.", safe_id))
            key = decision.get("key")
            if not isinstance(key, str) or len(key) > 128 or (
                    key != "" if kind == "catalog-request" else not key.strip()):
                errors.append(issue("key", "La clave debe ser canónica; las solicitudes usan una clave vacía.", safe_id))
            elif kind in ("section", "instruction", "count"):
                family = {"section": "sections", "instruction": "instructions", "count": "counts"}[kind]
                if key not in {entry["key"] for entry in self.catalogue[family]}:
                    errors.append(issue("key", "Elige una clave del catálogo de referencia o solicita un nombre nuevo.", safe_id))
            if "ordinal" in decision and (kind != "section" or type(decision["ordinal"]) is not int
                                            or not 1 <= decision["ordinal"] <= 999):
                errors.append(issue("ordinal", "Sólo las secciones admiten un ordinal entero entre 1 y 999.", safe_id))
            name = decision.get("name")
            if ("name" in decision and (not isinstance(name, str) or len(name) > 128)) or (
                    kind == "catalog-request" and (not isinstance(name, str) or not name.strip())):
                errors.append(issue("name", "La solicitud necesita un nombre de hasta 128 caracteres.", safe_id))
            if "note" in decision and (not isinstance(decision["note"], str) or len(decision["note"]) > 1000):
                errors.append(issue("note", "La nota admite hasta 1000 caracteres.", safe_id))
        return errors

    def saved_review(self) -> tuple[dict, list]:
        with self.lock:
            if not self.draft.exists():
                return self.empty_review(), []
            try:
                review = read_json(self.draft)
                return review, self.validate(review)
            except (OSError, ValueError, UnicodeError, RecursionError):
                return self.empty_review(), [issue("review", "El borrador guardado no es válido; se conserva el archivo.")]

    def load_catalogue(self) -> dict:
        catalogue = read_json(self.catalogue_file)
        for family in ("sections", "instructions", "counts"):
            entries = catalogue.get(family)
            if not isinstance(entries, list) or len(entries) > 256:
                raise ValueError("El catálogo de referencia no es válido.")
            keys = set()
            for entry in entries:
                if not isinstance(entry, dict) or any(not isinstance(entry.get(field), str)
                    or not entry[field].strip() or len(entry[field]) > 128 for field in ("key", "label")):
                    raise ValueError("El catálogo de referencia no es válido.")
                if entry["key"] in keys:
                    raise ValueError("El catálogo de referencia contiene claves repetidas.")
                keys.add(entry["key"])
        return catalogue

    def save(self, review: dict) -> tuple[int, dict]:
        with self.lock:
            errors = self.validate(review)
            if errors:
                return 422, response(False, errors=errors)
            atomic_json(self.draft, review)
            value = response(True, review)
            value["message"] = "Respuestas guardadas para adaptar código."
            return 200, value


class ReviewServer(ThreadingHTTPServer):
    daemon_threads = True

    def __init__(self, port: int, state: ReviewState):
        super().__init__(("127.0.0.1", port), ReviewHandler)
        self.state = state
        self.authority = f"127.0.0.1:{self.server_address[1]}"
        self.origin = f"http://{self.authority}"


class ReviewHandler(BaseHTTPRequestHandler):
    server_version = "PlayWorshipGuideReview/1"

    def log_message(self, format: str, *args) -> None:
        pass

    def json_response(self, status: int, value: dict) -> None:
        body = json.dumps(value, ensure_ascii=False, allow_nan=False).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        self.end_headers()
        if self.command != "HEAD":
            self.wfile.write(body)

    def error(self, status: int, field: str, message: str) -> None:
        self.json_response(status, response(False, errors=[issue(field, message)]))

    def authorized(self) -> bool:
        hosts, origins = self.headers.get_all("Host", []), self.headers.get_all("Origin", [])
        if hosts != [self.server.authority] or len(origins) > 1 or (origins and origins[0] != self.server.origin):
            self.error(403, "origin", "Usa la dirección local de esta revisión.")
            return False
        try:
            parsed = urlsplit(self.path)
        except ValueError:
            self.error(403, "path", "La ruta no es válida.")
            return False
        if parsed.scheme or parsed.netloc:
            self.error(403, "path", "La ruta debe pertenecer a esta revisión local.")
            return False
        return True

    def do_HEAD(self) -> None:
        self.do_GET()

    def do_GET(self) -> None:
        if not self.authorized():
            return
        path = urlsplit(self.path).path
        state = self.server.state
        try:
            if path == "/api/review":
                review, errors = state.saved_review()
                self.json_response(422 if errors else 200, response(not errors, review, errors))
            elif path == "/api/catalogue":
                self.json_response(200, state.catalogue)
            elif path in ("/", "/guide-recognition-review.html"):
                self.send_file(state.html, "text/html; charset=utf-8", ranges=False)
            elif path == "/audio.wav":
                self.send_file(state.audio, "audio/wav", ranges=True)
            else:
                self.error(404, "path", "La ruta no existe.")
        except (OSError, ValueError, UnicodeError, RecursionError):
            self.error(422, "review", "No se pudo leer la revisión o el catálogo de referencia.")

    def send_file(self, path: Path, content_type: str, ranges: bool) -> None:
        with path.open("rb") as stream:
            size = os.fstat(stream.fileno()).st_size
            start, end, partial = 0, size - 1, False
            range_header = self.headers.get("Range") if ranges else None
            if range_header:
                match = re.fullmatch(r"bytes=(\d*)-(\d*)", range_header)
                invalid = not match or not (match[1] or match[2]) or size == 0
                if not invalid:
                    if match[1]:
                        start = int(match[1]); end = min(int(match[2]), size - 1) if match[2] else size - 1
                        invalid = start >= size or end < start
                    else:
                        length = int(match[2]); invalid = length <= 0
                        start = max(0, size - length)
                if invalid:
                    self.send_response(416)
                    self.send_header("Content-Range", f"bytes */{size}")
                    self.send_header("Content-Length", "0")
                    self.end_headers()
                    return
                partial = True
            length = max(0, end - start + 1)
            self.send_response(206 if partial else 200)
            self.send_header("Content-Type", content_type)
            self.send_header("Content-Length", str(length))
            self.send_header("Cache-Control", "no-store")
            self.send_header("X-Content-Type-Options", "nosniff")
            if ranges:
                self.send_header("Accept-Ranges", "bytes")
            if partial:
                self.send_header("Content-Range", f"bytes {start}-{end}/{size}")
            self.end_headers()
            if self.command == "HEAD":
                return
            stream.seek(start)
            while length:
                chunk = stream.read(min(length, 64 * 1024))
                if not chunk:
                    break
                self.wfile.write(chunk)
                length -= len(chunk)

    def do_POST(self) -> None:
        if not self.authorized():
            return
        path = urlsplit(self.path).path
        if path != "/api/review":
            self.error(404, "path", "La ruta no existe.")
            return
        if self.headers.get_content_type() != "application/json":
            self.error(415, "contentType", "Usa application/json.")
            return
        lengths = self.headers.get_all("Content-Length", [])
        if len(lengths) != 1 or not re.fullmatch(r"[0-9]{1,10}", lengths[0]) or self.headers.get("Transfer-Encoding"):
            self.error(422, "body", "La petición requiere una longitud JSON válida.")
            return
        length = int(lengths[0])
        if length > MAX_BODY:
            self.error(413, "body", "El borrador excede 1 MiB.")
            return
        self.connection.settimeout(15)
        try:
            body = self.rfile.read(length)
            if len(body) != length:
                raise ValueError("Incomplete body")
            review = json.loads(body.decode("utf-8"), object_pairs_hook=unique_object, parse_constant=reject_constant)
            status, value = self.server.state.save(review)
            self.json_response(status, value)
        except (OSError, ValueError, UnicodeError, RecursionError):
            self.error(422, "review", "No se pudieron guardar las respuestas; verifica el borrador y vuelve a intentarlo.")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--directory", required=True, type=Path)
    parser.add_argument("--manifest", required=True, type=Path)
    parser.add_argument("--evaluation", required=True, type=Path)
    parser.add_argument("--catalogue", required=True, type=Path)
    parser.add_argument("--port", type=int, default=0)
    args = parser.parse_args()
    if not 0 <= args.port <= 65535:
        parser.error("--port debe estar entre 0 y 65535.")
    try:
        state = ReviewState(args)
        server = ReviewServer(args.port, state)
    except (OSError, ValueError, UnicodeError) as error:
        parser.error(str(error))
    print(server.origin + "/", flush=True)
    try:
        server.serve_forever(poll_interval=0.25)
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
