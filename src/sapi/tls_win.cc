// Real TLS client via Schannel/SSPI. Automatic certificate chain +
// hostname validation stays on throughout (never SCH_CRED_MANUAL_CRED_
// VALIDATION, never SCH_CRED_NO_SERVERNAME_CHECK) -- this is a genuine
// security boundary, not a demo toggle. Same "one blocking gocvm.Call"
// model as real_win.cc (see its own header comment): the handshake loop
// below runs to completion inside one TlsDial call, using plain blocking
// send()/recv() on the raw socket.
#include "handles.h"
#include "net_internal.h"
#include "real.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

namespace w2g {
namespace real {

using namespace internal;

namespace {

constexpr size_t kRecvChunk = 16384;
constexpr size_t kMaxPlaintextRead = 1 << 20;

bool SendAll(SOCKET s, const uint8_t* data, size_t len) {
  size_t sent = 0;
  while (sent < len) {
    int n = send(s, reinterpret_cast<const char*>(data + sent),
                static_cast<int>(len - sent), 0);
    if (n <= 0) return false;
    sent += static_cast<size_t>(n);
  }
  return true;
}

// Appends whatever's currently available to *buf. Returns false on a
// real socket error or a clean close with nothing buffered yet.
bool RecvMore(SOCKET s, std::vector<uint8_t>* buf) {
  uint8_t chunk[kRecvChunk];
  int n = recv(s, reinterpret_cast<char*>(chunk), sizeof(chunk), 0);
  if (n <= 0) return false;
  buf->insert(buf->end(), chunk, chunk + n);
  return true;
}

// Full client handshake loop (AcquireCredentialsHandleW +
// InitializeSecurityContextW, standard shape -- same as Microsoft's own
// "Creating a Secure Connection Using Schannel" client sample). On
// success *cred/*ctx/*sizes are populated and owned by the caller
// (handles.h's TlsEntry); *leftover holds any raw bytes read past the
// handshake that belong to the first application-data record.
bool DoHandshake(SOCKET s, const std::wstring& target, CredHandle* cred, CtxtHandle* ctx,
                 SecPkgContext_StreamSizes* sizes, std::vector<uint8_t>* leftover,
                 std::string* err) {
  SCHANNEL_CRED sc{};
  sc.dwVersion = SCHANNEL_CRED_VERSION;
  sc.dwFlags = SCH_CRED_NO_DEFAULT_CREDS | SCH_CRED_REVOCATION_CHECK_CHAIN;
  TimeStamp expiry{};
  SECURITY_STATUS st = AcquireCredentialsHandleW(nullptr, const_cast<wchar_t*>(UNISP_NAME_W),
                                                 SECPKG_CRED_OUTBOUND, nullptr, &sc, nullptr,
                                                 nullptr, cred, &expiry);
  if (st != SEC_E_OK) {
    *err = "AcquireCredentialsHandle failed (0x" + std::to_string(static_cast<unsigned>(st)) + ")";
    return false;
  }

  std::vector<uint8_t> raw;  // bytes read from the socket, not yet consumed
  bool have_ctx = false;
  constexpr DWORD kFlags = ISC_REQ_SEQUENCE_DETECT | ISC_REQ_REPLAY_DETECT |
                           ISC_REQ_CONFIDENTIALITY | ISC_REQ_ALLOCATE_MEMORY | ISC_REQ_STREAM;

  for (;;) {
    SecBuffer in_bufs[2] = {};
    SecBufferDesc in_desc{SECBUFFER_VERSION, 2, in_bufs};
    if (have_ctx) {
      in_bufs[0].BufferType = SECBUFFER_TOKEN;
      in_bufs[0].pvBuffer = raw.empty() ? nullptr : raw.data();
      in_bufs[0].cbBuffer = static_cast<unsigned long>(raw.size());
      in_bufs[1].BufferType = SECBUFFER_EMPTY;
    }

    SecBuffer out_buf{};
    out_buf.BufferType = SECBUFFER_TOKEN;
    SecBufferDesc out_desc{SECBUFFER_VERSION, 1, &out_buf};
    DWORD out_flags = 0;

    st = InitializeSecurityContextW(
        cred, have_ctx ? ctx : nullptr, have_ctx ? nullptr : const_cast<wchar_t*>(target.c_str()),
        kFlags, 0, SECURITY_NATIVE_DREP, have_ctx ? &in_desc : nullptr, 0, ctx, &out_desc,
        &out_flags, nullptr);
    have_ctx = true;

    if (out_buf.cbBuffer > 0 && out_buf.pvBuffer) {
      bool ok = SendAll(s, static_cast<const uint8_t*>(out_buf.pvBuffer), out_buf.cbBuffer);
      FreeContextBuffer(out_buf.pvBuffer);
      if (!ok) {
        *err = "send failed during handshake";
        return false;
      }
    }

    if (st == SEC_E_OK) {
      // Any bytes InitializeSecurityContext didn't consume (SECBUFFER_
      // EXTRA in in_bufs[1]) belong to the first application-data
      // record, not the handshake.
      if (have_ctx && in_bufs[1].BufferType == SECBUFFER_EXTRA && in_bufs[1].cbBuffer > 0) {
        size_t used = raw.size() - in_bufs[1].cbBuffer;
        leftover->assign(raw.begin() + static_cast<long>(used), raw.end());
      }
      SECURITY_STATUS qst = QueryContextAttributesW(ctx, SECPKG_ATTR_STREAM_SIZES, sizes);
      if (qst != SEC_E_OK) {
        *err =
            "QueryContextAttributes(STREAM_SIZES) failed (0x" + std::to_string(static_cast<unsigned>(qst)) + ")";
        DeleteSecurityContext(ctx);
        return false;
      }
      return true;
    }
    if (st == SEC_I_CONTINUE_NEEDED) {
      if (in_bufs[1].BufferType == SECBUFFER_EXTRA && in_bufs[1].cbBuffer > 0) {
        size_t used = raw.size() - in_bufs[1].cbBuffer;
        raw.erase(raw.begin(), raw.begin() + static_cast<long>(used));
      } else {
        raw.clear();
      }
      continue;
    }
    if (st == SEC_E_INCOMPLETE_MESSAGE) {
      if (!RecvMore(s, &raw)) {
        *err = "connection closed during handshake";
        return false;
      }
      continue;
    }
    // Anything else, including a real certificate validation failure
    // (e.g. CERT_E_UNTRUSTEDROOT/CERT_E_CN_NO_MATCH/CERT_E_EXPIRED,
    // surfaced through SEC_E_UNTRUSTED_ROOT etc.) -- a genuine security
    // rejection, reported as a real error, never silently accepted.
    *err = "TLS handshake failed (0x" + std::to_string(static_cast<unsigned>(st)) + ")";
    return false;
  }
}

}  // namespace

Reply TlsDial(const std::string& address) {
  std::string host, port;
  if (!SplitHostPort(address, &host, &port)) {
    return {W2G_RESULT_INVALID_ARGUMENT, "error: missing port in address " + address};
  }
  SOCKET s = INVALID_SOCKET;
  Reply connect_r = ConnectReal("tcp", address, &s);
  if (s == INVALID_SOCKET) {
    return {connect_r.code, "error: " + connect_r.payload};
  }

  CredHandle cred{};
  CtxtHandle ctx{};
  SecPkgContext_StreamSizes sizes{};
  std::vector<uint8_t> leftover;
  std::string err;
  if (!DoHandshake(s, Utf8ToWide(host), &cred, &ctx, &sizes, &leftover, &err)) {
    closesocket(s);
    return {W2G_RESULT_OK, "error: tls " + address + ": " + err};
  }

  Handle h = AllocTls(s, cred, ctx, sizes);
  TlsEntry* e = LookupTls(h);
  if (!leftover.empty()) e->raw_extra = std::move(leftover);
  return {W2G_RESULT_OK, "ok handle=" + std::to_string(h)};
}

Reply TlsIoRead(const std::string& handle_and_maxlen) {
  std::string hs, ms;
  SplitOne(handle_and_maxlen, '\x1f', &hs, &ms);
  Handle h = 0;
  if (!ParseHandle(hs, &h)) return {W2G_RESULT_INVALID_ARGUMENT, "error: bad handle"};
  TlsEntry* e = LookupTls(h);
  if (!e) return {W2G_RESULT_INVALID_ARGUMENT, "error: unknown handle " + hs};
  long maxlen = std::strtol(ms.c_str(), nullptr, 10);
  if (maxlen <= 0 || static_cast<size_t>(maxlen) > kMaxPlaintextRead) maxlen = 60000;

  while (e->plaintext.empty()) {
    if (e->raw_extra.size() < e->sizes.cbHeader && !RecvMore(e->s, &e->raw_extra)) {
      return {W2G_RESULT_OK, ""};  // clean close mid-stream -- EOF
    }
    SecBuffer bufs[4] = {};
    bufs[0].BufferType = SECBUFFER_DATA;
    bufs[0].pvBuffer = e->raw_extra.data();
    bufs[0].cbBuffer = static_cast<unsigned long>(e->raw_extra.size());
    bufs[1].BufferType = SECBUFFER_EMPTY;
    bufs[2].BufferType = SECBUFFER_EMPTY;
    bufs[3].BufferType = SECBUFFER_EMPTY;
    SecBufferDesc desc{SECBUFFER_VERSION, 4, bufs};
    SECURITY_STATUS st = DecryptMessage(&e->ctx, &desc, 0, nullptr);
    if (st == SEC_E_INCOMPLETE_MESSAGE) {
      if (!RecvMore(e->s, &e->raw_extra)) return {W2G_RESULT_OK, ""};
      continue;
    }
    if (st == SEC_I_CONTEXT_EXPIRED) {
      e->raw_extra.clear();
      return {W2G_RESULT_OK, ""};  // peer sent close_notify
    }
    if (st != SEC_E_OK && st != SEC_I_RENEGOTIATE) {
      return {W2G_RESULT_OK,
              "error: tls decrypt failed (0x" + std::to_string(static_cast<unsigned>(st)) + ")"};
    }
    std::vector<uint8_t> next_extra;
    for (const auto& b : bufs) {
      if (b.BufferType == SECBUFFER_DATA && b.cbBuffer > 0) {
        const uint8_t* p = static_cast<const uint8_t*>(b.pvBuffer);
        e->plaintext.insert(e->plaintext.end(), p, p + b.cbBuffer);
      } else if (b.BufferType == SECBUFFER_EXTRA && b.cbBuffer > 0) {
        const uint8_t* p = static_cast<const uint8_t*>(b.pvBuffer);
        next_extra.assign(p, p + b.cbBuffer);
      }
    }
    e->raw_extra = std::move(next_extra);
    if (st == SEC_I_RENEGOTIATE) {
      // Server-initiated renegotiation: not implemented (rare for a
      // plain HTTPS client fetch) -- surface as an honest error rather
      // than silently hanging.
      return {W2G_RESULT_OK, "error: tls: server requested renegotiation (not supported)"};
    }
  }

  size_t n = std::min(static_cast<size_t>(maxlen), e->plaintext.size());
  std::string out(reinterpret_cast<const char*>(e->plaintext.data()), n);
  e->plaintext.erase(e->plaintext.begin(), e->plaintext.begin() + static_cast<long>(n));
  return {W2G_RESULT_OK, out};
}

Reply TlsIoWrite(const std::string& handle_and_data) {
  std::string hs, data;
  SplitOne(handle_and_data, '\x1f', &hs, &data);
  Handle h = 0;
  if (!ParseHandle(hs, &h)) return {W2G_RESULT_INVALID_ARGUMENT, "error: bad handle"};
  TlsEntry* e = LookupTls(h);
  if (!e) return {W2G_RESULT_INVALID_ARGUMENT, "error: unknown handle " + hs};

  size_t chunk_max = e->sizes.cbMaximumMessage;
  if (chunk_max == 0) chunk_max = 4096;
  size_t total = 0;
  while (total < data.size()) {
    size_t n = std::min(chunk_max, data.size() - total);
    std::vector<uint8_t> msg(e->sizes.cbHeader + n + e->sizes.cbTrailer);
    std::memcpy(msg.data() + e->sizes.cbHeader, data.data() + total, n);

    SecBuffer bufs[4] = {};
    bufs[0].BufferType = SECBUFFER_STREAM_HEADER;
    bufs[0].pvBuffer = msg.data();
    bufs[0].cbBuffer = e->sizes.cbHeader;
    bufs[1].BufferType = SECBUFFER_DATA;
    bufs[1].pvBuffer = msg.data() + e->sizes.cbHeader;
    bufs[1].cbBuffer = static_cast<unsigned long>(n);
    bufs[2].BufferType = SECBUFFER_STREAM_TRAILER;
    bufs[2].pvBuffer = msg.data() + e->sizes.cbHeader + n;
    bufs[2].cbBuffer = e->sizes.cbTrailer;
    bufs[3].BufferType = SECBUFFER_EMPTY;
    SecBufferDesc desc{SECBUFFER_VERSION, 4, bufs};

    SECURITY_STATUS st = EncryptMessage(&e->ctx, 0, &desc, 0);
    if (st != SEC_E_OK) {
      return {W2G_RESULT_OK,
              "error: tls encrypt failed (0x" + std::to_string(static_cast<unsigned>(st)) + ")"};
    }
    size_t wire_len = bufs[0].cbBuffer + bufs[1].cbBuffer + bufs[2].cbBuffer;
    if (!SendAll(e->s, msg.data(), wire_len)) {
      return {W2G_RESULT_OK, "error: tls write: connection closed"};
    }
    total += n;
  }
  return {W2G_RESULT_OK, std::to_string(total)};
}

Reply TlsIoClose(const std::string& handle) {
  Handle h = 0;
  if (!ParseHandle(handle, &h)) return {W2G_RESULT_INVALID_ARGUMENT, "error: bad handle"};
  Release(h);
  return {W2G_RESULT_OK, "ok"};
}

}  // namespace real
}  // namespace w2g
