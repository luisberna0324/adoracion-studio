#pragma once
#include "FreeShowModel.h"
#include <functional>
#include <memory>

namespace playworship::lyrics
{
struct ConnectionSettings
{
    juce::String host = "127.0.0.1";
    int port = 5506;
    juce::String authToken; // Memory only. Never serialize this value.
    juce::String provider = "freeshow";
};
struct ConnectionStatus
{
    enum class State { disconnected, connecting, connected, unauthorized, error };
    State state = State::disconnected;
    juce::String message = "FreeShow desconectado";
    juce::String lastSent, lastProjected;
    bool outputConfirmed = false;
    juce::uint64 connectionEpoch = 0;
    juce::String reviewShowId;
    juce::uint64 reviewRevision = 0; // Monotonic notification for UI catalogue refresh after resolution failure.
};
struct HttpResponse { int status = 0; juce::var body; juce::String error; };
class FreeShowClient
{
public:
    // Callbacks execute on the worker. UI callers must marshal with MessageManager::callAsync.
    using Transport = std::function<HttpResponse(const ConnectionSettings&, const juce::var&)>;
    using Completion = std::function<void(bool, juce::String)>;
    using ShowsCompletion = std::function<void(std::vector<ShowSummary>, juce::String)>;
    using ShowCompletion = std::function<void(ShowCatalog)>;
    explicit FreeShowClient(Transport transport = {});
    ~FreeShowClient();
    FreeShowClient(const FreeShowClient&) = delete;
    FreeShowClient& operator=(const FreeShowClient&) = delete;
    static bool validateSettings(const ConnectionSettings&, juce::String* error = nullptr);
    void configure(ConnectionSettings);
    ConnectionSettings getSettings() const;
    ConnectionStatus getStatus() const;
    void testConnection(Completion = {});
    void requestShows(ShowsCompletion);
    void requestShow(juce::String showId, ShowCompletion);
    // Non-audio caller only. Latest wins, expires after 750 ms; disconnected cues are discarded.
    void queueTarget(Target, juce::uint64 generation = 0);
    void cancelPending();
    void setPlaybackGeneration(juce::uint64) noexcept;
    void pollOutput();
    void disconnect();
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
}
