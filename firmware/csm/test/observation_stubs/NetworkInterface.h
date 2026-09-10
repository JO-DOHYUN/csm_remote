#pragma once
#include <cstdint>
using nsapi_error_t=int;
using nsapi_size_or_error_t=int;
constexpr int NSAPI_ERROR_OK=0, NSAPI_ERROR_WOULD_BLOCK=-3001;
class NetworkInterface {};
namespace mbed { template<class T, class M> int callback(T*,M) { return 0; } }
