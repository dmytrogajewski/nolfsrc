#include "platform/system.h"
#include <ctime>
#include <stdexcept>

namespace nolf::platform {
LocalDate local_date() {
    const auto now=std::time(nullptr);
    std::tm local{};
    if (!localtime_r(&now,&local)) throw std::runtime_error("cannot read local date");
    return {local.tm_year+1900,local.tm_mon+1,local.tm_mday,local.tm_wday};
}
}
