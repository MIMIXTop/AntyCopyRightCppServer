#include <gtest/gtest.h>
#include "Util/TimeFunc.hpp"

TEST(TimeTest, CurrentTime) {

    auto res = util::time::getCurrentTimestamp();

    ASSERT_FALSE(res.empty());

}