#pragma once
#include <cstddef>
constexpr int osPriorityAboveNormal=1, osOK=0;
using osStatus=int;
namespace rtos { class Thread { public: Thread(int,size_t,unsigned char*,const char*){}
 template<class F> int start(F){return osOK;} }; }
