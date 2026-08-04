#include "render/DeterministicNoise.hpp"

#include <cmath>

namespace ugurugu::DeterministicNoise
{

namespace
{

quint64 mixHash(quint64 value)
{
    value += 0x9e3779b97f4a7c15ULL;
    value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31U);
}

}

qreal signedValue(quint64 seed, int frame, int index, quint64 channel)
{
    quint64 value = seed;
    value ^= mixHash(static_cast<quint64>(frame + 1) * 0x517cc1b727220a95ULL);
    value ^=
        mixHash(static_cast<quint64>(index + 4099) * 0x6eed0e9da4d94a4fULL);
    value ^= channel;
    const quint64 result = mixHash(value);
    const qreal unit =
        static_cast<qreal>(result >> 11U) / static_cast<qreal>(1ULL << 53U);
    return unit * 2.0 - 1.0;
}

qreal smoothValue(quint64 seed, int frame, qreal coordinate, quint64 channel)
{
    const int left = static_cast<int>(std::floor(coordinate));
    const qreal fraction = coordinate - static_cast<qreal>(left);
    const qreal blend = fraction * fraction * (3.0 - 2.0 * fraction);
    const qreal a = signedValue(seed, frame, left, channel);
    const qreal b = signedValue(seed, frame, left + 1, channel);
    return a + (b - a) * blend;
}

qreal unitValue(quint64 seed, int frame, int index, quint64 channel)
{
    return (signedValue(seed, frame, index, channel) + 1.0) * 0.5;
}

}
