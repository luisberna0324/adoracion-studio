#include "FreeShowClient.h"
#include "HolyricsModel.h"
#include <atomic>
#include <cmath>
#include <string>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <optional>
#include <thread>

namespace playworship::lyrics
{
namespace
{
juce::var action(const juce::String& name, const juce::String& id = {})
{
    auto* obj = new juce::DynamicObject(); obj->setProperty("action", name);
    if (id.isNotEmpty()) obj->setProperty("id", id);
    return juce::var(obj);
}
HttpResponse httpRequest(const ConnectionSettings& settings, const juce::var& body)
{
    // Direct sockets deliberately bypass OS/environment HTTP proxies. A local Bearer key
    // must never be forwarded to an ambient proxy, and no redirect is followed.
    HttpResponse result;
    const auto fail = [&](const juce::String& error) { result.error = error; return result; };
    const auto host = settings.host == "localhost" ? juce::String("127.0.0.1") : settings.host;
    juce::StreamingSocket socket;
    if (!socket.connect(host, settings.port, 1200)) return fail("El servidor de letras no responde");
    auto payload = body.clone();
    juce::String path = "/";
    if (settings.provider == "holyrics") {
        const auto name = body.getProperty("action", {}).toString();
        if (name.isEmpty() || !name.containsOnly("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ")) return fail("Acción Holyrics inválida");
        path = "/api/" + name + "?token=" + juce::URL::addEscapeChars(settings.authToken, true);
        payload.getDynamicObject()->removeProperty("action");
    }
    const auto json = juce::JSON::toString(payload, true);
    juce::String request = "POST " + path + " HTTP/1.1\r\nHost: " + host + ":" + juce::String(settings.port)
        + "\r\nContent-Type: application/json\r\nConnection: close\r\nContent-Length: "
        + juce::String((juce::int64) json.getNumBytesAsUTF8()) + "\r\n";
    if (settings.provider == "freeshow" && settings.authToken.isNotEmpty()) request += "Authorization: Bearer " + settings.authToken + "\r\n";
    request += "\r\n" + json;
    const auto bytesToWrite = (int) request.getNumBytesAsUTF8();
    if (socket.write(request.toRawUTF8(), bytesToWrite) != bytesToWrite) return fail("No se pudo enviar al programa de letras");
    constexpr size_t maxHeaders = 64 * 1024, maxBody = 16 * 1024 * 1024;
    const auto deadline = juce::Time::getMillisecondCounterHiRes() + 1200.0;
    std::string received, decoded;
    size_t bodyOffset = 0, chunkOffset = 0, chunkSize = 0;
    bool headersParsed = false, chunked = false, needsChunkHeader = true, trailers = false;
    juce::int64 contentLength = -1;
    for (;;)
    {
        if (headersParsed)
        {
            if (!chunked && received.size() - bodyOffset >= (size_t) contentLength)
            { decoded.assign(received.data() + bodyOffset, (size_t) contentLength); break; }
            if (chunked)
            {
                bool complete = false;
                for (;;)
                {
                    if (trailers)
                    {
                        if (received.size() >= chunkOffset + 2 && received.compare(chunkOffset, 2, "\r\n") == 0) complete = true;
                        else if (received.find("\r\n\r\n", chunkOffset) != std::string::npos) complete = true;
                        else if (received.size() - chunkOffset > maxHeaders) return fail("Cabeceras FreeShow demasiado grandes");
                        break;
                    }
                    if (needsChunkHeader)
                    {
                        const auto lineEnd = received.find("\r\n", chunkOffset);
                        if (lineEnd == std::string::npos)
                        { if (received.size() - chunkOffset > 8192) return fail(juce::String::fromUTF8("Chunk FreeShow inválido")); break; }
                        auto sizeText = received.substr(chunkOffset, lineEnd - chunkOffset);
                        sizeText = sizeText.substr(0, sizeText.find(';'));
                        if (sizeText.empty() || sizeText.size() > 8) return fail(juce::String::fromUTF8("Chunk FreeShow inválido"));
                        chunkSize = 0;
                        for (const auto c : sizeText)
                        {
                            int digit = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
                            if (digit < 0) return fail(juce::String::fromUTF8("Chunk FreeShow inválido"));
                            chunkSize = chunkSize * 16 + (size_t) digit;
                            if (chunkSize > maxBody - decoded.size()) return fail("Respuesta FreeShow demasiado grande");
                        }
                        chunkOffset = lineEnd + 2; needsChunkHeader = false;
                        if (chunkSize == 0) { trailers = true; continue; }
                    }
                    if (received.size() - chunkOffset < chunkSize + 2) break;
                    if (received.compare(chunkOffset + chunkSize, 2, "\r\n") != 0) return fail("Chunk FreeShow truncado");
                    decoded.append(received.data() + chunkOffset, chunkSize);
                    chunkOffset += chunkSize + 2; needsChunkHeader = true;
                }
                if (complete) break;
            }
        }
        const auto remaining = (int) std::ceil(deadline - juce::Time::getMillisecondCounterHiRes());
        if (remaining <= 0 || socket.waitUntilReady(true, remaining) <= 0) return fail(juce::String::fromUTF8("FreeShow agotó el tiempo de respuesta"));
        char buffer[8192];
        const auto count = socket.read(buffer, (int) sizeof(buffer), false);
        if (count <= 0) return fail("Respuesta FreeShow truncada");
        received.append(buffer, (size_t) count);
        if (received.size() > maxBody + 2 * maxHeaders) return fail("Respuesta FreeShow demasiado grande");
        if (!headersParsed)
        {
            const auto end = received.find("\r\n\r\n");
            if (end == std::string::npos)
            { if (received.size() > maxHeaders) return fail("Cabeceras FreeShow demasiado grandes"); continue; }
            if (end > maxHeaders) return fail("Cabeceras FreeShow demasiado grandes");
            bodyOffset = end + 4; chunkOffset = bodyOffset;
            const auto lines = juce::StringArray::fromLines(juce::String::fromUTF8(received.data(), (int) end));
            const auto status = juce::StringArray::fromTokens(lines[0], " ", "");
            if (status.size() < 2 || (status[0] != "HTTP/1.1" && status[0] != "HTTP/1.0")
                || status[1].length() != 3 || !status[1].containsOnly("0123456789")) return fail(juce::String::fromUTF8("Estado HTTP FreeShow inválido"));
            result.status = status[1].getIntValue();
            if (result.status == 204 || result.status < 200 || result.status >= 300) return result;
            juce::String contentType, transferEncoding;
            bool lengthSeen = false, transferSeen = false;
            for (int i = 1; i < lines.size(); ++i)
            {
                const auto key = lines[i].upToFirstOccurrenceOf(":", false, false).trim().toLowerCase();
                const auto value = lines[i].fromFirstOccurrenceOf(":", false, false).trim();
                if (key == "content-type") contentType = value;
                if (key == "transfer-encoding")
                { if (transferSeen) return fail("Transferencia HTTP FreeShow ambigua"); transferSeen = true; transferEncoding = value.toLowerCase(); }
                if (key == "content-length")
                {
                    if (lengthSeen || value.isEmpty() || value.length() > 9 || !value.containsOnly("0123456789")) return fail(juce::String::fromUTF8("Longitud HTTP FreeShow inválida"));
                    lengthSeen = true; contentLength = value.getLargeIntValue();
                    if (contentLength > (juce::int64) maxBody) return fail("Respuesta FreeShow demasiado grande");
                }
            }
            if (!contentType.startsWithIgnoreCase("application/json")) return fail(juce::String::fromUTF8("FreeShow no devolvió application/json"));
            if (transferSeen && (lengthSeen || transferEncoding != "chunked")) return fail("Transferencia HTTP FreeShow no compatible");
            chunked = transferSeen;
            if (!chunked && !lengthSeen) return fail(juce::String::fromUTF8("FreeShow no indicó la longitud de respuesta"));
            headersParsed = true;
        }
    }
    if (juce::JSON::parse(juce::String::fromUTF8(decoded.data(), (int) decoded.size()), result.body).failed())
        result.error = juce::String::fromUTF8("FreeShow devolvió JSON inválido");
    return result;

}
}

struct FreeShowClient::Impl
{
    struct Cue { Target target; juce::uint64 generation = 0, epoch = 0, serial = 0; double expires = 0; };
    Transport transport;
    mutable std::mutex mutex;
    std::condition_variable wake;
    ConnectionSettings settings;
    ConnectionStatus status;
    std::deque<std::function<void()>> requests;
    std::optional<Cue> pending, lastSent;
    std::atomic<juce::uint64> playbackGeneration{0};
    juce::uint64 cueSerial = 0;
    bool stopping = false;
    int lastIndex = 0;
    double nextPoll = 0;
    std::thread worker;

    explicit Impl(Transport t) : transport(t ? std::move(t) : Transport(httpRequest)), worker([this] { run(); }) {}
    ~Impl()
    {
        { std::lock_guard<std::mutex> lock(mutex); stopping = true; pending.reset(); requests.clear(); }
        wake.notify_all(); worker.join();
    }
    void enqueue(std::function<void()> task)
    {
        { std::lock_guard<std::mutex> lock(mutex); if (stopping) return; requests.push_back(std::move(task)); }
        wake.notify_one();
    }
    bool current(const Cue& cue)
    {
        std::lock_guard<std::mutex> lock(mutex);
        return !stopping && cue.epoch == status.connectionEpoch && cue.serial == cueSerial
            && status.state == ConnectionStatus::State::connected && juce::Time::getMillisecondCounterHiRes() <= cue.expires
            && (cue.generation == 0 || playbackGeneration.load(std::memory_order_acquire) == cue.generation);
    }
    HttpResponse request(const juce::var& data, juce::uint64 epoch)
    {
        ConnectionSettings snapshot;
        { std::lock_guard<std::mutex> lock(mutex); if (stopping || epoch != status.connectionEpoch) return {}; snapshot = settings; }
        juce::String validationError;
        auto response = FreeShowClient::validateSettings(snapshot, &validationError)
            ? transport(snapshot, data) : HttpResponse{0, {}, validationError};
        if (snapshot.provider == "holyrics" && response.status >= 200 && response.status < 300
            && response.body.getProperty("status", {}).toString() != "ok") {
            response.error = "Holyrics rechazó la solicitud; revisa el token y sus permisos";
            response.status = 403;
        }
        { std::lock_guard<std::mutex> lock(mutex); if (stopping || epoch != status.connectionEpoch) return {0, {}, "La conexión de letras cambió"}; }
        if (response.status < 200 || response.status >= 300 || response.error.isNotEmpty())
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (epoch == status.connectionEpoch)
            {
                status.state = response.status == 401 ? ConnectionStatus::State::unauthorized : ConnectionStatus::State::disconnected;
                status.message = response.status == 401 ? "Token de letras incorrecto" : (response.error.isNotEmpty() ? response.error : "Programa de letras desconectado");
                status.outputConfirmed = false; status.connectionEpoch++; pending.reset(); lastSent.reset();
            }
        }
        return response;
    }
    static bool success(const HttpResponse& r) { return r.status >= 200 && r.status < 300 && r.error.isEmpty(); }
    juce::uint64 epoch() { std::lock_guard<std::mutex> lock(mutex); return status.connectionEpoch; }
    void connected(juce::uint64 e)
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (e != status.connectionEpoch) return;
        status.state = ConnectionStatus::State::connected; status.message = settings.provider == "holyrics" ? "Holyrics conectado" : "FreeShow conectado";
    }
    juce::var listRequest() {
        auto result = action(holyrics() ? "GetSongs" : "get_shows");
        if (holyrics()) result.getDynamicObject()->setProperty("fields", "id,title");
        return result;
    }
    bool holyrics() { std::lock_guard<std::mutex> lock(mutex); return settings.provider == "holyrics"; }
    ShowCatalog fetchShow(const juce::String& id, juce::uint64 e)
    {
        if (holyrics()) {
            const auto response = request(action("GetLyrics", id), e);
            if (success(response)) return parseHolyricsSong(id, response.body);
            ShowCatalog failed; failed.provider = "holyrics"; failed.error = "No se pudo cargar la canción de Holyrics"; return failed;
        }
        const auto full = request(action("get_show", id), e);
        if (!success(full)) { ShowCatalog failed; failed.error = "No se pudo cargar el show FreeShow"; return failed; }
        const auto reference = request(action("get_show_layout", id), e);
        if (!success(reference) || !unwrapResponse(reference.body).isArray())
        { ShowCatalog failed; failed.error = juce::String::fromUTF8("FreeShow no devolvió el orden real del arreglo"); return failed; }
        return parseShow(id, full.body, reference.body);
    }
    void project(const Cue& cue)
    {
        if (!current(cue)) return;
        Resolution destination{true, 0, {}};
        Target effectiveTarget = cue.target;
        if (!cue.target.hide)
        {
            auto show = fetchShow(cue.target.showId, cue.epoch);
            if (!current(cue)) return;
            destination = resolve(cue.target, show);
            if (cue.target.useActiveLayout)
                effectiveTarget.layoutId = show.activeLayoutId;
            if (destination.valid && !cue.target.useActiveLayout && show.activeLayoutId != cue.target.layoutId)
            {
                // An app-wide blank-first setting is only observable for the active arrangement.
                // Changing arrangement does not project; verify its actual expanded reference before selecting.
                auto change = action("change_layout");
                change.getDynamicObject()->setProperty("showId", cue.target.showId);
                change.getDynamicObject()->setProperty("layoutId", cue.target.layoutId);
                if (!current(cue) || !success(request(change, cue.epoch))) return;
                show = fetchShow(cue.target.showId, cue.epoch);
                if (!current(cue)) return;
                destination = show.activeLayoutId == cue.target.layoutId
                    ? resolve(cue.target, show) : Resolution{false, 0, juce::String::fromUTF8("FreeShow no confirmó el arreglo solicitado")};
            }
        }
        if (!destination.valid)
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (cue.epoch == status.connectionEpoch && cue.serial == cueSerial
                && (cue.generation == 0 || playbackGeneration.load(std::memory_order_acquire) == cue.generation))
            {
                status.message = destination.error; status.outputConfirmed = false;
                status.reviewShowId = cue.target.showId; ++status.reviewRevision;
                lastSent.reset();
            }
            return;
        }
        if (!current(cue)) return;
        juce::var command;
        if (holyrics()) {
            // Older Holyrics silently ignores initial_index and projects the first slide.
            const auto versionResponse = request(action("GetVersion"), cue.epoch);
            auto versionData = versionResponse.body.getProperty("data", {});
            if (versionData.hasProperty("data")) versionData = versionData.getProperty("data", {});
            const auto parts = juce::StringArray::fromTokens(versionData.getProperty("version", {}).toString(), ".", "");
            const bool supported = success(versionResponse) && parts.size() >= 2
                && (parts[0].getIntValue() > 2 || (parts[0].getIntValue() == 2 && parts[1].getIntValue() >= 23));
            if (!supported) {
                std::lock_guard<std::mutex> lock(mutex);
                if (cue.epoch == status.connectionEpoch) {
                    status.message = "Se requiere Holyrics 2.23.0 o posterior y permiso GetVersion";
                    status.outputConfirmed = false;
                }
                return;
            }
            if (!current(cue)) return;
            command = action(cue.target.hide ? "SetF8" : "ShowLyrics");
            if (cue.target.hide) command.getDynamicObject()->setProperty("enable", true);
            else {
                command.getDynamicObject()->setProperty("id", effectiveTarget.showId);
                command.getDynamicObject()->setProperty("initial_index", destination.absoluteIndex - 1);
            }
        } else command = makeSelectRequest(effectiveTarget, destination.absoluteIndex);
        if (!current(cue)) return;
        const auto result = request(command, cue.epoch);
        if (!success(result)) return;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (cue.epoch != status.connectionEpoch || cue.serial != cueSerial) return;
            status.lastSent = targetLabel(cue.target); status.outputConfirmed = false;
            status.message = juce::String::fromUTF8("Enviado · esperando confirmación de salida");
            lastSent = cue; lastSent->target = std::move(effectiveTarget); lastIndex = destination.absoluteIndex;
        }
        observe(cue.epoch);
    }
    void observe(juce::uint64 e)
    {
        const bool isHolyrics = holyrics();
        const auto result = request(action(isHolyrics ? "GetCurrentPresentation" : "get_output"), e);
        if (!success(result)) return;
        std::lock_guard<std::mutex> lock(mutex);
        if (e != status.connectionEpoch || !lastSent) return;
        status.outputConfirmed = isHolyrics ? holyricsOutputMatches(lastSent->target, lastIndex, result.body) : outputMatches(lastSent->target, lastIndex, result.body);
        if (status.outputConfirmed)
        { status.lastProjected = targetLabel(lastSent->target); status.message = juce::String::fromUTF8("Salida confirmada · ") + status.lastProjected; }
        else status.message = juce::String::fromUTF8("Enviado · salida todavía no coincide");
    }
    void run()
    {
        for (;;)
        {
            std::function<void()> task;
            std::optional<Cue> cue;
            bool poll = false; juce::uint64 e = 0;
            {
                std::unique_lock<std::mutex> lock(mutex);
                wake.wait_for(lock, std::chrono::milliseconds(100), [&] { return stopping || pending || !requests.empty(); });
                if (stopping) break;
                if (pending) { cue = std::move(pending); pending.reset(); }
                else if (!requests.empty()) { task = std::move(requests.front()); requests.pop_front(); }
                else if (status.state == ConnectionStatus::State::connected && juce::Time::getMillisecondCounterHiRes() >= nextPoll)
                { poll = true; e = status.connectionEpoch; nextPoll = juce::Time::getMillisecondCounterHiRes() + 1000; }
            }
            if (cue) project(*cue); else if (task) task(); else if (poll) observe(e);
        }
    }
};

FreeShowClient::FreeShowClient(Transport transport) : impl(std::make_unique<Impl>(std::move(transport))) {}
FreeShowClient::~FreeShowClient() = default;
bool FreeShowClient::validateSettings(const ConnectionSettings& settings, juce::String* error)
{
    const auto fail = [&] { if (error) *error = juce::String::fromUTF8("Usa localhost o una IPv4 privada y un puerto válido"); return false; };
    if ((settings.provider != "freeshow" && settings.provider != "holyrics") || settings.port < 1 || settings.port > 65535 || settings.authToken.length() > 1024
        || settings.authToken.containsAnyOf("\r\n") || settings.host != settings.host.trim()) return fail();
    if (settings.host == "localhost") { if (error) error->clear(); return true; }
    const auto octets = juce::StringArray::fromTokens(settings.host, ".", "");
    if (octets.size() != 4) return fail();
    int values[4]{};
    for (int i = 0; i < 4; ++i)
    {
        if (octets[i].isEmpty() || octets[i].length() > 3 || !octets[i].containsOnly("0123456789")
            || (octets[i].length() > 1 && octets[i].startsWithChar('0'))) return fail();
        values[i] = octets[i].getIntValue(); if (values[i] > 255) return fail();
    }
    if (!(values[0] == 127 || values[0] == 10 || (values[0] == 172 && values[1] >= 16 && values[1] <= 31)
        || (values[0] == 192 && values[1] == 168))) return fail();
    if (values[3] == 255 || (values[0] != 127 && values[3] == 0)) return fail();
    if (error) error->clear(); return true;
}
void FreeShowClient::configure(ConnectionSettings settings)
{
    juce::String error;
    const bool valid = validateSettings(settings, &error);
    std::lock_guard<std::mutex> lock(impl->mutex);
    impl->status.connectionEpoch++; impl->pending.reset(); impl->lastSent.reset(); impl->cueSerial++;
    impl->status.outputConfirmed = false; impl->status.lastSent.clear(); impl->status.lastProjected.clear();
    impl->status.reviewShowId.clear();
    impl->settings = std::move(settings);
    if (!valid) { impl->status.state = ConnectionStatus::State::error; impl->status.message = error; return; }
    impl->status.state = ConnectionStatus::State::disconnected;
    impl->status.message = juce::String::fromUTF8("Configurado · probar conexión");
}
ConnectionSettings FreeShowClient::getSettings() const { std::lock_guard<std::mutex> lock(impl->mutex); return impl->settings; }
ConnectionStatus FreeShowClient::getStatus() const { std::lock_guard<std::mutex> lock(impl->mutex); return impl->status; }
void FreeShowClient::testConnection(Completion completion)
{
    const auto e = impl->epoch();
    { std::lock_guard<std::mutex> lock(impl->mutex); impl->status.state = ConnectionStatus::State::connecting; impl->status.message = juce::String::fromUTF8("Conectando al programa de letras…"); }
    impl->enqueue([this, e, completion = std::move(completion)]
    {
        const auto response = impl->request(impl->listRequest(), e);
        const bool ok = Impl::success(response) && (impl->holyrics() ? response.body.getProperty("data", {}).isArray() : unwrapResponse(response.body).isObject());
        if (ok) impl->connected(e);
        else if (Impl::success(response))
        {
            std::lock_guard<std::mutex> lock(impl->mutex);
            if (e == impl->status.connectionEpoch) { impl->status.state = ConnectionStatus::State::error; impl->status.message = "Respuesta incompatible con el programa elegido"; }
        }
        if (completion) completion(ok, ok ? juce::String{} : getStatus().message);
    });
}
void FreeShowClient::requestShows(ShowsCompletion completion)
{
    const auto e = impl->epoch();
    impl->enqueue([this, e, completion = std::move(completion)]
    {
        const auto response = impl->request(impl->listRequest(), e);
        const bool ok = Impl::success(response) && (impl->holyrics() ? response.body.getProperty("data", {}).isArray() : unwrapResponse(response.body).isObject());
        if (ok) impl->connected(e);
        if (completion) completion(ok ? (impl->holyrics() ? parseHolyricsSongs(response.body) : parseShowList(response.body)) : std::vector<ShowSummary>{}, ok ? juce::String{} : juce::String::fromUTF8("No se pudo cargar el catálogo de letras"));
    });
}
void FreeShowClient::requestShow(juce::String id, ShowCompletion completion)
{
    const auto e = impl->epoch();
    impl->enqueue([this, e, id = std::move(id), completion = std::move(completion)]
    { auto show = impl->fetchShow(id, e); if (show.ok()) impl->connected(e); if (completion) completion(std::move(show)); });
}
void FreeShowClient::queueTarget(Target target, juce::uint64 generation)
{
    { std::lock_guard<std::mutex> lock(impl->mutex);
      if (impl->status.state != ConnectionStatus::State::connected || target.provider != impl->settings.provider) return;
      if (generation != 0 && generation != impl->playbackGeneration.load(std::memory_order_acquire)) return;
      impl->status.outputConfirmed = false;
      impl->pending = Impl::Cue{std::move(target), generation, impl->status.connectionEpoch, ++impl->cueSerial,
                               juce::Time::getMillisecondCounterHiRes() + 750.0}; }
    impl->wake.notify_one();
}
void FreeShowClient::cancelPending()
{
    std::lock_guard<std::mutex> lock(impl->mutex); impl->pending.reset(); impl->cueSerial++; impl->lastSent.reset(); impl->status.outputConfirmed = false;
}
void FreeShowClient::setPlaybackGeneration(juce::uint64 generation) noexcept
{
    auto previous = impl->playbackGeneration.load(std::memory_order_relaxed);
    while (previous < generation && !impl->playbackGeneration.compare_exchange_weak(previous, generation, std::memory_order_release, std::memory_order_relaxed)) {}
}
void FreeShowClient::pollOutput()
{
    const auto e = impl->epoch();
    impl->enqueue([this, e] { impl->observe(e); });
}
void FreeShowClient::disconnect()
{
    std::lock_guard<std::mutex> lock(impl->mutex);
    impl->pending.reset(); impl->lastSent.reset(); impl->status.connectionEpoch++; impl->cueSerial++;
    impl->status.state = ConnectionStatus::State::disconnected; impl->status.message = "Programa de letras desconectado";
    impl->status.outputConfirmed = false; impl->status.reviewShowId.clear();
}
}
