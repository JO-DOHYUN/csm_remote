#pragma once
#include <cstdint>
namespace rtos { class EventFlags { public: uint32_t set(uint32_t b){return b;}
 uint32_t wait_any(uint32_t,uint32_t,bool){return 0;} }; }
