#pragma once

class Heavy;
#include "heavy.hpp"

class Widget {
public:
    int run(Heavy& h);

private:
    Heavy* ptr_{};
};
