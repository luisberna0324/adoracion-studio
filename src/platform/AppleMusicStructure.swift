import Foundation
import AVFoundation
import CoreMedia

#if canImport(MusicUnderstanding)
import MusicUnderstanding
#endif

// The C ABI is intentionally independent of JUCE, Swift-generated headers and
// CMTime. Build with the existing deployment target; only the provider needs 27.
public typealias PWMusicStructureCompletion = @convention(c) (
    UnsafePointer<CChar>?, UnsafePointer<CChar>?, UnsafeMutableRawPointer?
) -> Void

private final class MusicStructureJob: @unchecked Sendable {
    let token: UInt64
    let completion: PWMusicStructureCompletion
    let context: UnsafeMutableRawPointer?
    private let lock = NSLock()
    private var task: Task<Void, Never>?
    private var cancelled = false
    private var finished = false

    init(token: UInt64, completion: @escaping PWMusicStructureCompletion,
         context: UnsafeMutableRawPointer?) {
        self.token = token
        self.completion = completion
        self.context = context
    }

    func install(_ task: Task<Void, Never>) {
        lock.lock()
        if finished { lock.unlock(); return }
        self.task = task
        let shouldCancel = cancelled
        lock.unlock()
        if shouldCancel { task.cancel() }
    }

    func cancel() {
        lock.lock()
        cancelled = true
        let currentTask = task
        lock.unlock()
        currentTask?.cancel()
    }

    func complete(json: String? = nil, error: String? = nil) {
        lock.lock()
        if finished { lock.unlock(); return }
        finished = true
        let wasCancelled = cancelled
        task = nil
        lock.unlock()
        MusicStructureJobs.shared.remove(token)
        if let failure = wasCancelled ? "cancelled" : error {
            failure.withCString { completion(nil, $0, context) }
        } else if let json {
            json.withCString { completion($0, nil, context) }
        } else {
            "Music Understanding no devolvio resultados.".withCString { completion(nil, $0, context) }
        }
    }
}

private final class MusicStructureJobs: @unchecked Sendable {
    static let shared = MusicStructureJobs()
    private let lock = NSLock()
    private var nextToken: UInt64 = 1
    private var jobs: [UInt64: MusicStructureJob] = [:]

    func make(completion: @escaping PWMusicStructureCompletion,
              context: UnsafeMutableRawPointer?) -> MusicStructureJob {
        lock.lock()
        defer { lock.unlock() }
        let token = nextToken
        nextToken &+= 1
        if nextToken == 0 { nextToken = 1 }
        let job = MusicStructureJob(token: token, completion: completion, context: context)
        jobs[token] = job
        return job
    }

    func remove(_ token: UInt64) {
        lock.lock()
        jobs.removeValue(forKey: token)
        lock.unlock()
    }

    func cancel(_ token: UInt64) {
        lock.lock()
        let job = jobs[token]
        lock.unlock()
        job?.cancel()
    }
}

private final class MusicStructureMessages: @unchecked Sendable {
    static let shared = MusicStructureMessages()
    let sdkMissing: UnsafePointer<CChar>
    let systemMissing: UnsafePointer<CChar>
    let empty: UnsafePointer<CChar>
    private init() {
        sdkMissing = UnsafePointer(strdup("Esta compilacion no incluye Music Understanding. Requiere un SDK de Apple 27 compatible."))
        systemMissing = UnsafePointer(strdup("Music Understanding requiere macOS 27 o posterior. El editor manual sigue disponible."))
        empty = UnsafePointer(strdup(""))
    }
}

@_cdecl("pwMusicStructureAvailable")
public func pwMusicStructureAvailable() -> Int32 {
    #if canImport(MusicUnderstanding) && os(macOS)
    if #available(macOS 27.0, *) { return 1 }
    #endif
    return 0
}

@_cdecl("pwMusicStructureUnavailableReason")
public func pwMusicStructureUnavailableReason() -> UnsafePointer<CChar> {
    #if canImport(MusicUnderstanding) && os(macOS)
    if #available(macOS 27.0, *) { return MusicStructureMessages.shared.empty }
    return MusicStructureMessages.shared.systemMissing
    #else
    return MusicStructureMessages.shared.sdkMissing
    #endif
}

@_cdecl("pwMusicStructureStart")
public func pwMusicStructureStart(_ localPath: UnsafePointer<CChar>?,
                                 _ completion: PWMusicStructureCompletion?,
                                 _ context: UnsafeMutableRawPointer?) -> UInt64 {
    guard let completion else { return 0 }
    // Copy before returning: C++ may release its path storage immediately.
    let path = localPath.map { String(cString: $0) } ?? ""
    let job = MusicStructureJobs.shared.make(completion: completion, context: context)
    let task = Task.detached(priority: .utility) {
        do {
            try Task.checkCancellation()
            guard !path.isEmpty, path.hasPrefix("/"), FileManager.default.isReadableFile(atPath: path) else {
                job.complete(error: "El archivo de analisis local no existe o no tiene permiso de lectura.")
                return
            }
            #if canImport(MusicUnderstanding) && os(macOS)
            if #available(macOS 27.0, *) {
                let json = try await analyzeLocalMusic(path)
                try Task.checkCancellation()
                job.complete(json: json)
            } else {
                job.complete(error: String(cString: pwMusicStructureUnavailableReason()))
            }
            #else
            job.complete(error: String(cString: pwMusicStructureUnavailableReason()))
            #endif
        } catch is CancellationError {
            job.complete(error: "cancelled")
        } catch {
            job.complete(error: error.localizedDescription)
        }
    }
    job.install(task)
    return job.token
}

@_cdecl("pwMusicStructureCancel")
public func pwMusicStructureCancel(_ token: UInt64) {
    MusicStructureJobs.shared.cancel(token)
}

#if canImport(MusicUnderstanding) && os(macOS)
@available(macOS 27.0, *)
private func analyzeLocalMusic(_ path: String) async throws -> String {
    let asset = AVURLAsset(url: URL(fileURLWithPath: path), options: [
        AVURLAssetPreferPreciseDurationAndTimingKey: true
    ])
    let duration = try await asset.load(.duration).seconds
    guard duration.isFinite, duration > 0 else {
        throw NSError(domain: "PlayWorship.SectionDetection", code: 1,
                      userInfo: [NSLocalizedDescriptionKey: "El audio no tiene una duracion valida."])
    }
    let session = try await MusicUnderstandingSession(asset: asset)
    let result = try await withTaskCancellationHandler(operation: {
        try Task.checkCancellation()
        return try await session.analyze(for: [.rhythm, .structure])
    }, onCancel: {
        Task { await session.cancel() }
    })
    try Task.checkCancellation()

    func ranges(_ values: [CMTimeRange]) -> [[String: Double]] {
        values.compactMap { range in
            let start = range.start.seconds
            let end = CMTimeRangeGetEnd(range).seconds
            guard start.isFinite, end.isFinite, start >= 0, start < duration, end > start else { return nil }
            return ["startSeconds": start, "endSeconds": min(end, duration)]
        }.sorted { $0["startSeconds", default: 0] < $1["startSeconds", default: 0] }
    }

    func points(_ values: [CMTime]) -> [Double] {
        Array(Set(values.map(\.seconds).filter { $0.isFinite && $0 >= 0 && $0 <= duration })).sorted()
    }

    var object: [String: Any] = [
        "provider": "Apple Music Understanding",
        "version": "1-rhythm-structure",
        "durationSeconds": duration,
        "sections": ranges(result.structure?.sections ?? []),
        "segments": ranges(result.structure?.segments ?? []),
        "phrases": ranges(result.structure?.phrases ?? []),
        "beats": points(result.rhythm?.beats ?? []),
        "bars": points(result.rhythm?.bars ?? [])
    ]
    if let bpm = result.rhythm?.beatsPerMinute, bpm.isFinite, bpm > 0 {
        object["bpm"] = Double(bpm)
    }
    let data = try JSONSerialization.data(withJSONObject: object, options: [.sortedKeys])
    return String(decoding: data, as: UTF8.self)
}
#endif
