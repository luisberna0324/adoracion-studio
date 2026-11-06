#include "integrations/FreeShowClient.h"
#include "integrations/HolyricsModel.h"
#include <atomic>
#include <chrono>
#include <future>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace
{
using namespace playworship::lyrics;
void expect(bool pass, const char* message) { if (!pass) throw std::runtime_error(message); }
juce::var fixture()
{
    return juce::JSON::parse(juce::String::fromUTF8(R"json({"name":"Canción propia de prueba","settings":{"activeLayout":"main"},"slides":{
      "coro":{"group":"Coro","children":["coro-child"],"items":[{"type":"text","lines":[{"text":[{"value":"Tu luz nos "},{"value":"guía"}]},{"text":[{"value":"Cantamos hoy"}]}]}]},
      "coro-child":{"group":null,"items":[{"type":"text","lines":[{"text":[{"value":"En cada paso"}]}]}]},
      "coro-literal-2":{"group":"Coro2","items":[{"type":"text","lines":[{"text":[{"value":"Nombre literal distinto"}]}]}]}
    },"layouts":{"main":{"name":"Principal","slides":[{"id":"coro"},{"id":"coro-literal-2"},{"id":"coro"}]},"alt":{"name":"Alternativo","slides":[{"id":"coro-literal-2"},{"id":"coro"}]}}})json"));
}
juce::var envelope(juce::String name, juce::var data)
{
    auto* v = new juce::DynamicObject(); v->setProperty("action", name); v->setProperty("data", data); return juce::var(v);
}
juce::var layoutRef(const ShowCatalog& show)
{
    juce::Array<juce::var> values;
    for (const auto& layout : show.layouts) if (layout.id == show.activeLayoutId)
        for (const auto& slide : layout.slides)
        { auto* obj = new juce::DynamicObject(); obj->setProperty("id", slide.target.slideId); values.add(juce::var(obj)); }
    return values;
}
const Layout& mainLayout(const ShowCatalog& show)
{
    for (const auto& layout : show.layouts) if (layout.id == "main") return layout;
    throw std::runtime_error("missing main layout");
}
void modelContracts()
{
    const auto catalog = parseShow("test-show", envelope("get_show", fixture()));
    expect(catalog.ok(), "complete show parses");
    expect(catalog.groups.size() == 2 && catalog.groups.front().slides.size() == 2,
           "show menu groups deduplicate arrangement repetitions and preserve child slides");
    const auto showTarget = catalog.groups.front().slides[1].target;
    expect(showTarget.useActiveLayout && showTarget.layoutId.isEmpty()
           && resolve(showTarget, catalog).absoluteIndex == 2,
           "new show-bound marks resolve exact slide IDs in the active layout");
    auto alternative = fixture();
    alternative.getDynamicObject()->getProperty("settings").getDynamicObject()->setProperty("activeLayout", "alt");
    expect(resolve(showTarget, parseShow("test-show", alternative)).absoluteIndex == 3,
           "active-layout changes resolve the slide again rather than reuse an index");
    auto unused = fixture();
    unused.getDynamicObject()->getProperty("slides").getDynamicObject()->setProperty(
        "unused-group", juce::JSON::parse(R"({"group":"Puente","items":[]})"));
    const auto unusedCatalog = parseShow("test-show", unused);
    expect(unusedCatalog.groups.size() == 3
           && !resolve(unusedCatalog.groups.back().slides.front().target, unusedCatalog).valid,
           "unused show groups remain selectable but cannot project an unrelated active index");
    const auto& layout = mainLayout(catalog);
    expect(layout.slides.size() == 5, "groups expand children and repeated occurrence");
    expect(layout.slides[1].target.slideId == "coro-child" && layout.slides[1].screen == 2 && layout.slides[1].absoluteIndex == 2,
           "screen 2 is an absolute expanded slide");
    expect(layout.slides[2].target.title == "Coro2" && layout.slides[2].target.occurrence == 1,
           "literal Coro2 is not occurrence or screen 2");
    expect(layout.slides[4].target.occurrence == 2 && layout.slides[4].screen == 2 && resolve(layout.slides[4].target, catalog).absoluteIndex == 5,
           "second occurrence screen 2 uses absolute index five");
    expect(layout.slides[4].displayName.contains(juce::String::fromUTF8("aparición 2")) && layout.slides[4].displayName.contains("pantalla 2"),
           "picker shows occurrence and screen explicitly");
    expect(layout.slides[0].target.text == juce::String::fromUTF8("Tu luz nos guía\nCantamos hoy"), "real API text joins fragments and lines");
    const auto request = makeSelectRequest(layout.slides[4].target, 5);
    expect(request.getProperty("action", {}).toString() == "index_select_slide" && (int) request.getProperty("index", {}) == 5
           && request.getProperty("showId", {}).toString() == "test-show" && request.getProperty("layoutId", {}).toString() == "main",
           "projection is explicit absolute one-based selection with IDs");
    auto changed = fixture();
    changed.getDynamicObject()->getProperty("layouts").getDynamicObject()->getProperty("main").getDynamicObject()->setProperty("slides", juce::JSON::parse(R"([{"id":"coro"},{"id":"coro"},{"id":"coro-literal-2"}])"));
    expect(!resolve(layout.slides[4].target, parseShow("test-show", changed)).valid, "external reorder requires review instead of approximate matching");
    auto ref = layoutRef(catalog);
    expect(parseShow("test-show", fixture(), envelope("get_show_layout", ref)).ok(), "active expanded real reference is checked");
    ref[1].getDynamicObject()->setProperty("id", "wrong-slide");
    expect(!parseShow("test-show", fixture(), ref).ok(), "unexpected real layout fails closed");
    ref = layoutRef(catalog);
    auto* blank = new juce::DynamicObject(); blank->setProperty("id", "fake_empty"); ref.getArray()->insert(0, juce::var(blank));
    const auto blankCatalog = parseShow("test-show", fixture(), ref);
    expect(blankCatalog.ok() && mainLayout(blankCatalog).slides[0].absoluteIndex == 2, "blank-first FreeShow setting offsets real indices safely");
    expect(resolve(showTarget, blankCatalog).absoluteIndex == 3,
           "show-bound destinations honor the verified blank-first offset");
    SongLyrics lyrics; lyrics.enabled = true; lyrics.showId = catalog.id; lyrics.layoutId = "main";
    lyrics.marks.push_back({"cue-b", 15.0, true, layout.slides[4].target});
    Target hide; hide.hide = true; lyrics.marks.push_back({"cue-a", 0.0, true, hide});
    SongLyrics parsed; juce::String error;
    expect(fromVar(toVar(lyrics), parsed, &error) && parsed.marks[0].id == "cue-a" && parsed.marks[1].target.occurrence == 2,
           "roundtrip persists exact identities and musical positions");
    auto invalid = toVar(lyrics); invalid.getDynamicObject()->getProperty("marks")[0].getDynamicObject()->setProperty("seconds", -1.0);
    expect(!fromVar(invalid, parsed, &error) && parsed.marks.size() == 2 && parsed.marks[0].id == "cue-a", "invalid deserialize is atomic");
    invalid = toVar(lyrics); invalid.getDynamicObject()->getProperty("marks")[1].getDynamicObject()->setProperty("id", "cue-b");
    expect(!fromVar(invalid, parsed), "duplicate IDs reject");
    expect(fromVar({}, parsed) && !parsed.enabled && parsed.marks.empty(), "legacy missing lyrics remains compatible");
    SongLyrics showBound; showBound.showId = catalog.id;
    showBound.marks.push_back({"show-cue", 12.5, true, showTarget});
    expect(fromVar(toVar(showBound), parsed) && parsed.marks.front().target.useActiveLayout
           && parsed.marks.front().target.slideId == showTarget.slideId,
           "new binding persists alongside unchanged legacy targets without migration");
    auto* outputSlide = new juce::DynamicObject(); outputSlide->setProperty("id", "test-show"); outputSlide->setProperty("layout", "main"); outputSlide->setProperty("index", 4);
    auto* output = new juce::DynamicObject(); output->setProperty("slide", juce::var(outputSlide));
    const juce::var outputValue(output);
    expect(outputMatches(layout.slides[4].target, 5, envelope("get_output", outputValue)), "output confirmation translates zero-based observed index");
    expect(!outputMatches(layout.slides[1].target, 2, outputValue), "accepted command is not projected confirmation");
    expect(makeSelectRequest(hide, 0).getProperty("action", {}).toString() == "clear_slide" && outputMatches(hide, 0, envelope("get_output", juce::JSON::parse(R"({"slide":null,"transition":null})"))),
           "hide clears slide alone and confirms explicit cleared lyrics");
    expect(!outputMatches(hide, 0, envelope("get_output", {}))
           && !outputMatches(hide, 0, juce::JSON::parse("{}"))
           && !outputMatches(hide, 0, juce::JSON::parse(R"({"slide":"malformed"})"))
           && !outputMatches(hide, 0, juce::JSON::parse(R"({"slide":[]})")),
           "null whole output, missing slide and malformed slide cannot confirm hide");
}
void settingsContracts()
{
    for (const auto host : {"127.0.0.1", "localhost", "192.168.1.50", "10.2.3.4", "172.16.0.2", "172.31.8.3"})
        expect(FreeShowClient::validateSettings({host, 5506, {}}), "loopback and private IPv4 accepted");
    for (const auto host : {"example.org", "8.8.8.8", "169.254.169.254", "0.0.0.0", "192.168.1.255", "127.000.0.1", "127.0.0.1/path", "user@127.0.0.1", "127.0.0.1\r\nX:y", "192.168.1.3 ", "172.32.0.1"})
        expect(!FreeShowClient::validateSettings({host, 5506, {}}), "public DNS, metadata, injection and malformed addresses rejected");
    expect(!FreeShowClient::validateSettings({"localhost", 0, {}}) && !FreeShowClient::validateSettings({"localhost", 5506, "token\r\nInjected:x"}), "port and Bearer headers validated");
}
template<class Predicate> bool until(Predicate p, int timeout = 4000)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout);
    while (std::chrono::steady_clock::now() < deadline) { if (p()) return true; std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
    return p();
}
void holyricsContracts()
{
    const auto fixture = juce::JSON::parse(R"({"status":"ok","data":{"id":"song-1","title":"Cancion demo","order":"1,2,1","slides":[{"text":"Primera linea","slide_description":"Verso"},{"text":"Segunda linea","slide_description":"Coro"}]}})");
    auto catalog = parseHolyricsSong("song-1", fixture);
    expect(catalog.ok() && catalog.provider == "holyrics", "Holyrics catalogue parses without projection");
    expect(catalog.groups.size() == 2 && catalog.layouts[0].slides.size() == 3, "Holyrics preserves slide order and repetitions");
    const auto target = catalog.layouts[0].slides[2].target;
    expect(target.occurrence == 2 && resolve(target, catalog).absoluteIndex == 3, "Repeated slide resolves exact appearance");
    auto changed = fixture.clone();
    changed.getDynamicObject()->getProperty("data").getDynamicObject()->setProperty("order", "2,1");
    expect(!resolve(target, parseHolyricsSong("song-1", changed)).valid, "Changed Holyrics order cannot silently project a different slide");
    SongLyrics song; song.provider = "holyrics"; song.showId = "song-1";
    song.marks.push_back({"cue", 1.25, true, target});
    SongLyrics restored;
    expect(fromVar(toVar(song), restored) && restored.provider == "holyrics" && restored.marks[0].target.provider == "holyrics", "Provider identity round-trips through portable project model");
    auto wrong = target; wrong.provider = "freeshow";
    expect(!resolve(wrong, catalog).valid, "FreeShow targets cannot resolve in Holyrics");
    std::atomic<int> sends{0};
    std::atomic<bool> oldHolyrics{false};
    FreeShowClient client([&](const ConnectionSettings& settings, const juce::var& request) {
        expect(settings.provider == "holyrics", "Requests use configured Holyrics provider");
        const auto name = request.getProperty("action", {}).toString();
        if (name == "GetSongs") return HttpResponse{200, juce::JSON::parse(R"({"status":"ok","data":[{"id":"song-1","title":"Demo"}]})"), {}};
        if (name == "GetLyrics") return HttpResponse{200, fixture, {}};
        if (name == "GetVersion") return HttpResponse{200, juce::JSON::parse(oldHolyrics ? R"({"status":"ok","data":{"version":"2.20.0"}})" : R"({"status":"ok","data":{"version":"2.23.0"}})"), {}};
        if (name == "ShowLyrics") {
            expect(request.getProperty("id", {}).toString() == "song-1" && static_cast<int>(request.getProperty("initial_index", -1)) == 2, "Holyrics command uses song ID and zero-based exact index");
            ++sends; return HttpResponse{200, juce::JSON::parse(R"({"status":"ok"})"), {}};
        }
        if (name == "GetCurrentPresentation") return HttpResponse{200, juce::JSON::parse(R"({"status":"ok","data":{"type":"song","song_id":"song-1","slide_type":"default","slide_number":3}})"), {}};
        return HttpResponse{400, {}, "unexpected action"};
    });
    ConnectionSettings settings; settings.provider = "holyrics"; settings.port = 8091;
    client.configure(settings); client.testConnection();
    expect(until([&] { return client.getStatus().state == ConnectionStatus::State::connected; }), "Read-only Holyrics probe connects");
    expect(sends == 0, "Connection never projects a slide");
    client.queueTarget(wrong); client.queueTarget(target);
    expect(until([&] { return client.getStatus().outputConfirmed; }) && sends == 1, "Worker confirms Holyrics exact output and rejects foreign targets");
    oldHolyrics = true; client.queueTarget(target);
    expect(until([&] { return client.getStatus().message.contains("2.23.0"); }) && sends == 1,
           "Older Holyrics cannot silently ignore the requested slide index");
    client.disconnect();
}

void clientContracts()
{
    const auto catalog = parseShow("test-show", fixture());
    const auto target = mainLayout(catalog).slides[4].target;
    std::atomic<int> sends{0}; std::atomic<bool> online{true};
    std::mutex mutex; juce::var selected;
    FreeShowClient client([&](const ConnectionSettings&, const juce::var& request)
    {
        if (!online) return HttpResponse{0, {}, "fixture disconnected"};
        const auto actionName = request.getProperty("action", {}).toString();
        if (actionName == "get_shows") return HttpResponse{200, envelope(actionName, juce::JSON::parse(R"({"test-show":{"name":"Prueba"}})")), {}};
        if (actionName == "get_show") return HttpResponse{200, envelope(actionName, fixture()), {}};
        if (actionName == "get_show_layout") return HttpResponse{200, envelope(actionName, layoutRef(catalog)), {}};
        if (actionName == "index_select_slide" || actionName == "clear_slide")
        { std::lock_guard<std::mutex> lock(mutex); selected = request; sends++; return HttpResponse{204, {}, {}}; }
        if (actionName == "get_output")
        {
            std::lock_guard<std::mutex> lock(mutex);
            auto* out = new juce::DynamicObject(); out->setProperty("slide", juce::var{});
            if (selected.getProperty("action", {}).toString() == "index_select_slide")
            { auto* slide = new juce::DynamicObject(); slide->setProperty("id", selected.getProperty("showId", {})); slide->setProperty("layout", selected.getProperty("layoutId", {})); slide->setProperty("index", (int) selected.getProperty("index", {}) - 1); out->setProperty("slide", juce::var(slide)); }
            return HttpResponse{200, envelope(actionName, juce::var(out)), {}};
        }
        return HttpResponse{500, {}, "unexpected fixture request"};
    });
    client.queueTarget(target); std::this_thread::sleep_for(std::chrono::milliseconds(40));
    expect(sends == 0, "disconnected cues discarded immediately");
    client.testConnection();
    expect(until([&] { return client.getStatus().state == ConnectionStatus::State::connected; }), "read-only connection probe succeeds");
    client.setPlaybackGeneration(2); client.setPlaybackGeneration(1); client.queueTarget(target, 1);
    std::this_thread::sleep_for(std::chrono::milliseconds(40)); expect(sends == 0, "generation cannot regress or deliver stale seek cue");
    client.queueTarget(target, 2);
    expect(until([&] { return client.getStatus().outputConfirmed; }) && sends == 1, "worker sends and separately confirms real output identity");
    online = false; client.pollOutput();
    expect(until([&] { return client.getStatus().state == ConnectionStatus::State::disconnected; }), "network failure marks disconnected without audio interaction");
    const auto epoch = client.getStatus().connectionEpoch;
    client.queueTarget(target, 2); online = true; client.testConnection();
    expect(until([&] { return client.getStatus().state == ConnectionStatus::State::connected; }), "explicit reconnect succeeds");
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    expect(sends == 1 && client.getStatus().connectionEpoch == epoch, "reconnection does not replay expired history");
    Target hide; hide.hide = true; client.queueTarget(hide);
    expect(until([&] { return client.getStatus().outputConfirmed && client.getStatus().lastProjected == "Ocultar letras"; }) && sends == 2,
           "explicit hide destination clears and confirms independently");
}
class LoopbackHttpFixture
{
public:
    using Handler = std::function<std::string(const std::string&)>;
    explicit LoopbackHttpFixture(Handler handler, bool stall = false) : reply(std::move(handler)), stalled(stall)
    {
        expect(listener.createListener(0, "127.0.0.1"), "bind local-only HTTP fixture on ephemeral port");
        port = listener.getBoundPort();
        thread = std::thread([this]
        {
            while (!stopped)
            {
                if (listener.waitUntilReady(true, 50) <= 0) continue;
                std::unique_ptr<juce::StreamingSocket> connection(listener.waitForNextConnection());
                if (!connection) continue;
                std::string request;
                const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(2);
                while (!stopped && std::chrono::steady_clock::now() < until)
                {
                    if (connection->waitUntilReady(true, 50) <= 0) continue;
                    char buffer[4096]; const auto count = connection->read(buffer, (int) sizeof(buffer), false);
                    if (count <= 0) break;
                    request.append(buffer, (size_t) count);
                    const auto split = request.find("\r\n\r\n");
                    if (split == std::string::npos) continue;
                    const auto lengthStart = request.find("Content-Length: ");
                    if (lengthStart == std::string::npos) break;
                    const auto contentLength = (size_t) std::strtoul(request.c_str() + lengthStart + 16, nullptr, 10);
                    if (request.size() >= split + 4 + contentLength) break;
                }
                requests++;
                const auto response = reply(request);
                connection->write(response.data(), (int) response.size());
                if (stalled)
                    for (int i = 0; i < 200 && !stopped; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        });
    }
    ~LoopbackHttpFixture() { stopped = true; thread.join(); listener.close(); }
    int port = 0;
    std::atomic<int> requests{0};
private:
    juce::StreamingSocket listener;
    Handler reply;
    bool stalled;
    std::atomic<bool> stopped{false};
    std::thread thread;
};
std::string jsonResponse(const std::string& body)
{
    return "HTTP/1.1 200 OK\r\nContent-Type: application/json; charset=utf-8\r\nContent-Length: "
        + std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
}
void directHttpContracts()
{
    // These use the actual direct socket transport, even when ambient http_proxy is set.
    const std::string body = u8R"({"action":"get_shows","data":{"own":{"name":"Guía de canción propia"}}})";
    for (bool chunked : {false, true})
    {
        LoopbackHttpFixture server([&](const std::string&)
        {
            if (!chunked) return jsonResponse(body);
            const auto split = body.find(u8"í") + 1; // Split inside a UTF-8 character, not a code point boundary.
            const auto first = body.substr(0, split), second = body.substr(split);
            return "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nTransfer-Encoding: chunked\r\n\r\n"
                + juce::String::toHexString((juce::int64) first.size()).toStdString() + "\r\n" + first + "\r\n"
                + juce::String::toHexString((juce::int64) second.size()).toStdString() + ";fixture=yes\r\n" + second + "\r\n0\r\nX-Fixture: local\r\n\r\n";
        });
        FreeShowClient client; client.configure({"127.0.0.1", server.port, {}});
        std::promise<std::vector<ShowSummary>> completion; auto result = completion.get_future();
        client.requestShows([&](std::vector<ShowSummary> shows, juce::String) { completion.set_value(std::move(shows)); });
        expect(result.wait_for(std::chrono::seconds(3)) == std::future_status::ready, "direct HTTP parsed within deadline");
        const auto shows = result.get();
        expect(shows.size() == 1 && shows[0].name == juce::String::fromUTF8(u8"Guía de canción propia"),
               "Content-Length and chunked preserve UTF-8 octets");
    }
    {
        std::atomic<bool> authSeen{false};
        LoopbackHttpFixture server([&](const std::string& request)
        {
            authSeen = request.find("Authorization: Bearer fixture-session-key\r\n") != std::string::npos;
            return std::string("HTTP/1.1 401 Unauthorized\r\nContent-Length: 0\r\n\r\n");
        });
        FreeShowClient client; client.configure({"127.0.0.1", server.port, "fixture-session-key"}); client.testConnection();
        expect(until([&] { return client.getStatus().state == ConnectionStatus::State::unauthorized; }) && authSeen,
               "optional session Bearer sent only to validated local host and 401 stays clear");
    }
    {
        LoopbackHttpFixture server([](const std::string& request)
        {
            if (request.find("clear_slide") != std::string::npos) return std::string("HTTP/1.1 204 No Content\r\n\r\n");
            if (request.find("get_output") != std::string::npos) return jsonResponse(R"({"action":"get_output","data":{"slide":null}})");
            return jsonResponse("{}");
        });
        FreeShowClient client; client.configure({"127.0.0.1", server.port, {}}); client.testConnection();
        expect(until([&] { return client.getStatus().state == ConnectionStatus::State::connected; }), "direct socket connected");
        Target hide; hide.hide = true; client.queueTarget(hide);
        expect(until([&] { return client.getStatus().outputConfirmed; }), "HTTP 204 accepted action still requires separate output observation");
    }
    const std::vector<std::string> invalidResponses = {
        "HTTP/1.1 302 Found\r\nLocation: http://192.0.2.1/never-follow\r\nContent-Length: 0\r\n\r\n",
        "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: 100\r\n\r\n{}",
        "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: 16777217\r\n\r\n",
        "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: 2\r\n\r\n{}",
        "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nTransfer-Encoding: chunked\r\n\r\n8\r\n{}\r\n",
        "HTTP/1.1 200 OK\r\nX-Large: " + std::string(65537, 'a') + "\r\n\r\n"
    };
    for (const auto& response : invalidResponses)
    {
        LoopbackHttpFixture server([&](const std::string&) { return response; });
        FreeShowClient client; client.configure({"127.0.0.1", server.port, "fixture-session-key"});
        std::promise<bool> completion; auto result = completion.get_future();
        client.testConnection([&](bool ok, juce::String) { completion.set_value(ok); });
        expect(result.wait_for(std::chrono::seconds(3)) == std::future_status::ready && !result.get(),
               "redirect, truncated, oversized and non-JSON HTTP fail closed");
        expect(server.requests == 1, "invalid HTTP cannot replay or follow a redirect");
    }
    {
        LoopbackHttpFixture server([](const std::string&)
        { return std::string("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: 100\r\n\r\n{}"); }, true);
        FreeShowClient client; client.configure({"127.0.0.1", server.port, {}});
        const auto start = std::chrono::steady_clock::now();
        std::promise<bool> completion; auto result = completion.get_future();
        client.testConnection([&](bool ok, juce::String) { completion.set_value(ok); });
        expect(result.wait_for(std::chrono::seconds(3)) == std::future_status::ready && !result.get(), "stalled body observes read deadline");
        expect(std::chrono::steady_clock::now() - start < std::chrono::milliseconds(2500), "read deadline is bounded independently of peer close");
    }
}
void holyricsHttpContracts()
{
    std::atomic<bool> correct{false};
    LoopbackHttpFixture server([&](const std::string& wire) {
        correct = wire.find("POST /api/GetSongs?token=local%26test HTTP/1.1") != std::string::npos
            && wire.find("Authorization: Bearer") == std::string::npos
            && wire.find("\"fields\"") != std::string::npos;
        const std::string body = R"({"status":"ok","data":[]})";
        return std::string("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: ") + std::to_string(body.size()) + "\r\n\r\n" + body;
    });
    FreeShowClient client;
    ConnectionSettings settings; settings.provider = "holyrics"; settings.host = "127.0.0.1";
    settings.port = server.port; settings.authToken = "local&test";
    client.configure(settings);
    std::promise<bool> completion; auto result = completion.get_future();
    client.testConnection([&](bool ok, juce::String) { completion.set_value(ok); });
    expect(result.wait_for(std::chrono::seconds(3)) == std::future_status::ready && result.get() && correct,
           "Holyrics LAN sends official API path, escaped session token and JSON fields without a proxy");
}

void activeLayoutSafety()
{
    const auto initial = parseShow("test-show", fixture());
    const auto target = initial.groups.front().slides[1].target;
    std::atomic<int> phase{0}, projections{0}, mutations{0};
    std::mutex observedMutex;
    juce::var selected;
    const auto makeShow = [&] {
        auto show = fixture();
        if (phase == 1)
            show.getDynamicObject()->getProperty("settings").getDynamicObject()->setProperty("activeLayout", "alt");
        if (phase == 2)
            show.getDynamicObject()->getProperty("layouts").getDynamicObject()->getProperty("main")
                .getDynamicObject()->setProperty("slides", juce::JSON::parse(R"([{"id":"coro-literal-2"}])"));
        return show;
    };
    FreeShowClient client([&](const ConnectionSettings&, const juce::var& request) {
        const auto name = request.getProperty("action", {}).toString();
        if (name == "get_shows") return HttpResponse{200, envelope(name, juce::JSON::parse("{}")), {}};
        if (name == "get_show") return HttpResponse{200, envelope(name, makeShow()), {}};
        if (name == "get_show_layout")
            return HttpResponse{200, envelope(name, layoutRef(parseShow("test-show", makeShow()))), {}};
        if (name == "change_layout" || name == "set_show") { ++mutations; return HttpResponse{204, {}, {}}; }
        if (name == "index_select_slide") {
            const std::lock_guard<std::mutex> lock(observedMutex);
            selected = request; ++projections;
            return HttpResponse{204, {}, {}};
        }
        if (name == "get_output") {
            const std::lock_guard<std::mutex> lock(observedMutex);
            auto* slide = new juce::DynamicObject();
            slide->setProperty("id", selected.getProperty("showId", {}));
            slide->setProperty("layout", selected.getProperty("layoutId", {}));
            slide->setProperty("index", (int) selected.getProperty("index", 0) - 1);
            auto* output = new juce::DynamicObject(); output->setProperty("slide", juce::var(slide));
            return HttpResponse{200, envelope(name, juce::var(output)), {}};
        }
        return HttpResponse{200, envelope(name, {}), {}};
    });
    client.testConnection();
    expect(until([&] { return client.getStatus().state == ConnectionStatus::State::connected; }), "active-layout connection");
    for (int pass = 0; pass < 2; ++pass) {
        phase = pass; client.queueTarget(target);
        expect(until([&] { return projections == pass + 1 && client.getStatus().outputConfirmed; }), "show-bound destination is confirmed");
        const std::lock_guard<std::mutex> lock(observedMutex);
        expect(selected.getProperty("layoutId", {}).toString() == (pass == 0 ? "main" : "alt")
               && (int) selected.getProperty("index", 0) == (pass == 0 ? 2 : 3),
               "each request contains the verified active layout and its newly resolved index");
    }
    phase = 2; client.queueTarget(target);
    expect(until([&] { return client.getStatus().message.contains(juce::String::fromUTF8("no está en el arreglo activo")); }),
           "missing active slide is rejected with an actionable review reason");
    expect(projections == 2 && mutations == 0, "new marks never mutate show/layout or fall back to an unrelated index");
}
void alternateBlankSafety()
{
    const auto makeShow = [](const juce::String& active)
    {
        auto show = fixture();
        show.getDynamicObject()->getProperty("slides").getDynamicObject()->getProperty("coro").getDynamicObject()->setProperty("items", juce::Array<juce::var>{});
        show.getDynamicObject()->getProperty("settings").getDynamicObject()->setProperty("activeLayout", active);
        return show;
    };
    const auto initial = parseShow("test-show", makeShow("main"));
    Target target;
    for (const auto& layout : initial.layouts) if (layout.id == "alt") target = layout.slides.front().target;
    juce::String active = "main";
    std::atomic<int> changes{0}, projections{0};
    FreeShowClient client([&](const ConnectionSettings&, const juce::var& request)
    {
        const auto name = request.getProperty("action", {}).toString();
        if (name == "get_shows") return HttpResponse{200, envelope(name, juce::JSON::parse("{}")), {}};
        if (name == "get_show") return HttpResponse{200, envelope(name, makeShow(active)), {}};
        if (name == "change_layout") { active = request.getProperty("layoutId", {}).toString(); changes++; return HttpResponse{204, {}, {}}; }
        if (name == "get_show_layout")
        {
            auto reference = layoutRef(parseShow("test-show", makeShow(active)));
            if (active == "alt")
            { auto* blank = new juce::DynamicObject(); blank->setProperty("id", "fake_empty"); reference.getArray()->insert(0, juce::var(blank)); }
            return HttpResponse{200, envelope(name, reference), {}};
        }
        if (name == "index_select_slide") { projections++; return HttpResponse{204, {}, {}}; }
        return HttpResponse{200, envelope(name, {}), {}};
    });
    client.testConnection();
    expect(until([&] { return client.getStatus().state == ConnectionStatus::State::connected; }), "alternate safety connection");
    client.queueTarget(target);
    expect(until([&] { return client.getStatus().message.contains("volver a elegir"); }), "unknown blank-first setting becomes review after real reference check");
    expect(changes == 1 && projections == 0, "arrangement selection without projection cannot leak a wrong offset slide");
}
void realIntegration(const juce::String& showId, int port)
{
    FreeShowClient client; client.configure({"127.0.0.1", port, {}});
    client.testConnection();
    expect(until([&] { return client.getStatus().state == ConnectionStatus::State::connected; }), "real FreeShow connection");
    std::promise<ShowCatalog> promise; auto future = promise.get_future();
    client.requestShow(showId, [&](ShowCatalog show) { promise.set_value(std::move(show)); });
    expect(future.wait_for(std::chrono::seconds(5)) == std::future_status::ready, "real catalog request completed");
    const auto catalog = future.get();
    if (!catalog.ok()) throw std::runtime_error(catalog.error.toStdString());
    int count = 0;
    for (const auto& layout : catalog.layouts)
        for (const auto& destination : layout.slides)
        {
            client.cancelPending(); client.queueTarget(destination.target);
            expect(until([&] { auto status = client.getStatus(); return status.outputConfirmed && status.lastProjected == targetLabel(destination.target); }), "real output confirms exact expanded target");
            std::cout << "Projected " << layout.name << " / " << destination.displayName << '\n'; ++count;
        }
    Target hide; hide.hide = true; client.queueTarget(hide);
    expect(until([&] { auto status = client.getStatus(); return status.outputConfirmed && status.lastProjected == "Ocultar letras"; }), "real hide confirmed");
    std::cout << "Real FreeShow integration passed: " << count << " destinations + hide\n";
}
}
int main(int argc, char** argv)
{
    try
    {
        modelContracts(); settingsContracts(); holyricsContracts(); holyricsHttpContracts(); clientContracts(); activeLayoutSafety(); alternateBlankSafety(); directHttpContracts();
        if (argc >= 3 && juce::String(argv[1]) == "--real") realIntegration(argv[2], argc > 3 ? juce::String(argv[3]).getIntValue() : 5506);
        std::cout << "FreeShow protocol, identity, persistence and worker tests passed\n"; return 0;
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
