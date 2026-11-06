#include "HolyricsModel.h"
#include <map>
#include <algorithm>
namespace playworship::lyrics {
namespace {
juce::var data(const juce::var& value) { return value.getProperty("data", {}); }
}
std::vector<ShowSummary> parseHolyricsSongs(const juce::var& response) {
  std::vector<ShowSummary> songs;
  const auto value = data(response);
  if (const auto* array = value.getArray()) for (const auto& song : *array) {
    const auto id = song.getProperty("id", {}).toString();
    const auto title = song.getProperty("title", {}).toString();
    if (id.isNotEmpty() && title.isNotEmpty()) songs.push_back({id, title});
  }
  std::sort(songs.begin(), songs.end(), [](const auto& a, const auto& b) { return a.name.compareNatural(b.name) < 0; });
  return songs;
}
ShowCatalog parseHolyricsSong(const juce::String& id, const juce::var& response) {
  ShowCatalog result; result.provider = "holyrics"; result.id = id;
  auto fail = [&](const juce::String& error) { result.error = error; result.layouts.clear(); result.groups.clear(); return result; };
  const auto song = data(response);
  const auto slidesValue = song.getProperty("slides", {});
  const auto* slides = slidesValue.getArray();
  if (!song.isObject() || song.getProperty("id", {}).toString() != id || !slides || slides->isEmpty() || slides->size() > 10000)
    return fail("Holyrics no devolvió una canción con diapositivas compatibles");
  result.name = song.getProperty("title", id).toString();
  result.activeLayoutId = "holyrics-order";
  Layout layout; layout.id = result.activeLayoutId; layout.name = "Orden de Holyrics";
  auto order = juce::StringArray::fromTokens(song.getProperty("order", {}).toString(), ",", "");
  order.trim(); order.removeEmptyStrings();
  if (order.isEmpty()) for (int i = 0; i < slides->size(); ++i) order.add(juce::String(i + 1));
  if (order.size() > 10000) return fail("Demasiadas diapositivas Holyrics");
  std::map<juce::String, int> occurrences;
  juce::String signature;
  for (const auto& item : order) {
    if (!item.containsOnly("0123456789")) return fail("Orden Holyrics inválido");
    const int index = item.getIntValue() - 1;
    if (index < 0 || index >= slides->size()) return fail("Diapositiva ausente en Holyrics");
    const auto& slide = slides->getReference(index);
    const auto text = slide.getProperty("text", {}).toString();
    auto description = slide.getProperty("slide_description", {}).toString();
    if (description.isEmpty()) description = "Diapositivas";
    const auto identity = juce::String::toHexString((description + "\n" + text).hashCode64());
    SlideDestination destination;
    destination.target.provider = "holyrics"; destination.target.showId = id;
    destination.target.layoutId = layout.id; destination.target.groupId = description;
    destination.target.slideId = identity; destination.target.text = text.substring(0, 16384);
    destination.target.title = description; destination.target.occurrence = ++occurrences[identity];
    destination.absoluteIndex = static_cast<int>(layout.slides.size()) + 1;
    signature += item + ":" + identity + ";";
    layout.slides.push_back(destination);
  }
  layout.signature = juce::String::toHexString(signature.hashCode64());
  for (auto& slide : layout.slides) {
    slide.target.layoutSignature = layout.signature;
    auto group = std::find_if(result.groups.begin(), result.groups.end(), [&](const auto& g) { return g.id == slide.target.groupId; });
    if (group == result.groups.end()) { result.groups.push_back({slide.target.groupId, slide.target.title, {}}); group = result.groups.end() - 1; }
    slide.screen = slide.absoluteIndex;
    slide.displayName = "Diapositiva " + juce::String(slide.absoluteIndex);
    group->slides.push_back(slide);
  }
  result.layouts.push_back(std::move(layout));
  return result;
}
bool holyricsOutputMatches(const Target& target, int index, const juce::var& response) {
  const auto output = data(response);
  if (target.hide) return output.isObject() && output.getProperty("slide_type", {}).toString() == "wallpaper";
  return output.isObject() && output.getProperty("type", {}).toString() == "song"
      && output.getProperty("song_id", output.getProperty("id", {})).toString() == target.showId
      && output.getProperty("slide_type", {}).toString() == "default"
      && static_cast<int>(output.getProperty("slide_number", -1)) == index;
}
}
