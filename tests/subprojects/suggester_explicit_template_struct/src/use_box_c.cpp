#include "compute_box.hpp"

int use_box_c() {
    outer::inner::Packet<int, 22> packet{};
    return packet.score() + 2;
}
