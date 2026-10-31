#include "FreeShowModel.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <set>

namespace playworship::lyrics
{
namespace
{
juce::var object() { return juce::var(new juce::DynamicObject()); }
void put(juce::var& v, const char* key, const juce::var& value) { v.getDynamicObject()->setProperty(key, value); }
bool textField(const juce::var& v, const char* key, juce::String& out, bool required = false, int max = 16384)
{
    const auto p = v.getProperty(key, {});
    if (!p.isString() || p.toString().length() > max || (required && p.toString().isEmpty())) return false;
    out = p.toString(); return true;
}
bool integer(const juce::var& v) { return v.isInt() || v.isInt64(); }
bool number(const juce::var& v) { return integer(v) || v.isDouble(); }
juce::var targetVar(const Target& t)
{
    auto v = object();
    put(v, "hide", t.hide); put(v, "showId", t.showId); put(v, "layoutId", t.layoutId);
    put(v, "groupId", t.groupId); put(v, "slideId", t.slideId); put(v, "occurrence", t.occurrence);
    put(v, "layoutSignature", t.layoutSignature); put(v, "title", t.title); put(v, "text", t.text);
    if (t.useActiveLayout) put(v, "useActiveLayout", true);
    return v;
}
bool readTarget(const juce::var& v, Target& t)
{
    if (!v.isObject() || !v.getProperty("hide", {}).isBool()) return false;
    t.hide = (bool) v.getProperty("hide", false);
    if (v.hasProperty("useActiveLayout") && !v.getProperty("useActiveLayout", {}).isBool()) return false;
    t.useActiveLayout = (bool) v.getProperty("useActiveLayout", false);
    if (!textField(v, "showId", t.showId, !t.hide, 512) || !textField(v, "layoutId", t.layoutId, !t.hide && !t.useActiveLayout, 512)
        || !textField(v, "groupId", t.groupId, !t.hide, 512) || !textField(v, "slideId", t.slideId, !t.hide, 512)
        || !textField(v, "layoutSignature", t.layoutSignature, !t.hide && !t.useActiveLayout, 512)
        || !textField(v, "title", t.title) || !textField(v, "text", t.text)) return false;
    const auto occurrence = v.getProperty("occurrence", {});
    if (!integer(occurrence) || (juce::int64) occurrence < 1 || (juce::int64) occurrence > 10000) return false;
    t.occurrence = (int) occurrence;
    return true;
}
juce::String slideText(const juce::var& slide)
{
    juce::StringArray lines;
    const auto itemsValue = slide.getProperty("items", {});
    if (auto* items = itemsValue.getArray())
        for (const auto& item : *items)
        {
            if (item.getProperty("type", "text").toString() != "text") continue;
            const auto linesValue = item.getProperty("lines", {});
            if (auto* itemLines = linesValue.getArray())
                for (const auto& line : *itemLines)
                {
                    juce::String text;
                    const auto fragmentsValue = line.getProperty("text", {});
                    if (auto* fragments = fragmentsValue.getArray())
                        for (const auto& fragment : *fragments) text += fragment.getProperty("value", {}).toString();
                    if (text.isNotEmpty()) lines.add(text);
                }
        }
    return lines.joinIntoString("\n").substring(0, 16384);
}
std::vector<juce::String> childIds(const juce::var& parent)
{
    std::vector<juce::String> ids;
    const auto childrenValue = parent.getProperty("children", {});
    if (auto* children = childrenValue.getArray())
        for (const auto& child : *children)
        {
            if (child.isString()) ids.push_back(child.toString());
            else if (auto* nested = child.getArray())
                for (const auto& id : *nested) if (id.isString()) ids.push_back(id.toString());
        }
    return ids;
}
}

juce::var unwrapResponse(const juce::var& v)
{
    return v.hasProperty("action") && v.hasProperty("data") ? v.getProperty("data", {}) : v;
}
juce::var toVar(const SongLyrics& song)
{
    auto v = object(); put(v, "version", 1); put(v, "enabled", song.enabled);
    put(v, "showId", song.showId); put(v, "showName", song.showName);
    put(v, "layoutId", song.layoutId); put(v, "layoutName", song.layoutName);
    juce::Array<juce::var> marks;
    for (const auto& m : song.marks)
    {
        auto value = object(); put(value, "id", m.id); put(value, "seconds", m.seconds);
        put(value, "enabled", m.enabled); put(value, "target", targetVar(m.target)); marks.add(value);
    }
    put(v, "marks", marks); return v;
}
bool fromVar(const juce::var& v, SongLyrics& out, juce::String* error)
{
    const auto fail = [&] { if (error) *error = juce::String::fromUTF8("Marcas de letra inválidas; se conservaron los datos anteriores"); return false; };
    if (v.isVoid() || v.isUndefined()) { out = {}; if (error) error->clear(); return true; }
    if (!v.isObject() || !integer(v.getProperty("version", {})) || (juce::int64) v.getProperty("version", {}) != 1
        || !v.getProperty("enabled", {}).isBool()) return fail();
    SongLyrics song; song.enabled = (bool) v.getProperty("enabled", false);
    if (!textField(v, "showId", song.showId, false, 512) || !textField(v, "showName", song.showName)
        || !textField(v, "layoutId", song.layoutId, false, 512) || !textField(v, "layoutName", song.layoutName)) return fail();
    const auto marksValue = v.getProperty("marks", {});
    const auto* marks = marksValue.getArray();
    if (marks == nullptr || marks->size() > 1024) return fail();
    std::set<juce::String> seen;
    for (const auto& value : *marks)
    {
        Mark mark;
        const auto time = value.getProperty("seconds", {});
        if (!value.isObject() || !textField(value, "id", mark.id, true, 512) || !seen.insert(mark.id).second
            || !number(time) || !std::isfinite((double) time) || (double) time < 0.0 || (double) time > 604800.0
            || !value.getProperty("enabled", {}).isBool() || !readTarget(value.getProperty("target", {}), mark.target)) return fail();
        mark.seconds = (double) time; mark.enabled = (bool) value.getProperty("enabled", true);
        song.marks.push_back(std::move(mark));
    }
    std::stable_sort(song.marks.begin(), song.marks.end(), [](const auto& a, const auto& b) { return a.seconds < b.seconds; });
    out = std::move(song); if (error) error->clear(); return true;
}
std::vector<ShowSummary> parseShowList(const juce::var& response)
{
    const auto data = unwrapResponse(response);
    std::vector<ShowSummary> result;
    if (auto* obj = data.getDynamicObject())
        for (const auto& entry : obj->getProperties())
            if (entry.value.isObject() && entry.value.getProperty("name", {}).isString())
                result.push_back({entry.name.toString(), entry.value.getProperty("name", {}).toString()});
    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) { return a.name.compareNatural(b.name) < 0; });
    return result;
}
ShowCatalog parseShow(const juce::String& showId, const juce::var& response, const juce::var& referenceResponse)
{
    ShowCatalog catalog; catalog.id = showId;
    const auto fail = [&](const juce::String& error) { catalog.error = error; catalog.layouts.clear(); return catalog; };
    const auto show = unwrapResponse(response);
    const auto slides = show.getProperty("slides", {});
    const auto layoutsValue = show.getProperty("layouts", {});
    if (showId.isEmpty() || !show.isObject() || !slides.isObject() || !layoutsValue.isObject())
        return fail(juce::String::fromUTF8("FreeShow no devolvió un show completo"));
    catalog.name = show.getProperty("name", showId).toString();
    catalog.activeLayoutId = show.getProperty("settings", {}).getProperty("activeLayout", {}).toString();
    // The creation menu represents show groups, not occurrences in an arrangement.
    std::set<juce::String> children;
    for (const auto &entry : slides.getDynamicObject()->getProperties())
        for (const auto &id : childIds(entry.value)) children.insert(id);
    size_t groupSlideCount = 0;
    for (const auto &entry : slides.getDynamicObject()->getProperties())
    {
        const auto groupId = entry.name.toString();
        if (children.count(groupId) != 0) continue;
        SlideGroup group; group.id = groupId;
        group.name = entry.value.getProperty("group", groupId).toString();
        auto ids = childIds(entry.value); ids.insert(ids.begin(), groupId);
        std::set<juce::String> unique;
        for (const auto &id : ids)
        {
            const auto slide = slides.getProperty(id, {});
            if (!slide.isObject() || !unique.insert(id).second)
                return fail("Pantalla ausente o repetida en grupo; revisar FreeShow");
            if (++groupSlideCount > 100000) return fail("Demasiadas pantallas FreeShow");
            SlideDestination destination;
            destination.target.showId = showId; destination.target.groupId = groupId;
            destination.target.slideId = id; destination.target.title = group.name;
            destination.target.text = slideText(slide); destination.target.useActiveLayout = true;
            destination.screen = static_cast<int>(group.slides.size()) + 1;
            group.slides.push_back(std::move(destination));
        }
        catalog.groups.push_back(std::move(group));
    }
    const auto reference = unwrapResponse(referenceResponse);
    // The app-wide optional blank-first-slide setting is observable through get_show_layout.
    const bool prependBlank = reference.isArray() && reference.size() > 0 && reference[0].getProperty("id", {}).toString() == "fake_empty";
    const auto* layouts = layoutsValue.getDynamicObject();
    if (layouts->getProperties().size() > 1000) return fail("Demasiados arreglos FreeShow");
    size_t destinationCount = 0;
    bool observedActive = false;
    for (const auto& item : layouts->getProperties())
    {
        Layout layout; layout.id = item.name.toString(); layout.name = item.value.getProperty("name", layout.id).toString();
        const auto orderValue = item.value.getProperty("slides", {});
        const auto* order = orderValue.getArray();
        if (order == nullptr || order->size() > 10000) return fail(juce::String::fromUTF8("Orden de arreglo FreeShow inválido"));
        std::map<juce::String, int> occurrences;
        int absolute = 1;
        juce::String signature;
        // A synthetic blank has no stable slide ID and cannot be a cue destination.
        if (prependBlank && !order->isEmpty() && slideText(slides.getProperty((*order)[0].getProperty("id", {}).toString(), {})).isNotEmpty()) absolute++;
        for (const auto& group : *order)
        {
            const auto groupId = group.getProperty("id", {}).toString();
            const auto parent = slides.getProperty(groupId, {});
            if (groupId.isEmpty() || !parent.isObject()) return fail("El arreglo referencia un grupo ausente; revisar FreeShow");
            const int occurrence = ++occurrences[groupId];
            const auto groupName = parent.getProperty("group", groupId).toString();
            auto ids = childIds(parent); ids.insert(ids.begin(), groupId);
            std::set<juce::String> unique;
            int screen = 0;
            for (const auto& slideId : ids)
            {
                const auto slide = slides.getProperty(slideId, {});
                if (!slide.isObject() || !unique.insert(slideId).second) return fail("Pantalla ausente o repetida en grupo; revisar FreeShow");
                SlideDestination destination;
                destination.target.showId = showId; destination.target.layoutId = layout.id;
                destination.target.groupId = groupId; destination.target.slideId = slideId;
                destination.target.occurrence = occurrence; destination.target.title = groupName;
                destination.target.text = slideText(slide); destination.screen = ++screen; destination.absoluteIndex = absolute++;
                destination.displayName = groupName + juce::String::fromUTF8(" · aparición ") + juce::String(occurrence)
                    + juce::String::fromUTF8(" · pantalla ") + juce::String(screen) + juce::String::fromUTF8(" · #") + juce::String(destination.absoluteIndex)
                    + " [" + groupId + "/" + slideId + "]";
                signature += juce::String(groupId.length()) + ":" + groupId + "/" + juce::String(occurrence)
                    + "/" + juce::String(slideId.length()) + ":" + slideId + ";";
                if (++destinationCount > 100000 || layout.slides.size() >= 10000) return fail("Demasiadas pantallas FreeShow");
                layout.slides.push_back(std::move(destination));
            }
        }
        signature = juce::String(absolute > (int) layout.slides.size() + 1 ? "blank;" : "") + signature;
        layout.signature = juce::String::toHexString(signature.hashCode64());
        for (auto& destination : layout.slides) destination.target.layoutSignature = layout.signature;
        if (layout.id == catalog.activeLayoutId && reference.isArray())
        {
            observedActive = true;
            const int offset = layout.slides.empty() ? 0 : layout.slides.front().absoluteIndex - 1;
            if (reference.size() != (int) layout.slides.size() + offset) return fail(juce::String::fromUTF8("El orden real de FreeShow cambió; actualizar y revisar destinos"));
            for (const auto& destination : layout.slides)
                if (reference[destination.absoluteIndex - 1].getProperty("id", {}).toString() != destination.target.slideId)
                    return fail("El orden real de FreeShow no coincide; revisar destinos");
        }
        catalog.layouts.push_back(std::move(layout));
    }
    if (reference.isArray() && !observedActive) return fail("Arreglo activo ausente; revisar FreeShow");
    if (catalog.layouts.empty()) return fail("El show no contiene arreglos");
    return catalog;
}
Resolution resolve(const Target& target, const ShowCatalog& catalog)
{
    if (target.hide) return {true, 0, {}};
    if (!catalog.ok() || target.showId != catalog.id) return {false, 0, "Show no disponible; revisar destino"};
    if (target.useActiveLayout)
    {
        for (const auto &layout : catalog.layouts)
            if (layout.id == catalog.activeLayoutId)
            {
                auto current = target;
                current.useActiveLayout = false; current.layoutId = layout.id;
                current.layoutSignature = layout.signature; current.occurrence = 1;
                const auto resolved = resolve(current, catalog);
                if (!resolved.valid)
                    return {false, 0, juce::String::fromUTF8("La diapositiva no está en el arreglo activo de FreeShow; revisar destino")};
                return resolved;
            }
        return {false, 0, "Arreglo activo ausente; revisar FreeShow"};
    }
    for (const auto& layout : catalog.layouts)
        if (layout.id == target.layoutId)
        {
            if (target.layoutSignature.isEmpty() || target.layoutSignature != layout.signature)
                return {false, 0, juce::String::fromUTF8("El arreglo cambió; volver a elegir el destino")};
            const SlideDestination* found = nullptr;
            for (const auto& slide : layout.slides)
                if (slide.target.groupId == target.groupId && slide.target.slideId == target.slideId && slide.target.occurrence == target.occurrence)
                {
                    if (found != nullptr) return {false, 0, "Destino ambiguo; revisar"};
                    found = &slide;
                }
            if (found) return {true, found->absoluteIndex, {}};
            return {false, 0, juce::String::fromUTF8("Pantalla o aparición ausente; revisar destino")};
        }
    return {false, 0, "Arreglo ausente; revisar destino"};
}
juce::String targetLabel(const Target& target)
{
    return target.hide ? "Ocultar letras" : target.title + juce::String::fromUTF8(" · aparición ") + juce::String(target.occurrence) + " [" + target.slideId.substring(0, 8) + "]";
}
juce::var makeSelectRequest(const Target& target, int absoluteIndex)
{
    auto v = object(); put(v, "action", target.hide ? "clear_slide" : "index_select_slide");
    if (!target.hide) { put(v, "showId", target.showId); put(v, "layoutId", target.layoutId); put(v, "index", absoluteIndex); }
    return v;
}
bool outputMatches(const Target& target, int absoluteIndex, const juce::var& response)
{
    const auto output = unwrapResponse(response);
    const auto slide = output.getProperty("slide", {});
    if (target.hide) return output.isObject() && output.hasProperty("slide") && slide.isVoid();
    return slide.getProperty("id", {}).toString() == target.showId
        && slide.getProperty("layout", {}).toString() == target.layoutId
        && integer(slide.getProperty("index", {})) && (int) slide.getProperty("index", -1) == absoluteIndex - 1;
}
}
