#pragma once

#include <vector>

#include "util/assert.h"

// Simple moving average
class Rotary {
    using Buffer = std::vector<double>;
    using index_type = Buffer::size_type;

  public:
    /// Creates a moving average filter with the given active length.
    Rotary(qsizetype filterLength)
            : Rotary(filterLength, filterLength) {
    }

    /// Creates a moving average filter with a preallocated maximum length.
    Rotary(qsizetype filterLength, qsizetype maxFilterLength);

    /// Changes the active filter length without reallocating.
    void setFilterLength(qsizetype filterLength);

    /// Low pass filtered rotary event.
    double filter(double value);

  private:
    index_type nextIndex(index_type) const;
    void reset();
    void append(double v);
    double calculate() const;
    Buffer m_filterHistory;
    index_type m_filterLength;
    index_type m_headIndex;
    double m_filterSum;
};
