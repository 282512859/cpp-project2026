#pragma once

#include <string_view>

namespace cloud::common {

enum class ErrorCode {
    Ok,
    BadRequest,
    UnsupportedVersion,
    Unauthorized,
    Forbidden,
    NotFound,
    NameConflict,
    HashMismatch,
    IoError,
    DbError,
    TransferExpired,
    InternalError,
};

inline constexpr std::string_view toString(ErrorCode code) {
    switch (code) {
    case ErrorCode::Ok: return "OK";
    case ErrorCode::BadRequest: return "BAD_REQUEST";
    case ErrorCode::UnsupportedVersion: return "UNSUPPORTED_VERSION";
    case ErrorCode::Unauthorized: return "UNAUTHORIZED";
    case ErrorCode::Forbidden: return "FORBIDDEN";
    case ErrorCode::NotFound: return "NOT_FOUND";
    case ErrorCode::NameConflict: return "NAME_CONFLICT";
    case ErrorCode::HashMismatch: return "HASH_MISMATCH";
    case ErrorCode::IoError: return "IO_ERROR";
    case ErrorCode::DbError: return "DB_ERROR";
    case ErrorCode::TransferExpired: return "TRANSFER_EXPIRED";
    case ErrorCode::InternalError: return "INTERNAL_ERROR";
    }
    return "INTERNAL_ERROR";
}

} // namespace cloud::common
