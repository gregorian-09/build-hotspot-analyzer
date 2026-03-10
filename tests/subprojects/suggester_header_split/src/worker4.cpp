#include "monolith.hpp"

int worker4() {
    splitproj::CompileUnit unit("w4");
    splitproj::Record4 rec{};
    return unit.weight() + rec.score();
}
