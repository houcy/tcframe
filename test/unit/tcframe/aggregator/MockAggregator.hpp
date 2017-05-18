#pragma once

#include "gmock/gmock.h"

#include "tcframe/aggregator/Aggregator.hpp"

namespace tcframe {

class MockAggregator : public Aggregator {
public:
    MOCK_METHOD2(aggregate, Verdict(const vector<Verdict>&, double));
};

}
