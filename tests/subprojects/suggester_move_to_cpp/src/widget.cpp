#include "widget.hpp"

int Widget::run(Heavy& h) {
    ptr_ = &h;
    return ptr_->value();
}
