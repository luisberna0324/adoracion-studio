#include "ChordProCodec.h"
#include "TempoValue.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <vector>

namespace ChordProCodec {
namespace {
constexpr int kMaxSourceUtf16 = 1048576;

juce::String canonicalEnvironment(juce::String value) {
  value = value.trim().toLowerCase().replace("-", "_");
  if (value == "pre_chorus") return "prechorus";
  if (value == "introduction") return "intro";
  for (const auto *kind : songLyricsSectionKinds())
    if (value == kind) return kind;
  return {};
}

juce::String headingKind(juce::String value) {
  value = value.trim().toLowerCase().replace("_", " ").replace("-", " ");
  const std::pair<const char *, const char *> names[] = {
      {"pre chorus", "prechorus"}, {"pre coro", "prechorus"}, {"prechorus", "prechorus"},
      {"introducción", "intro"}, {"introduction", "intro"}, {"intro", "intro"},
      {"instrumental", "instrumental"}, {"interludio", "instrumental"},
      {"verse", "verse"}, {"verso", "verse"}, {"estrofa", "verse"},
      {"chorus", "chorus"}, {"coro", "chorus"}, {"estribillo", "chorus"},
      {"bridge", "bridge"}, {"puente", "bridge"}, {"outro", "outro"},
      {"ending", "ending"}, {"final", "ending"}};
  for (const auto &[name, kind] : names) {
    const juce::String prefix = juce::String::fromUTF8(name);
    if (value == prefix || value.startsWith(prefix + " ") ||
        (value.startsWith(prefix) && value.substring(prefix.length()).containsOnly("0123456789")))
      return kind;
  }
  return {};
}

juce::StringArray sourceLines(const juce::String &text) {
  juce::StringArray lines;
  int start = 0;
  for (int i = 0; i < text.length(); ++i) {
    if (text[i] != '\n') continue;
    lines.add(text.substring(start, i));
    start = i + 1;
  }
  // A terminating LF ends the last line; it is not a second blank lyric line.
  if (start < text.length() || text.isEmpty()) lines.add(text.substring(start));
  return lines;
}

juce::String directiveAlias(const juce::String &name) {
  static const std::map<juce::String, juce::String> aliases{
      {"t", "title"}, {"st", "subtitle"}, {"c", "comment"},
      {"soc", "start_of_chorus"}, {"eoc", "end_of_chorus"},
      {"sov", "start_of_verse"}, {"eov", "end_of_verse"},
      {"sob", "start_of_bridge"}, {"eob", "end_of_bridge"},
      {"sog", "start_of_grid"}, {"eog", "end_of_grid"}};
  const auto found = aliases.find(name);
  return found == aliases.end() ? name : found->second;
}

bool singleLineMetadata(const juce::String &value) {
  return !value.containsAnyOf("\r\n{}") && songLyricsUtf16Length(value) >= 0;
}

// The basic subset keeps only section type; labels are accepted with a warning.
// Other environment attributes must not be discarded as though they were labels.
bool supportedEnvironmentLabel(const juce::String &value) {
  if (value.isEmpty()) return true;
  if (value.containsAnyOf("<>")) return false;
  const int equals = value.indexOfChar('=');
  if (equals < 0) return true; // Legacy free-form section label.
  if (value.substring(0, equals).trim() != "label") return false;
  const auto label = value.substring(equals + 1).trim();
  if (label.isEmpty()) return false;
  const auto quote = label[0];
  if (quote == '\'' || quote == '"')
    return label.length() >= 2 && label.endsWithChar(quote) &&
           !label.substring(1, label.length() - 1).containsChar(quote);
  return !label.containsAnyOf(" \t=\"'");
}

struct GridAttributes {
  std::optional<int> width;
  juce::String label;
};

bool parseGridWidth(const juce::String &shape, int &width) {
  const auto normalized = shape.toLowerCase();
  if (!normalized.containsOnly("0123456789x") || normalized.startsWithChar('x') || normalized.endsWithChar('x')) return false;
  const int cross = normalized.indexOfChar('x');
  if (cross >= 0 && normalized.substring(cross + 1).containsChar('x')) return false;
  const auto dimensions = juce::StringArray::fromTokens(shape.toLowerCase(), "x", "");
  if (dimensions.isEmpty() || dimensions.size() > 2) return false;
  int product = 1;
  for (const auto &dimension : dimensions) {
    if (dimension.isEmpty() || !dimension.containsOnly("0123456789") || dimension.length() > 2) return false;
    const int value = dimension.getIntValue();
    if (value < 1 || value > 32 || product * value > 32) return false;
    product *= value;
  }
  width = product;
  return true;
}

// The native grid models cells and plain bar partitions. Margins, repeats,
// strums and multiple chords in one cell need richer storage and are rejected.
bool parseGridAttributes(juce::String arguments, GridAttributes &attributes) {
  arguments = arguments.trim();
  if (arguments.startsWithChar(':')) {
    arguments = arguments.substring(1).trim();
    if (!arguments.containsChar('=')) {
      const int separator = arguments.indexOfAnyOf(" \t");
      const auto shape = separator < 0 ? arguments : arguments.substring(0, separator);
      int width = 0;
      if (!parseGridWidth(shape, width)) return false;
      attributes.width = width;
      if (separator >= 0) attributes.label = arguments.substring(separator).trim();
      return !attributes.label.containsAnyOf("{}<>\\\"'");
    }
  }
  std::set<juce::String> names;
  while (arguments.isNotEmpty()) {
    const int equals = arguments.indexOfChar('=');
    if (equals < 1) return false;
    const auto name = arguments.substring(0, equals).trim();
    if ((name != "shape" && name != "label") || !names.insert(name).second) return false;
    arguments = arguments.substring(equals + 1).trimStart();
    if (arguments.isEmpty()) return false;
    juce::String value;
    if (arguments.startsWithChar('"') || arguments.startsWithChar('\'')) {
      const auto quote = arguments[0];
      const int end = arguments.indexOfChar(1, quote);
      if (end < 0) return false;
      value = arguments.substring(1, end);
      arguments = arguments.substring(end + 1);
      if (arguments.isNotEmpty() && !juce::CharacterFunctions::isWhitespace(arguments[0])) return false;
      arguments = arguments.trimStart();
    } else {
      const int end = arguments.indexOfAnyOf(" \t");
      value = end < 0 ? arguments : arguments.substring(0, end);
      arguments = end < 0 ? juce::String() : arguments.substring(end).trimStart();
    }
    if (value.containsAnyOf("{}<>\\\"'") || songLyricsUtf16Length(value) < 0) return false;
    if (name == "shape") {
      int width = 0;
      if (!parseGridWidth(value, width)) return false;
      attributes.width = width;
    } else {
      attributes.label = value;
    }
  }
  return true;
}

juce::String gridKindForLabel(const juce::String &label) {
  return headingKind(label) == "intro" ? "intro" : "instrumental";
}

bool parseGridRow(const juce::String &line, SongLyricsInstrumentalRow &row) {
  const auto tokens = juce::StringArray::fromTokens(line, " \t", "");
  if (tokens.size() < 3 || tokens[0] != "|" || tokens[tokens.size() - 1] != "|") return false;
  row.id = juce::Uuid().toString();
  int barLength = 0;
  for (int tokenIndex = 1; tokenIndex < tokens.size() - 1; ++tokenIndex) {
    const auto &token = tokens[tokenIndex];
    if (token == "|") {
      if (barLength == 0) return false;
      row.barLengths.push_back(barLength);
      barLength = 0;
      continue;
    }
    if (token != "." && !isValidSongLyricsChordSymbol(token)) return false;
    SongLyricsInstrumentalCell cell;
    cell.id = juce::Uuid().toString();
    if (token != ".") cell.symbol = token;
    row.cells.push_back(std::move(cell));
    if (row.cells.size() > kSongLyricsMaxInstrumentalCellsPerRow) return false;
    ++barLength;
  }
  if (barLength == 0) return false;
  row.barLengths.push_back(barLength);
  return true;
}

struct NativeLine {
  int start = 0, end = 0;
  bool hasSeparator = false;
  juce::String text;
};

std::vector<NativeLine> lyricLines(const juce::String &text) {
  std::vector<NativeLine> result;
  int startCharacter = 0, startUnits = 0, units = 0;
  for (int i = 0; i < text.length(); ++i) {
    const auto character = static_cast<unsigned int>(text[i]);
    if (character == '\n') {
      result.push_back({startUnits, units, true, text.substring(startCharacter, i)});
      startCharacter = i + 1;
      startUnits = units + 1;
    }
    units += character > 0xffffu ? 2 : 1;
  }
  result.push_back({startUnits, units, false, text.substring(startCharacter)});
  return result;
}
} // namespace

ImportResult importText(const juce::String &source) {
  ImportResult result;
  const int sourceLength = songLyricsUtf16Length(source);
  if (sourceLength < 0 || sourceLength > kMaxSourceUtf16) {
    result.error = juce::String::fromUTF8("El archivo ChordPro no es Unicode válido o supera el tamaño permitido.");
    return result;
  }
  const auto normalized = source.replace("\r\n", "\n").replace("\r", "\n");
  const auto lines = sourceLines(normalized);
  SongLyricsDocument document;
  struct ActiveSection { juce::String kind; int start = -1, end = -1; bool paired = false; };
  std::optional<ActiveSection> active;
  struct ActiveGrid { SongLyricsInstrumentalBlock block; std::optional<int> width; };
  std::optional<ActiveGrid> grid;
  std::vector<size_t> pendingBlocks;
  std::map<int, int> orderAtAnchor;
  size_t totalGridCells = 0;
  size_t totalChordCount = 0;
  const auto anchorPendingBlocks = [&](int anchor) {
    for (const auto blockIndex : pendingBlocks) {
      auto &block = document.instrumentalBlocks[blockIndex];
      block.anchorUtf16 = anchor;
      block.order = orderAtAnchor[anchor]++;
    }
    pendingBlocks.clear();
  };
  bool hadLyricLine = false;
  int units = 0;
  std::set<juce::String> seenMetadata;
  auto repeatedMetadata = [&](const juce::String &name) {
    if (!seenMetadata.insert(name).second)
      result.warnings.addIfNotAlreadyThere(
          juce::String::fromUTF8("Un metadato repetido conserva su último valor: ") + name + ".");
  };
  auto fail = [&](int line, const juce::String &message) {
    result.error = juce::String::fromUTF8("Línea ") + juce::String(line + 1) + ": " + message;
    result.document.reset();
  };
  auto closeSection = [&] {
    if (!active) return;
    if (active->start >= 0 && active->end > active->start) {
      document.sections.push_back({juce::Uuid().toString(), active->kind,
                                  active->start, active->end - active->start});
    } else {
      result.warnings.addIfNotAlreadyThere(
          juce::String::fromUTF8("Una sección sin caracteres de letra se importó sin etiqueta; sus acordes se conservan."));
    }
    active.reset();
  };
  auto labelWarning = [&] {
    result.warnings.addIfNotAlreadyThere(
        juce::String::fromUTF8("Los nombres de sección se muestran según el tipo y el orden en PlayWorship."));
  };

  for (int index = 0; index < lines.size(); ++index) {
    const auto &line = lines[index];
    const auto trimmed = line.trim();
    if (trimmed.startsWithChar('#')) {
      result.warnings.addIfNotAlreadyThere(
          "Los comentarios de archivo que empiezan con # no forman parte de la letra.");
      continue;
    }
    if (trimmed.startsWithChar('{')) {
      if (!trimmed.endsWithChar('}') || trimmed.substring(1, trimmed.length() - 1).containsAnyOf("{}")) {
        fail(index, juce::String::fromUTF8("La directiva debe ocupar una línea completa.")); return result;
      }
      const auto directive = trimmed.substring(1, trimmed.length() - 1).trim();
      int nameEnd = 0;
      while (nameEnd < directive.length() && directive[nameEnd] != ':' &&
             !juce::CharacterFunctions::isWhitespace(directive[nameEnd])) ++nameEnd;
      const auto name = directiveAlias(directive.substring(0, nameEnd).toLowerCase());
      const auto arguments = directive.substring(nameEnd).trim();
      const bool colonArguments = arguments.startsWithChar(':');
      const auto value = colonArguments ? arguments.substring(1).trim() : juce::String();
      if (grid) {
        if (name != "end_of_grid" || arguments.isNotEmpty()) {
          fail(index, "Cierra el grid antes de usar otra directiva; no se admiten atributos en su cierre."); return result;
        }
        if (grid->block.rows.empty()) { fail(index, "El grid debe contener al menos una fila."); return result; }
        document.instrumentalBlocks.push_back(std::move(grid->block));
        pendingBlocks.push_back(document.instrumentalBlocks.size() - 1);
        grid.reset();
        if (document.instrumentalBlocks.size() > kSongLyricsMaxInstrumentalBlocks) {
          fail(index, "La letra supera la cantidad de bloques instrumentales permitida."); return result;
        }
        continue;
      }
      if (name == "start_of_grid") {
        GridAttributes attributes;
        if (!parseGridAttributes(arguments, attributes) || attributes.label.length() > kSongLyricsMaxInstrumentalLabelLength) {
          fail(index, juce::String::fromUTF8("Solo se admiten label y shape de 1 a 32 celdas, sin márgenes ni marcado.")); return result;
        }
        if (!active || !active->paired) closeSection();
        grid = ActiveGrid{};
        grid->width = attributes.width;
        grid->block.id = juce::Uuid().toString();
        grid->block.kind = gridKindForLabel(attributes.label);
        grid->block.label = attributes.label;
        if (!attributes.width)
          result.warnings.addIfNotAlreadyThere(juce::String::fromUTF8("El ancho del grid se deduce de sus filas y se exportará explícitamente."));
        continue;
      }
      if (name == "end_of_grid") { fail(index, "No hay un grid abierto que cerrar."); return result; }
      if (arguments.isNotEmpty() && !colonArguments) {
        fail(index, juce::String::fromUTF8("Los atributos de esta directiva aún no son compatibles.")); return result;
      }
      if (name == "title" || name == "artist" || name == "key" || name == "subtitle" || name == "time") {
        if (!singleLineMetadata(value)) { fail(index, "Metadato no compatible."); return result; }
        repeatedMetadata(name);
        if (name == "title") result.metadata.title = value;
        else if (name == "artist") result.metadata.artist = value;
        else if (name == "key") result.metadata.key = value;
        else if (name == "subtitle") result.metadata.subtitle = value;
        else result.metadata.timeSignature = value;
        continue;
      }
      if (name == "tempo") {
        double tempo = 0.0;
        if (!TempoValue::parseBpm(value, tempo)) { fail(index, juce::String::fromUTF8("El tempo debe ser un número positivo.")); return result; }
        repeatedMetadata(name);
        result.metadata.tempoBpm = tempo;
        continue;
      }
      if (name.startsWith("start_of_")) {
        const auto kind = canonicalEnvironment(name.substring(9));
        if (kind.isEmpty()) { fail(index, juce::String::fromUTF8("Esta sección ChordPro todavía no es compatible: ") + name); return result; }
        if (!supportedEnvironmentLabel(value)) {
          fail(index, juce::String::fromUTF8("Los atributos de esta sección aún no son compatibles.")); return result;
        }
        if (active && active->paired) { fail(index, juce::String::fromUTF8("Cierra la sección anterior antes de abrir otra.")); return result; }
        closeSection();
        active = ActiveSection{kind, -1, -1, true};
        if (value.isNotEmpty()) labelWarning();
        continue;
      }
      if (name.startsWith("end_of_")) {
        if (value.isNotEmpty()) { fail(index, juce::String::fromUTF8("El cierre de sección no admite atributos.")); return result; }
        const auto kind = canonicalEnvironment(name.substring(7));
        if (!active || !active->paired || active->kind != kind) {
          fail(index, juce::String::fromUTF8("El cierre no coincide con la sección abierta.")); return result;
        }
        closeSection();
        continue;
      }
      if (name == "comment") {
        if (active && active->paired) { fail(index, juce::String::fromUTF8("Los comentarios dentro de una sección aún no son compatibles.")); return result; }
        closeSection();
        if (value.isEmpty()) continue;
        const auto kind = headingKind(value);
        if (kind.isEmpty()) { fail(index, juce::String::fromUTF8("Este comentario no es un encabezado de sección compatible.")); return result; }
        active = ActiveSection{kind, -1, -1, false};
        labelWarning();
        continue;
      }
      fail(index, "Directiva ChordPro no compatible: " + name + juce::String::fromUTF8(". No se importó el documento."));
      return result;
    }
    if (grid) {
      SongLyricsInstrumentalRow row;
      if (!parseGridRow(line, row)) {
        fail(index, juce::String::fromUTF8("El grid requiere filas | C . | G . |; no admite notas de margen, repeticiones ni otros símbolos.")); return result;
      }
      const auto width = static_cast<int>(row.cells.size());
      if (!grid->width) grid->width = width;
      if (*grid->width != width) {
        fail(index, juce::String::fromUTF8("Todas las filas del grid deben tener el mismo número de celdas que shape.")); return result;
      }
      totalGridCells += row.cells.size();
      for (const auto &cell : row.cells) if (cell.symbol) ++totalChordCount;
      if (totalChordCount > kSongLyricsMaxChords) {
        fail(index, "La letra y los grids superan la cantidad de acordes permitida."); return result;
      }
      grid->block.rows.push_back(std::move(row));
      if (grid->block.rows.size() > kSongLyricsMaxInstrumentalRowsPerBlock || totalGridCells > kSongLyricsMaxInstrumentalCells) {
        fail(index, "El grid supera la cantidad de filas o celdas permitida."); return result;
      }
      continue;
    }
    if (line.containsAnyOf("{}<>") || trimmed.endsWithChar('\\')) {
      fail(index, juce::String::fromUTF8("El marcado o las líneas continuadas aún no son compatibles.")); return result;
    }
    if (hadLyricLine) { document.text += "\n"; ++units; }
    anchorPendingBlocks(units);
    if (active && active->start < 0) active->start = units;
    juce::String plain;
    int lineUnits = 0;
    for (int character = 0; character < line.length(); ++character) {
      const auto value = static_cast<unsigned int>(line[character]);
      if (value == '[') {
        const int end = line.indexOfChar(character + 1, ']');
        if (end < 0) { fail(index, "Falta cerrar el acorde con ]."); return result; }
        const auto symbol = line.substring(character + 1, end).trim();
        if (symbol.containsChar('[') || !isValidSongLyricsChordSymbol(symbol)) {
          fail(index, "Acorde no compatible: " + symbol); return result;
        }
        if (!document.chords.empty() && document.chords.back().offset == units + lineUnits) {
          fail(index, "Separa con espacios los acordes instrumentales consecutivos."); return result;
        }
        if (++totalChordCount > kSongLyricsMaxChords) {
          fail(index, "La letra y los grids superan la cantidad de acordes permitida."); return result;
        }
        document.chords.push_back({juce::Uuid().toString(), units + lineUnits, symbol});
        character = end;
        continue;
      }
      if (value == ']') { fail(index, "Hay un corchete de cierre sin acorde."); return result; }
      plain += juce::String::charToString(static_cast<juce::juce_wchar>(value));
      lineUnits += value > 0xffffu ? 2 : 1;
    }
    document.text += plain;
    units += lineUnits;
    hadLyricLine = true;
    if (active) active->end = units;
    if (units > kSongLyricsMaxUtf16Length || document.chords.size() > kSongLyricsMaxChords ||
        document.sections.size() > kSongLyricsMaxSections) {
      fail(index, juce::String::fromUTF8("La letra o sus marcas superan el tamaño permitido.")); return result;
    }
  }
  if (grid) { fail(lines.size() - 1, juce::String::fromUTF8("Falta cerrar el último grid instrumental.")); return result; }
  if (active && active->paired) { fail(lines.size() - 1, juce::String::fromUTF8("Falta cerrar la última sección.")); return result; }
  anchorPendingBlocks(units);
  closeSection();
  if (document.sections.size() > kSongLyricsMaxSections) {
    result.error = "La letra supera la cantidad de secciones permitida.";
    return result;
  }
  normalizeSongLyricsSchema(document);
  if (!hasSongLyrics(document)) {
    result.error = juce::String::fromUTF8("No se encontró letra, acordes ni grids para importar.");
    return result;
  }
  if (!isValidSongLyricsDocument(document)) {
    result.error = juce::String::fromUTF8("La letra, los acordes o los grids no tienen posiciones o tamaños válidos.");
    return result;
  }
  result.document = std::move(document);
  return result;
}

juce::String exportText(const SongLyricsDocument &document, const Metadata &metadata,
                         juce::String *error) {
  const auto fail = [&](const juce::String &message) {
    if (error) *error = message;
    return juce::String();
  };
  if (error) error->clear();
  if (!isEditableSongLyricsDocument(document) || !isValidSongLyricsDocument(document) || !hasSongLyrics(document))
    return fail(juce::String::fromUTF8("La letra no es válida o usa una versión que esta aplicación no puede editar."));
  for (const auto &value : {metadata.title, metadata.artist, metadata.key, metadata.subtitle, metadata.timeSignature})
    if (!singleLineMetadata(value)) return fail(juce::String::fromUTF8("Los metadatos deben ocupar una sola línea sin llaves."));
  if (metadata.tempoBpm && (!std::isfinite(*metadata.tempoBpm) || *metadata.tempoBpm <= 0.0))
    return fail(juce::String::fromUTF8("El tempo del archivo debe ser un número positivo."));
  if (document.text.containsAnyOf("[]{}<>") || document.text.contains("\\\n"))
    return fail(juce::String::fromUTF8("Esta letra contiene caracteres reservados que el exportador básico todavía no puede representar."));
  const auto lines = lyricLines(document.text);
  for (const auto &line : lines)
    if (line.text.trim().startsWithChar('#') || line.text.trim().endsWithChar('\\'))
      return fail(juce::String::fromUTF8("Una línea de letra se interpretaría como comentario o continuación ChordPro."));
  struct SectionLines { int first = 0, last = 0; juce::String kind; };
  std::vector<SectionLines> sections;
  for (const auto &section : document.sections) {
    int first = -1, last = -1;
    const int end = section.start + section.length;
    for (int i = 0; i < static_cast<int>(lines.size()); ++i) {
      if (lines[static_cast<size_t>(i)].start == section.start) first = i;
      const auto &line = lines[static_cast<size_t>(i)];
      // A section ending only on a separator would lose that code unit when
      // import closes the environment. It is representable only if an actual
      // empty lyric line also ends at that position (including a final empty line).
      if (end == line.end) last = i;
    }
    if (first < 0 || last < first)
      return fail(juce::String::fromUTF8("Para exportar ChordPro, las secciones deben abarcar líneas completas sin un salto final no representable."));
    sections.push_back({first, last, section.kind});
  }
  std::sort(sections.begin(), sections.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
  for (size_t i = 1; i < sections.size(); ++i)
    if (sections[i].first <= sections[i - 1].last)
      return fail(juce::String::fromUTF8("Dos secciones comparten una línea; sepáralas antes de exportar ChordPro."));
  auto blocks = document.instrumentalBlocks;
  std::sort(blocks.begin(), blocks.end(), [](const auto &a, const auto &b) {
    return a.anchorUtf16 < b.anchorUtf16 || (a.anchorUtf16 == b.anchorUtf16 && a.order < b.order);
  });
  for (const auto &block : blocks) {
    if (gridKindForLabel(block.label) != block.kind || block.label.containsAnyOf("{}<>\\\"'"))
      return fail("El tipo y la etiqueta del instrumental no se pueden representar sin cambios: usa Intro, Solo o Instrumental.");
    const auto width = block.rows.front().cells.size();
    for (const auto &row : block.rows)
      if (row.cells.size() != width)
        return fail(juce::String::fromUTF8("Para exportar el grid, sus filas deben tener el mismo número de celdas."));
  }
  auto chords = document.chords;
  std::sort(chords.begin(), chords.end(), [](const auto &a, const auto &b) { return a.offset < b.offset; });
  juce::String output;
  const auto addMetadata = [&](const char *name, const juce::String &value) {
    if (value.isNotEmpty()) output += "{" + juce::String(name) + ": " + value + "}\n";
  };
  addMetadata("title", metadata.title); addMetadata("artist", metadata.artist);
  addMetadata("subtitle", metadata.subtitle); addMetadata("key", metadata.key);
  if (metadata.tempoBpm) addMetadata("tempo", TempoValue::formatBpm(*metadata.tempoBpm));
  addMetadata("time", metadata.timeSignature);
  size_t blockIndex = 0;
  const auto emitBlocks = [&](int anchor) {
    while (blockIndex < blocks.size() && blocks[blockIndex].anchorUtf16 == anchor) {
      const auto &block = blocks[blockIndex++];
      output += "{start_of_grid shape=\"" + juce::String(static_cast<int>(block.rows.front().cells.size())) + "\"";
      if (block.label.isNotEmpty()) output += " label=\"" + block.label + "\"";
      output += "}\n";
      for (const auto &row : block.rows) {
        output += "|";
        size_t cellIndex = 0;
        const auto emitBar = [&](int cellCount) {
          for (int i = 0; i < cellCount; ++i) {
            const auto &cell = row.cells[cellIndex++];
            output += " " + (cell.symbol ? *cell.symbol : juce::String("."));
          }
          output += " |";
        };
        if (row.barLengths.empty()) emitBar(static_cast<int>(row.cells.size()));
        else for (const auto length : row.barLengths) emitBar(length);
        output += "\n";
      }
      output += "{end_of_grid}\n";
    }
  };
  size_t chordIndex = 0;
  for (int lineIndex = 0; lineIndex < static_cast<int>(lines.size()); ++lineIndex) {
    emitBlocks(lines[static_cast<size_t>(lineIndex)].start);
    const auto section = std::find_if(sections.begin(), sections.end(), [&](const auto &s) { return s.first == lineIndex; });
    if (section != sections.end()) output += "{start_of_" + section->kind + "}\n";
    const auto &line = lines[static_cast<size_t>(lineIndex)];
    int units = line.start;
    for (int character = 0; character <= line.text.length(); ++character) {
      while (chordIndex < chords.size() && chords[chordIndex].offset == units) {
        output += "[" + chords[chordIndex].symbol + "]";
        ++chordIndex;
      }
      if (character == line.text.length()) break;
      const auto value = static_cast<unsigned int>(line.text[character]);
      output += juce::String::charToString(static_cast<juce::juce_wchar>(value));
      units += value > 0xffffu ? 2 : 1;
    }
    output += "\n";
    const auto closing = std::find_if(sections.begin(), sections.end(), [&](const auto &s) { return s.last == lineIndex; });
    if (closing != sections.end()) output += "{end_of_" + closing->kind + "}\n";
  }
  emitBlocks(songLyricsUtf16Length(document.text));
  if (blockIndex != blocks.size()) return fail("No se pudieron representar todos los bloques instrumentales.");
  if (chordIndex != chords.size()) return fail("No se pudieron representar todos los acordes.");
  return output;
}
} // namespace ChordProCodec
