# CryptoExecutor: Off-Face-Thread ABE Operations

## Problem

ABE pairing operations (content-key generation/encryption on the producer,
`cpDecrypt`/`kpDecrypt` on the consumer) are expensive — often tens to hundreds
of milliseconds, growing with the number of attributes. Previously they ran
synchronously on the caller's thread, which is normally the thread running the
`Face` event loop. While a pairing op was in progress that Face could not
process any Interests or Data, so:

- a producer could not answer Interests (including CK and data segment
  requests) while encrypting;
- a consumer's own `SegmentFetcher` for the CK stalled during decryption,
  which could cause timeouts and retransmissions under load.

## Design

`CryptoExecutor` (`src/crypto-executor.hpp`) is a process-wide singleton that
owns one `boost::asio::io_context` and a single worker thread running it.

```
 Face thread                     CryptoExecutor thread
 -----------                     ---------------------
 produceAsync()/consume()
   └─ post(job) ───────────────▶ pairing + AES (+ signing on producer)
                                    └─ post(result) ───┐
 callback(result) ◀──────────────────────────────────┘
```

1. The caller's request is posted to `CryptoExecutor::getInstance().ioContext()`.
2. The worker thread does the ABE/AES work. It never touches the `Face`.
3. The result (or error string) is posted back to the owning object's
   `m_face.getIoContext()`, so user callbacks run on the same thread as
   before and need no extra synchronization.

There is deliberately **one** worker thread for the whole process:

- OpenABE is not documented as safe for concurrent use from multiple threads.
- `ABESupport` keeps process-wide caches (e.g. the decrypted content-key map)
  that are not synchronized.

A single thread guarantees at most one ABE operation runs at a time, no matter
how many `Producer`/`Consumer` instances exist.

## API

### Producer

Two new overloads mirror the existing synchronous `produce()` calls:

```c++
// KP-ABE
producer.produceAsync(dataName, {"ucla", "cs", "exam"}, content, signingInfo,
  [] (SPtrVector<Data> contentData, SPtrVector<Data> ckData) { /* publish */ },
  [] (const std::string& error) { /* handle error */ });

// CP-ABE
producer.produceAsync(dataName, "ucla and professor", content, signingInfo,
                      onSuccess, onError);
```

- `content` is copied before the job is queued, so the caller does not need to
  keep the buffer alive.
- `produce()` is called through virtual dispatch, so `CacheProducer` still
  reuses cached content keys and only runs the pairing operation the first
  time an attribute set/policy is seen.
- The synchronous `produce()` overloads are unchanged.

### Consumer

No API change. `Consumer::consume()` now runs the CK decryption and content
decryption through `CryptoExecutor` internally (`decryptCipherTextAsync()`),
for both the CK-cache-hit path and the freshly-fetched-CK path. The success
and error callbacks are still invoked on the consumer's Face thread.

## Lifetime

`CryptoExecutor` is created on first use and destroyed at process exit. It
does not track individual jobs, so **do not destroy a `Producer` or `Consumer`
while one of its `produceAsync()`/`consume()` calls is still in flight** — the
queued job captures `this`.

## Related changes in this branch

- `Consumer::obtainDecryptionKey()` now uses a 4 s Interest lifetime and a
  120 s max timeout for the DKEY `SegmentFetcher`, since DKEY generation at
  the attribute authority can be slow.
