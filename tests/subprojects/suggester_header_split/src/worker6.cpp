#include "monolith.hpp"

int worker6() {
    splitproj::CompileUnit unit("w6");
    splitproj::Record6 rec{};
    return unit.weight() + rec.score();
}
