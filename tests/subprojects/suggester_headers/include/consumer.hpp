#pragma once

#include "mega.hpp"
#include <string>

namespace headers {

class Consumer {
public:
    Consumer();
    void attach(MegaType* instance);
    std::string describe(const MegaType& instance) const;

private:
    MegaType* instance_{};
};

}  // namespace headers
