#include "util/rotary.h"

#include <algorithm>

Rotary::Rotary(qsizetype filterLength, qsizetype maxFilterLength)
        : m_filterHistory(maxFilterLength, 0.0),
          m_filterLength{static_cast<index_type>(filterLength)},
          m_headIndex{0},
          m_filterSum{0.0} {
    DEBUG_ASSERT(filterLength > 0);
    DEBUG_ASSERT(maxFilterLength >= filterLength);
}

Rotary::index_type Rotary::nextIndex(Rotary::index_type i) const {
    if (++i >= m_filterLength) {
        return 0;
    }
    return i;
}

void Rotary::setFilterLength(qsizetype filterLength) {
    DEBUG_ASSERT(filterLength > 0);
    DEBUG_ASSERT(static_cast<index_type>(filterLength) <= m_filterHistory.size());
    const index_type boundedFilterLength = std::clamp<index_type>(
            static_cast<index_type>(filterLength),
            1,
            m_filterHistory.size());
    if (boundedFilterLength == m_filterLength) {
        return;
    }
    m_filterLength = boundedFilterLength;
    reset();
}

void Rotary::reset() {
    std::fill(std::begin(m_filterHistory), std::end(m_filterHistory), 0.0);
    m_headIndex = 0;
    m_filterSum = 0.0;
}

void Rotary::append(double v) {
    m_headIndex = nextIndex(m_headIndex);
    m_filterSum -= m_filterHistory[m_headIndex];
    m_filterHistory[m_headIndex] = v;
    m_filterSum += v;
}

double Rotary::calculate() const {
    return m_filterSum / static_cast<double>(m_filterLength);
}

/* Note: There's probably a bug in this function (or this class) somewhere.
    The filter function seems to be the cause of the "drifting" bug in the Hercules stuff.
    What happens is that filter() gets called to do some magic to a value that's returned
    from the Hercules device, and that magic adds "momentum" to it's motion (ie. it doesn't
    stop dead when you stop spinning the jog wheels.) The problem with this "magic" is that
    when herculeslinux.cpp passes the filtered value off to the wheel ControlObject (or what
    have you), the ControlObject's internal value never goes back to zero properly.
    - Albert (March 13, 2007)
*/
double Rotary::filter(double v) {
    append(v);
    return calculate();
}
