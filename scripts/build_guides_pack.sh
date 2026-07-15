#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'USAGE'
Usage:
  scripts/build_guides_pack.sh [--source-root <dir>] [--output-zip <file>] [--ffmpeg <path>]

Defaults:
  --source-root "$HOME/Documents/proyectos/playworship"
  --output-zip assets/guides/guides-en-es.zip
  --ffmpeg ffmpeg
USAGE
}

SOURCE_ROOT="${HOME}/Documents/proyectos/playworship"
OUTPUT_ZIP="assets/guides/guides-en-es.zip"
FFMPEG_BIN="ffmpeg"
GUIDES_PACK_VERSION="en-es-v3"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --source-root)
      SOURCE_ROOT="${2:-}"
      shift 2
      ;;
    --output-zip)
      OUTPUT_ZIP="${2:-}"
      shift 2
      ;;
    --ffmpeg)
      FFMPEG_BIN="${2:-}"
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown argument: $1" >&2
      usage
      exit 1
      ;;
  esac
done

if [[ ! -d "$SOURCE_ROOT" ]]; then
  echo "Source root not found: $SOURCE_ROOT" >&2
  exit 1
fi

if ! command -v "$FFMPEG_BIN" >/dev/null 2>&1; then
  echo "ffmpeg binary not found: $FFMPEG_BIN" >&2
  exit 1
fi

resolve_category_dir() {
  local root="$1"
  local lang="$2"
  local category="$3"
  local candidates=()
  if [[ "$lang" == "en" ]]; then
    if [[ "$category" == "sections" ]]; then
      candidates+=(
        "$root/Click and Guide Samples/English Guides/Song Sections"
        "$root/English Guides/Song Sections"
        "$root/en/sections"
      )
    else
      candidates+=(
        "$root/Click and Guide Samples/English Guides/Dynamic Cues"
        "$root/English Guides/Dynamic Cues"
        "$root/en/cues"
      )
    fi
  elif [[ "$lang" == "es" ]]; then
    if [[ "$category" == "sections" ]]; then
      candidates+=(
        "$root/Click and Guide Samples/Spanish Guides/Song Sections"
        "$root/Spanish Guides/Song Sections"
        "$root/es/sections"
      )
    else
      candidates+=(
        "$root/Click and Guide Samples/Spanish Guides/Guide Cues"
        "$root/Spanish Guides/Guide Cues"
        "$root/es/cues"
      )
    fi
  fi
  candidates+=(
    "$root/$lang/$category"
    "$root/guides/$lang/$category"
    "$root/guide/$lang/$category"
    "$root/assets/guides/$lang/$category"
  )
  for candidate in "${candidates[@]}"; do
    if [[ -d "$candidate" ]]; then
      printf '%s\n' "$candidate"
      return 0
    fi
  done
  return 1
}

TMP_DIR="$(mktemp -d)"
cleanup() {
  rm -rf "$TMP_DIR"
}
trap cleanup EXIT

convert_category() {
  local lang="$1"
  local category="$2"
  local src_dir
  if ! src_dir="$(resolve_category_dir "$SOURCE_ROOT" "$lang" "$category")"; then
    echo "Warning: missing $lang/$category in source root" >&2
    return 0
  fi

  local dst_base="$TMP_DIR/$lang/$category"
  mkdir -p "$dst_base"
  local seen_keys_file
  seen_keys_file="$TMP_DIR/.seen-${lang}-${category}.txt"
  : > "$seen_keys_file"

  normalize_key() {
    local key="$1"
    key="$(printf '%s' "$key" | tr '[:upper:]' '[:lower:]')"
    key="$(printf '%s' "$key" | sed -E 's/[^[:alnum:]#()+]+/ /g; s/[[:space:]]+/ /g; s/^[[:space:]]+//; s/[[:space:]]+$//')"
    printf '%s' "$key"
  }

  extract_label() {
    local base_name="$1"
    local label="$base_name"
    if [[ "$label" == *" - "* ]]; then
      label="${label#* - }"
    fi
    printf '%s' "$label"
  }

  add_variant_copy() {
    local src_file="$1"
    local variant="$2"
    local normalized
    normalized="$(normalize_key "$variant")"
    [[ -z "$normalized" ]] && return 0
    if grep -Fxq "$normalized" "$seen_keys_file"; then
      return 0
    fi
    # MP3 decodes through both CoreAudio (macOS) and Windows Media Foundation.
    # ALAC/M4A works on macOS but is not reliably readable on Windows.
    local dst="$dst_base/$normalized.mp3"
    "$FFMPEG_BIN" -hide_banner -loglevel error -y -i "$src_file" \
      -codec:a libmp3lame -b:a 128k "$dst"
    printf '%s\n' "$normalized" >> "$seen_keys_file"
  }

  add_es_to_en_aliases() {
    local src_file="$1"
    local text="$2"
    local mapped="$text"
    mapped="$(printf '%s' "$mapped" | sed -E 's/[Pp]re[[:space:]]+[Cc]oro/Pre Chorus/g')"
    mapped="$(printf '%s' "$mapped" | sed -E 's/[Pp]ost[[:space:]]+[Cc]oro/Post Chorus/g')"
    mapped="$(printf '%s' "$mapped" | sed -E 's/[Vv]erso/Verse/g')"
    mapped="$(printf '%s' "$mapped" | sed -E 's/[Cc]oro/Chorus/g')"
    mapped="$(printf '%s' "$mapped" | sed -E 's/[Pp]uente/Bridge/g')"
    mapped="$(printf '%s' "$mapped" | sed -E 's/[Ii]nterludio/Interlude/g')"
    mapped="$(printf '%s' "$mapped" | sed -E 's/[Bb]aja[[:space:]]+[Ii]ntensidad/Breakdown/g')"
    mapped="$(printf '%s' "$mapped" | sed -E 's/[Rr]epetir/Tag/g')"
    mapped="$(printf '%s' "$mapped" | sed -E 's/[Ff]inal/Ending/g')"
    mapped="$(printf '%s' "$mapped" | sed -E 's/[Ii]ntroduccion/Intro/g')"
    mapped="$(printf '%s' "$mapped" | sed -E 's/[Ss]alida/Outro/g')"
    if [[ "$mapped" != "$text" ]]; then
      add_variant_copy "$src_file" "$mapped"
    fi
  }

  while IFS= read -r -d '' file; do
    local base_name
    base_name="$(basename "$file")"
    base_name="${base_name%.*}"
    local label
    label="$(extract_label "$base_name")"
    local label_no_paren
    label_no_paren="$(printf '%s' "$label" | sed -E 's/[[:space:]]*\([^)]*\)//g; s/[[:space:]]+/ /g; s/^[[:space:]]+//; s/[[:space:]]+$//')"
    local paren_text
    paren_text="$(printf '%s' "$label" | sed -nE 's/.*\(([^)]*)\).*/\1/p' | head -n 1)"

    add_variant_copy "$file" "$label"
    add_variant_copy "$file" "$label_no_paren"
    if [[ -n "$paren_text" ]]; then
      add_variant_copy "$file" "$paren_text"
    fi

    if [[ "$lang" == "es" && "$category" == "sections" ]]; then
      add_es_to_en_aliases "$file" "$label"
      add_es_to_en_aliases "$file" "$label_no_paren"
      if [[ -n "$paren_text" ]]; then
        add_es_to_en_aliases "$file" "$paren_text"
      fi
    fi
  done < <(find "$src_dir" -type f \
    \( -iname '*.wav' -o -iname '*.aif' -o -iname '*.aiff' -o -iname '*.mp3' -o -iname '*.m4a' \) \
    -print0 | sort -z)

  if [[ "$category" == "sections" ]]; then
    ensure_canonical_key() {
      local canonical="$1"
      shift
      local canonical_file="$dst_base/$canonical.mp3"
      if [[ -f "$canonical_file" ]]; then
        return 0
      fi
      local pattern
      for pattern in "$@"; do
        local match
        match="$(find "$dst_base" -maxdepth 1 -type f -name '*.mp3' \
          | sed 's#^.*/##' \
          | grep -Ei "^${pattern}\\.mp3$" \
          | sort \
          | head -n 1 || true)"
        if [[ -n "$match" && -f "$dst_base/$match" ]]; then
          cp -f "$dst_base/$match" "$canonical_file"
          printf '%s\n' "$canonical" >> "$seen_keys_file"
          return 0
        fi
      done
    }

    ensure_canonical_key "intro" "intro" "intro 1($| .*)"
    ensure_canonical_key "verse" "verse" "verso" "verse 1($| .*)" "verso 1($| .*)"
    ensure_canonical_key "pre chorus" "pre chorus" "pre coro" \
      "pre chorus 1($| .*)" "pre coro 1($| .*)"
    ensure_canonical_key "chorus" "chorus" "coro" \
      "chorus 1($| .*)" "coro 1($| .*)"
    ensure_canonical_key "post chorus" "post chorus" "post coro" \
      "post chorus 1($| .*)" "post coro 1($| .*)"
    ensure_canonical_key "bridge" "bridge" "puente" \
      "bridge 1($| .*)" "puente 1($| .*)"
    ensure_canonical_key "instrumental" "instrumental" "instrumental 1($| .*)"
    ensure_canonical_key "solo" "solo" "solo 1($| .*)"
    ensure_canonical_key "breakdown" "breakdown" "baja intensidad" \
      "breakdown 1($| .*)" "baja intensidad 1($| .*)"
    ensure_canonical_key "interlude" "interlude" "interludio" \
      "interlude 1($| .*)" "interludio 1($| .*)"
    ensure_canonical_key "tag" "tag" "repetir" \
      "tag 1($| .*)" "repetir 1($| .*)"
    ensure_canonical_key "vamp" "vamp" "vamp 1($| .*)"
    ensure_canonical_key "outro" "outro" "outro 1($| .*)"
    ensure_canonical_key "ending" "ending" "final" \
      "ending 1($| .*)" "final 1($| .*)"
  fi
}

for lang in en es; do
  convert_category "$lang" sections
  convert_category "$lang" cues

done

for lang in en es; do
  if [[ ! -d "$TMP_DIR/$lang/sections" ]]; then
    echo "Missing required sections output for language: $lang" >&2
    exit 1
  fi
  section_count="$(find "$TMP_DIR/$lang/sections" -type f -iname '*.mp3' | wc -l | xargs)"
  if [[ -z "$section_count" || "$section_count" -eq 0 ]]; then
    echo "No section assets produced for language: $lang" >&2
    exit 1
  fi

done

mkdir -p "$(dirname "$OUTPUT_ZIP")"
rm -f "$OUTPUT_ZIP"
printf '%s\n' "$GUIDES_PACK_VERSION" > "$TMP_DIR/pack-version.txt"
(
  cd "$TMP_DIR"
  /usr/bin/zip -qr "$OLDPWD/$OUTPUT_ZIP" en es pack-version.txt
)

echo "Created guides pack: $OUTPUT_ZIP"
