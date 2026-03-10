#include "compute_box.hpp"

int use_box_b() {
    outer::inner::Packet<int, 22> packet{};
    return packet.score() + 1;
}
