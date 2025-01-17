// Copyright 2013 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/dns/record_rdata.h"

#include <algorithm>
#include <numeric>
#include <utility>

#include "base/big_endian.h"
#include "base/logging.h"
#include "base/memory/ptr_util.h"
#include "base/rand_util.h"
#include "base/strings/string_piece.h"
#include "net/base/ip_address.h"
#include "net/dns/dns_response.h"
#include "net/dns/public/dns_protocol.h"

namespace net {

static const size_t kSrvRecordMinimumSize = 6;

// The simplest INTEGRITY record is a U16-length-prefixed nonce (containing zero
// bytes) followed by its SHA256 digest.
static constexpr size_t kIntegrityMinimumSize =
    sizeof(uint16_t) + IntegrityRecordRdata::kDigestLen;

// Minimal HTTPS rdata is 2 octets priority + 1 octet empty name.
static constexpr size_t kHttpsRdataMinimumSize = 3;

bool RecordRdata::HasValidSize(const base::StringPiece& data, uint16_t type) {
  switch (type) {
    case dns_protocol::kTypeSRV:
      return data.size() >= kSrvRecordMinimumSize;
    case dns_protocol::kTypeA:
      return data.size() == IPAddress::kIPv4AddressSize;
    case dns_protocol::kTypeAAAA:
      return data.size() == IPAddress::kIPv6AddressSize;
    case dns_protocol::kExperimentalTypeIntegrity:
      return data.size() >= kIntegrityMinimumSize;
    case dns_protocol::kTypeHttps:
      return data.size() >= kHttpsRdataMinimumSize;
    case dns_protocol::kTypeCNAME:
    case dns_protocol::kTypePTR:
    case dns_protocol::kTypeTXT:
    case dns_protocol::kTypeNSEC:
    case dns_protocol::kTypeOPT:
    case dns_protocol::kTypeSOA:
      return true;
    default:
      VLOG(1) << "Unrecognized RDATA type.";
      return true;
  }
}

SrvRecordRdata::SrvRecordRdata() = default;

SrvRecordRdata::~SrvRecordRdata() = default;

// static
std::unique_ptr<SrvRecordRdata> SrvRecordRdata::Create(
    const base::StringPiece& data,
    const DnsRecordParser& parser) {
  if (!HasValidSize(data, kType))
    return nullptr;

  auto rdata = base::WrapUnique(new SrvRecordRdata());

  auto reader = base::BigEndianReader::FromStringPiece(data);
  // 2 bytes for priority, 2 bytes for weight, 2 bytes for port.
  reader.ReadU16(&rdata->priority_);
  reader.ReadU16(&rdata->weight_);
  reader.ReadU16(&rdata->port_);

  if (!parser.ReadName(data.substr(kSrvRecordMinimumSize).begin(),
                       &rdata->target_))
    return nullptr;

  return rdata;
}

uint16_t SrvRecordRdata::Type() const {
  return SrvRecordRdata::kType;
}

bool SrvRecordRdata::IsEqual(const RecordRdata* other) const {
  if (other->Type() != Type()) return false;
  const SrvRecordRdata* srv_other = static_cast<const SrvRecordRdata*>(other);
  return weight_ == srv_other->weight_ &&
      port_ == srv_other->port_ &&
      priority_ == srv_other->priority_ &&
      target_ == srv_other->target_;
}

ARecordRdata::ARecordRdata() = default;

ARecordRdata::~ARecordRdata() = default;

// static
std::unique_ptr<ARecordRdata> ARecordRdata::Create(
    const base::StringPiece& data,
    const DnsRecordParser& parser) {
  if (!HasValidSize(data, kType))
    return nullptr;

  auto rdata = base::WrapUnique(new ARecordRdata());
  rdata->address_ =
      IPAddress(reinterpret_cast<const uint8_t*>(data.data()), data.length());
  return rdata;
}

uint16_t ARecordRdata::Type() const {
  return ARecordRdata::kType;
}

bool ARecordRdata::IsEqual(const RecordRdata* other) const {
  if (other->Type() != Type()) return false;
  const ARecordRdata* a_other = static_cast<const ARecordRdata*>(other);
  return address_ == a_other->address_;
}

AAAARecordRdata::AAAARecordRdata() = default;

AAAARecordRdata::~AAAARecordRdata() = default;

// static
std::unique_ptr<AAAARecordRdata> AAAARecordRdata::Create(
    const base::StringPiece& data,
    const DnsRecordParser& parser) {
  if (!HasValidSize(data, kType))
    return nullptr;

  auto rdata = base::WrapUnique(new AAAARecordRdata());
  rdata->address_ =
      IPAddress(reinterpret_cast<const uint8_t*>(data.data()), data.length());
  return rdata;
}

uint16_t AAAARecordRdata::Type() const {
  return AAAARecordRdata::kType;
}

bool AAAARecordRdata::IsEqual(const RecordRdata* other) const {
  if (other->Type() != Type()) return false;
  const AAAARecordRdata* a_other = static_cast<const AAAARecordRdata*>(other);
  return address_ == a_other->address_;
}

CnameRecordRdata::CnameRecordRdata() = default;

CnameRecordRdata::~CnameRecordRdata() = default;

// static
std::unique_ptr<CnameRecordRdata> CnameRecordRdata::Create(
    const base::StringPiece& data,
    const DnsRecordParser& parser) {
  auto rdata = base::WrapUnique(new CnameRecordRdata());

  if (!parser.ReadName(data.begin(), &rdata->cname_))
    return nullptr;

  return rdata;
}

uint16_t CnameRecordRdata::Type() const {
  return CnameRecordRdata::kType;
}

bool CnameRecordRdata::IsEqual(const RecordRdata* other) const {
  if (other->Type() != Type()) return false;
  const CnameRecordRdata* cname_other =
      static_cast<const CnameRecordRdata*>(other);
  return cname_ == cname_other->cname_;
}

PtrRecordRdata::PtrRecordRdata() = default;

PtrRecordRdata::~PtrRecordRdata() = default;

// static
std::unique_ptr<PtrRecordRdata> PtrRecordRdata::Create(
    const base::StringPiece& data,
    const DnsRecordParser& parser) {
  auto rdata = base::WrapUnique(new PtrRecordRdata());

  if (!parser.ReadName(data.begin(), &rdata->ptrdomain_))
    return nullptr;

  return rdata;
}

uint16_t PtrRecordRdata::Type() const {
  return PtrRecordRdata::kType;
}

bool PtrRecordRdata::IsEqual(const RecordRdata* other) const {
  if (other->Type() != Type()) return false;
  const PtrRecordRdata* ptr_other = static_cast<const PtrRecordRdata*>(other);
  return ptrdomain_ == ptr_other->ptrdomain_;
}

TxtRecordRdata::TxtRecordRdata() = default;

TxtRecordRdata::~TxtRecordRdata() = default;

// static
std::unique_ptr<TxtRecordRdata> TxtRecordRdata::Create(
    const base::StringPiece& data,
    const DnsRecordParser& parser) {
  auto rdata = base::WrapUnique(new TxtRecordRdata());

  for (size_t i = 0; i < data.size(); ) {
    uint8_t length = data[i];

    if (i + length >= data.size())
      return nullptr;

    rdata->texts_.push_back(std::string(data.substr(i + 1, length)));

    // Move to the next string.
    i += length + 1;
  }

  return rdata;
}

uint16_t TxtRecordRdata::Type() const {
  return TxtRecordRdata::kType;
}

bool TxtRecordRdata::IsEqual(const RecordRdata* other) const {
  if (other->Type() != Type()) return false;
  const TxtRecordRdata* txt_other = static_cast<const TxtRecordRdata*>(other);
  return texts_ == txt_other->texts_;
}

NsecRecordRdata::NsecRecordRdata() = default;

NsecRecordRdata::~NsecRecordRdata() = default;

// static
std::unique_ptr<NsecRecordRdata> NsecRecordRdata::Create(
    const base::StringPiece& data,
    const DnsRecordParser& parser) {
  auto rdata = base::WrapUnique(new NsecRecordRdata());

  // Read the "next domain". This part for the NSEC record format is
  // ignored for mDNS, since it has no semantic meaning.
  unsigned next_domain_length = parser.ReadName(data.data(), nullptr);

  // If we did not succeed in getting the next domain or the data length
  // is too short for reading the bitmap header, return.
  if (next_domain_length == 0 || data.length() < next_domain_length + 2)
    return nullptr;

  struct BitmapHeader {
    uint8_t block_number;  // The block number should be zero.
    uint8_t length;        // Bitmap length in bytes. Between 1 and 32.
  };

  const BitmapHeader* header = reinterpret_cast<const BitmapHeader*>(
      data.data() + next_domain_length);

  // The block number must be zero in mDns-specific NSEC records. The bitmap
  // length must be between 1 and 32.
  if (header->block_number != 0 || header->length == 0 || header->length > 32)
    return nullptr;

  base::StringPiece bitmap_data = data.substr(next_domain_length + 2);

  // Since we may only have one block, the data length must be exactly equal to
  // the domain length plus bitmap size.
  if (bitmap_data.length() != header->length)
    return nullptr;

  rdata->bitmap_.insert(rdata->bitmap_.begin(),
                        bitmap_data.begin(),
                        bitmap_data.end());

  return rdata;
}

uint16_t NsecRecordRdata::Type() const {
  return NsecRecordRdata::kType;
}

bool NsecRecordRdata::IsEqual(const RecordRdata* other) const {
  if (other->Type() != Type())
    return false;
  const NsecRecordRdata* nsec_other =
      static_cast<const NsecRecordRdata*>(other);
  return bitmap_ == nsec_other->bitmap_;
}

bool NsecRecordRdata::GetBit(unsigned i) const {
  unsigned byte_num = i/8;
  if (bitmap_.size() < byte_num + 1)
    return false;

  unsigned bit_num = 7 - i % 8;
  return (bitmap_[byte_num] & (1 << bit_num)) != 0;
}

IntegrityRecordRdata::IntegrityRecordRdata(Nonce nonce)
    : nonce_(std::move(nonce)), digest_(Hash(nonce_)), is_intact_(true) {}

IntegrityRecordRdata::IntegrityRecordRdata(Nonce nonce,
                                           Digest digest,
                                           size_t rdata_len)
    : nonce_(std::move(nonce)),
      digest_(digest),
      is_intact_(rdata_len == LengthForSerialization(nonce_) &&
                 Hash(nonce_) == digest_) {}

IntegrityRecordRdata::IntegrityRecordRdata(IntegrityRecordRdata&&) = default;
IntegrityRecordRdata::IntegrityRecordRdata(const IntegrityRecordRdata&) =
    default;
IntegrityRecordRdata::~IntegrityRecordRdata() = default;

bool IntegrityRecordRdata::IsEqual(const RecordRdata* other) const {
  if (other->Type() != Type())
    return false;
  const IntegrityRecordRdata* integrity_other =
      static_cast<const IntegrityRecordRdata*>(other);
  return is_intact_ && integrity_other->is_intact_ &&
         nonce_ == integrity_other->nonce_ &&
         digest_ == integrity_other->digest_;
}

uint16_t IntegrityRecordRdata::Type() const {
  return kType;
}

// static
std::unique_ptr<IntegrityRecordRdata> IntegrityRecordRdata::Create(
    const base::StringPiece& data) {
  auto reader = base::BigEndianReader::FromStringPiece(data);
  // Parse a U16-prefixed |Nonce| followed by a |Digest|.
  base::StringPiece parsed_nonce, parsed_digest;

  // Note that even if this parse fails, we still want to create a record.
  bool parse_success = reader.ReadU16LengthPrefixed(&parsed_nonce) &&
                       reader.ReadPiece(&parsed_digest, kDigestLen);

  const std::string kZeroDigest = std::string(kDigestLen, 0);
  if (!parse_success) {
    parsed_nonce = base::StringPiece();
    parsed_digest = base::StringPiece(kZeroDigest);
  }

  Digest digest_copy{};
  CHECK_EQ(parsed_digest.size(), digest_copy.size());
  std::copy_n(parsed_digest.begin(), parsed_digest.size(), digest_copy.begin());

  auto record = base::WrapUnique(
      new IntegrityRecordRdata(Nonce(parsed_nonce.begin(), parsed_nonce.end()),
                               digest_copy, data.size()));

  // A failed parse implies |!IsIntact()|, though the converse is not true. The
  // record may be considered not intact if there were trailing bytes in |data|
  // or if |parsed_digest| is not the hash of |parsed_nonce|.
  if (!parse_success)
    DCHECK(!record->IsIntact());
  return record;
}

// static
IntegrityRecordRdata IntegrityRecordRdata::Random() {
  constexpr uint16_t kMinNonceLen = 32;
  constexpr uint16_t kMaxNonceLen = 512;

  // Construct random nonce.
  const uint16_t nonce_len = base::RandInt(kMinNonceLen, kMaxNonceLen);
  Nonce nonce(nonce_len);
  base::RandBytes(nonce.data(), nonce.size());

  return IntegrityRecordRdata(std::move(nonce));
}

absl::optional<std::vector<uint8_t>> IntegrityRecordRdata::Serialize() const {
  if (!is_intact_) {
    return absl::nullopt;
  }

  // Create backing buffer and writer.
  std::vector<uint8_t> serialized(LengthForSerialization(nonce_));
  base::BigEndianWriter writer(reinterpret_cast<char*>(serialized.data()),
                               serialized.size());

  // Writes will only fail if the buffer is too small. We are asserting here
  // that our buffer is exactly the right size, which is expected to always be
  // true if |is_intact_|.
  CHECK(writer.WriteU16(nonce_.size()));
  CHECK(writer.WriteBytes(nonce_.data(), nonce_.size()));
  CHECK(writer.WriteBytes(digest_.data(), digest_.size()));
  CHECK_EQ(writer.remaining(), 0u);

  return serialized;
}

// static
IntegrityRecordRdata::Digest IntegrityRecordRdata::Hash(const Nonce& nonce) {
  Digest digest{};
  SHA256(nonce.data(), nonce.size(), digest.data());
  return digest;
}

// static
size_t IntegrityRecordRdata::LengthForSerialization(const Nonce& nonce) {
  // A serialized INTEGRITY record consists of a U16-prefixed |nonce_|, followed
  // by the bytes of |digest_|.
  return sizeof(uint16_t) + nonce.size() + kDigestLen;
}

}  // namespace net
