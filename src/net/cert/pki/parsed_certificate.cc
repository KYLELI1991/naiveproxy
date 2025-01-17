// Copyright 2016 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/cert/pki/parsed_certificate.h"

#include "net/cert/pki/cert_errors.h"
#include "net/cert/pki/certificate_policies.h"
#include "net/cert/pki/extended_key_usage.h"
#include "net/cert/pki/name_constraints.h"
#include "net/cert/pki/signature_algorithm.h"
#include "net/cert/pki/verify_name_match.h"
#include "net/der/parser.h"
#include "third_party/boringssl/src/include/openssl/pool.h"

namespace net {

namespace {

DEFINE_CERT_ERROR_ID(kFailedParsingCertificate, "Failed parsing Certificate");
DEFINE_CERT_ERROR_ID(kFailedParsingTbsCertificate,
                     "Failed parsing TBSCertificate");
DEFINE_CERT_ERROR_ID(kFailedParsingSignatureAlgorithm,
                     "Failed parsing SignatureAlgorithm");
DEFINE_CERT_ERROR_ID(kFailedReadingIssuerOrSubject,
                     "Failed reading issuer or subject");
DEFINE_CERT_ERROR_ID(kFailedNormalizingSubject, "Failed normalizing subject");
DEFINE_CERT_ERROR_ID(kFailedNormalizingIssuer, "Failed normalizing issuer");
DEFINE_CERT_ERROR_ID(kFailedParsingExtensions, "Failed parsing extensions");
DEFINE_CERT_ERROR_ID(kFailedParsingBasicConstraints,
                     "Failed parsing basic constraints");
DEFINE_CERT_ERROR_ID(kFailedParsingKeyUsage, "Failed parsing key usage");
DEFINE_CERT_ERROR_ID(kFailedParsingEku, "Failed parsing extended key usage");
DEFINE_CERT_ERROR_ID(kFailedParsingSubjectAltName,
                     "Failed parsing subjectAltName");
DEFINE_CERT_ERROR_ID(kSubjectAltNameNotCritical,
                     "Empty subject and subjectAltName is not critical");
DEFINE_CERT_ERROR_ID(kFailedParsingNameConstraints,
                     "Failed parsing name constraints");
DEFINE_CERT_ERROR_ID(kFailedParsingAia, "Failed parsing authority info access");
DEFINE_CERT_ERROR_ID(kFailedParsingPolicies,
                     "Failed parsing certificate policies");
DEFINE_CERT_ERROR_ID(kFailedParsingPolicyConstraints,
                     "Failed parsing policy constraints");
DEFINE_CERT_ERROR_ID(kFailedParsingPolicyMappings,
                     "Failed parsing policy mappings");
DEFINE_CERT_ERROR_ID(kFailedParsingInhibitAnyPolicy,
                     "Failed parsing inhibit any policy");
DEFINE_CERT_ERROR_ID(kFailedParsingAuthorityKeyIdentifier,
                     "Failed parsing authority key identifier");
DEFINE_CERT_ERROR_ID(kFailedParsingSubjectKeyIdentifier,
                     "Failed parsing subject key identifier");

[[nodiscard]] bool GetSequenceValue(const der::Input& tlv, der::Input* value) {
  der::Parser parser(tlv);
  return parser.ReadTag(der::kSequence, value) && !parser.HasMore();
}

}  // namespace

bool ParsedCertificate::GetExtension(const der::Input& extension_oid,
                                     ParsedExtension* parsed_extension) const {
  if (!tbs_.extensions_tlv)
    return false;

  auto it = extensions_.find(extension_oid);
  if (it == extensions_.end()) {
    *parsed_extension = ParsedExtension();
    return false;
  }

  *parsed_extension = it->second;
  return true;
}

ParsedCertificate::ParsedCertificate() = default;
ParsedCertificate::~ParsedCertificate() = default;

// static
scoped_refptr<ParsedCertificate> ParsedCertificate::Create(
    bssl::UniquePtr<CRYPTO_BUFFER> backing_data,
    const ParseCertificateOptions& options,
    CertErrors* errors) {
  // |errors| is an optional parameter, but to keep the code simpler, use a
  // dummy object when one wasn't provided.
  CertErrors unused_errors;
  if (!errors)
    errors = &unused_errors;

  auto result = base::WrapRefCounted(new ParsedCertificate);
  result->cert_data_ = std::move(backing_data);
  result->cert_ = der::Input(CRYPTO_BUFFER_data(result->cert_data_.get()),
                             CRYPTO_BUFFER_len(result->cert_data_.get()));

  if (!ParseCertificate(result->cert_, &result->tbs_certificate_tlv_,
                        &result->signature_algorithm_tlv_,
                        &result->signature_value_, errors)) {
    errors->AddError(kFailedParsingCertificate);
    return nullptr;
  }

  if (!ParseTbsCertificate(result->tbs_certificate_tlv_, options, &result->tbs_,
                           errors)) {
    errors->AddError(kFailedParsingTbsCertificate);
    return nullptr;
  }

  // Attempt to parse the signature algorithm contained in the Certificate.
  absl::optional<SignatureAlgorithm> sigalg =
      ParseSignatureAlgorithm(result->signature_algorithm_tlv_, errors);
  if (!sigalg) {
    errors->AddError(kFailedParsingSignatureAlgorithm);
    return nullptr;
  }
  result->signature_algorithm_ = *sigalg;

  der::Input subject_value;
  if (!GetSequenceValue(result->tbs_.subject_tlv, &subject_value)) {
    errors->AddError(kFailedReadingIssuerOrSubject);
    return nullptr;
  }
  if (!NormalizeName(subject_value, &result->normalized_subject_, errors)) {
    errors->AddError(kFailedNormalizingSubject);
    return nullptr;
  }
  der::Input issuer_value;
  if (!GetSequenceValue(result->tbs_.issuer_tlv, &issuer_value)) {
    errors->AddError(kFailedReadingIssuerOrSubject);
    return nullptr;
  }
  if (!NormalizeName(issuer_value, &result->normalized_issuer_, errors)) {
    errors->AddError(kFailedNormalizingIssuer);
    return nullptr;
  }

  // Parse the standard X.509 extensions.
  if (result->tbs_.extensions_tlv) {
    // ParseExtensions() ensures there are no duplicates, and maps the (unique)
    // OID to the extension value.
    if (!ParseExtensions(result->tbs_.extensions_tlv.value(),
                         &result->extensions_)) {
      errors->AddError(kFailedParsingExtensions);
      return nullptr;
    }

    ParsedExtension extension;

    // Basic constraints.
    if (result->GetExtension(der::Input(kBasicConstraintsOid), &extension)) {
      result->has_basic_constraints_ = true;
      if (!ParseBasicConstraints(extension.value,
                                 &result->basic_constraints_)) {
        errors->AddError(kFailedParsingBasicConstraints);
        return nullptr;
      }
    }

    // Key Usage.
    if (result->GetExtension(der::Input(kKeyUsageOid), &extension)) {
      result->has_key_usage_ = true;
      if (!ParseKeyUsage(extension.value, &result->key_usage_)) {
        errors->AddError(kFailedParsingKeyUsage);
        return nullptr;
      }
    }

    // Extended Key Usage.
    if (result->GetExtension(der::Input(kExtKeyUsageOid), &extension)) {
      result->has_extended_key_usage_ = true;
      if (!ParseEKUExtension(extension.value, &result->extended_key_usage_)) {
        errors->AddError(kFailedParsingEku);
        return nullptr;
      }
    }

    // Subject alternative name.
    if (result->GetExtension(der::Input(kSubjectAltNameOid),
                             &result->subject_alt_names_extension_)) {
      // RFC 5280 section 4.2.1.6:
      // SubjectAltName ::= GeneralNames
      result->subject_alt_names_ = GeneralNames::Create(
          result->subject_alt_names_extension_.value, errors);
      if (!result->subject_alt_names_) {
        errors->AddError(kFailedParsingSubjectAltName);
        return nullptr;
      }
      // RFC 5280 section 4.1.2.6:
      // If subject naming information is present only in the subjectAltName
      // extension (e.g., a key bound only to an email address or URI), then the
      // subject name MUST be an empty sequence and the subjectAltName extension
      // MUST be critical.
      if (subject_value.Length() == 0 &&
          !result->subject_alt_names_extension_.critical) {
        errors->AddError(kSubjectAltNameNotCritical);
        return nullptr;
      }
    }

    // Name constraints.
    if (result->GetExtension(der::Input(kNameConstraintsOid), &extension)) {
      result->name_constraints_ =
          NameConstraints::Create(extension.value, extension.critical, errors);
      if (!result->name_constraints_) {
        errors->AddError(kFailedParsingNameConstraints);
        return nullptr;
      }
    }

    // Authority information access.
    if (result->GetExtension(der::Input(kAuthorityInfoAccessOid),
                             &result->authority_info_access_extension_)) {
      result->has_authority_info_access_ = true;
      if (!ParseAuthorityInfoAccessURIs(
              result->authority_info_access_extension_.value,
              &result->ca_issuers_uris_, &result->ocsp_uris_)) {
        errors->AddError(kFailedParsingAia);
        return nullptr;
      }
    }

    // Policies.
    if (result->GetExtension(der::Input(kCertificatePoliciesOid), &extension)) {
      result->has_policy_oids_ = true;
      if (!ParseCertificatePoliciesExtensionOids(
              extension.value, false /*fail_parsing_unknown_qualifier_oids*/,
              &result->policy_oids_, errors)) {
        errors->AddError(kFailedParsingPolicies);
        return nullptr;
      }
    }

    // Policy constraints.
    if (result->GetExtension(der::Input(kPolicyConstraintsOid), &extension)) {
      result->has_policy_constraints_ = true;
      if (!ParsePolicyConstraints(extension.value,
                                  &result->policy_constraints_)) {
        errors->AddError(kFailedParsingPolicyConstraints);
        return nullptr;
      }
    }

    // Policy mappings.
    if (result->GetExtension(der::Input(kPolicyMappingsOid), &extension)) {
      result->has_policy_mappings_ = true;
      if (!ParsePolicyMappings(extension.value, &result->policy_mappings_)) {
        errors->AddError(kFailedParsingPolicyMappings);
        return nullptr;
      }
    }

    // Inhibit Any Policy.
    if (result->GetExtension(der::Input(kInhibitAnyPolicyOid), &extension)) {
      result->has_inhibit_any_policy_ = true;
      if (!ParseInhibitAnyPolicy(extension.value,
                                 &result->inhibit_any_policy_)) {
        errors->AddError(kFailedParsingInhibitAnyPolicy);
        return nullptr;
      }
    }

    // Subject Key Identifier.
    if (result->GetExtension(der::Input(kSubjectKeyIdentifierOid),
                             &extension)) {
      result->subject_key_identifier_ = absl::make_optional<der::Input>();
      if (!ParseSubjectKeyIdentifier(
              extension.value, &result->subject_key_identifier_.value())) {
        errors->AddError(kFailedParsingSubjectKeyIdentifier);
        return nullptr;
      }
    }

    // Authority Key Identifier.
    if (result->GetExtension(der::Input(kAuthorityKeyIdentifierOid),
                             &extension)) {
      result->authority_key_identifier_ =
          absl::make_optional<ParsedAuthorityKeyIdentifier>();
      if (!ParseAuthorityKeyIdentifier(
              extension.value, &result->authority_key_identifier_.value())) {
        errors->AddError(kFailedParsingAuthorityKeyIdentifier);
        return nullptr;
      }
    }
  }

  return result;
}

// static
bool ParsedCertificate::CreateAndAddToVector(
    bssl::UniquePtr<CRYPTO_BUFFER> cert_data,
    const ParseCertificateOptions& options,
    std::vector<scoped_refptr<net::ParsedCertificate>>* chain,
    CertErrors* errors) {
  scoped_refptr<ParsedCertificate> cert(
      Create(std::move(cert_data), options, errors));
  if (!cert)
    return false;
  chain->push_back(std::move(cert));
  return true;
}

}  // namespace net
