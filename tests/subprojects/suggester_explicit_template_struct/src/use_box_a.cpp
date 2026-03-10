#include "compute_box.hpp"

int use_box_a() {
    outer::inner::Packet<int, 22> packet{};
    return packet.score();
}
