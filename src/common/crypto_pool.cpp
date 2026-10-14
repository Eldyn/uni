#include <common/crypto_pool.hpp>
#include <common/base64.hpp>
#include <common/env.hpp>
#include <logger.hpp>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <string>
#include <vector>

namespace {

// PBKDF2 parameters, moved unchanged from AuthService: increasing
// kIterations raises the cost of brute-force attempts. The cost factor
// is untouched by the pool migration — only the thread it runs on changed.
constexpr int kSaltBytes  = 16;
constexpr int kHashBytes  = 32;
constexpr int kIterations = 200'000;

// INFO: PBKDF2 is battle-tested and acceptable for this project scale.
//
//       The "pepper" is an application-level secret from the environment.
//       Unlike a salt (stored in the DB, unique per user, public), a pepper
//       is *not* stored anywhere, it lives only in memory, loaded from the
//       env at startup. An attacker who steals the DB but not the server
//       config cannot run offline dictionary attacks even with the salts in
//       hand.
Result<std::string> HashPasswordOnWorker(const std::string& password) {
    // INFO: 1. Generate a cryptographically random salt. RAND_bytes fills the
    //       buffer with random bytes from OpenSSL's CSPRNG.
    std::vector<unsigned char> salt(kSaltBytes);
    if (RAND_bytes(salt.data(), kSaltBytes) != 1) {
        return std::unexpected(Error::Internal("[Auth] CSPRNG failure: RAND_bytes returned 0"));
    }

    // INFO: 2. Read the pepper from the environment and append it to the
    //       password. std::string + std::string = concatenation; no UB here.
    //       Read on the worker so the loop thread pays only enqueue.
    std::string peppered_pass = password + Env::Get("PASSWORD_PEPPER", "");

    // INFO: 3. Derive the hash.
    //          PKCS5_PBKDF2_HMAC(password, passlen,
    //                            salt, saltlen,
    //                            iterations,
    //                            digest,        ← EVP_sha256() here
    //                            keylen,
    //                            output_buffer)
    //          Returns 1 on success, 0 on failure.
    std::vector<unsigned char> hash(kHashBytes);
    if (PKCS5_PBKDF2_HMAC(
            peppered_pass.c_str(),
            static_cast<int>(peppered_pass.size()),
            salt.data(),
            kSaltBytes,
            kIterations,
            EVP_sha256(),
            kHashBytes,
            hash.data()) != 1) {
        return std::unexpected(Error::Internal("[Auth] PBKDF2 failed"));
    }

    // INFO: 4. Encode both salt and hash to base64 and return as
    //       "<salt>:<hash>". Storing them together makes retrieval a single
    //       DB read.
    return Base64::Encode(salt) + ":" + Base64::Encode(hash);
}

bool VerifyPasswordOnWorker(const std::string& password, const std::string& stored) {
    auto colon = stored.find(':');
    if (colon == std::string::npos) return false;

    std::vector<unsigned char> salt;
    std::vector<unsigned char> ref_hash;
    try {
        salt     = Base64::Decode(stored.substr(0, colon));
        ref_hash = Base64::Decode(stored.substr(colon + 1));
    } catch (const std::exception&) {
        return false;  // malformed base64 in the row: never a match
    }
    if (salt.size() != static_cast<std::size_t>(kSaltBytes) ||
        ref_hash.size() != static_cast<std::size_t>(kHashBytes)) {
        return false;  // truncated/oversized digest would over-read in CRYPTO_memcmp
    }

    std::string peppered_pass = password + Env::Get("PASSWORD_PEPPER", "");

    std::vector<unsigned char> candidate(kHashBytes);
    if (PKCS5_PBKDF2_HMAC(
            peppered_pass.c_str(),
            static_cast<int>(peppered_pass.size()),
            salt.data(),
            static_cast<int>(salt.size()),
            kIterations,
            EVP_sha256(),
            kHashBytes,
            candidate.data()) != 1) {
        return false;
    }

    // INFO: CRYPTO_memcmp performs a constant-time comparison. A regular `==`
    //       on std::vector short-circuits on the first mismatch, which leaks
    //       timing information, a timing-side-channel attack can use that to
    //       infer how many bytes of the hash match. Returns 0 if equal (same
    //       convention as memcmp).
    return (CRYPTO_memcmp(candidate.data(), ref_hash.data(), kHashBytes) == 0);
}

}  // namespace

CryptoPool& CryptoPool::Get() {
    static CryptoPool instance;
    return instance;
}

CryptoPool::CryptoPool() {
    std::size_t n = ThreadCount();
    threads_.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        threads_.emplace_back(&CryptoPool::Run, this);
    }
    Logger::Info("[Crypto] pool started with " + std::to_string(n) + " threads");
}

CryptoPool::~CryptoPool() {
    {
        std::lock_guard<std::mutex> lock(m_);
        stop_ = true;
    }
    cv_.notify_all();
    for (auto& th : threads_) {
        if (th.joinable()) th.join();
    }
}

std::size_t CryptoPool::ThreadCount() {
    unsigned hc  = std::thread::hardware_concurrency();
    int      def = hc == 0 ? 4 : static_cast<int>(hc);
    int      n   = Env::GetInt("CRYPTO_POOL_THREADS", def);
    if (n <= 0) n = def;
    return static_cast<std::size_t>(n);
}

std::future<Result<std::string>> CryptoPool::HashPasswordAsync(std::string password) {
    return Enqueue([pw = std::move(password)]() mutable {
        return HashPasswordOnWorker(pw);
    });
}

std::future<bool> CryptoPool::VerifyPasswordAsync(std::string password, std::string stored) {
    return Enqueue([pw = std::move(password), st = std::move(stored)]() mutable {
        return VerifyPasswordOnWorker(pw, st);
    });
}

void CryptoPool::Run() {
    while (true) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(m_);
            cv_.wait(lock, [this] { return stop_ || !q_.empty(); });
            if (q_.empty()) {
                if (stop_) return;
                continue;
            }
            task = std::move(q_.front());
            q_.pop_front();
        }
        task();
    }
}
