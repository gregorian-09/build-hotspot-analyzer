#include "consumer.hpp"

namespace headers {

Consumer::Consumer() = default;

void Consumer::attach(MegaType* instance) {
    instance_ = instance;
}

std::string Consumer::describe(const MegaType& instance) const {
    return instance.summary();
}

}  // namespace headers
