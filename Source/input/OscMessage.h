#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace chordtroller::osc
{

/**
    Just enough OSC 1.0 to read what handtrack sends: messages and bundles whose
    arguments are int32, float32 or string. Anything else in a message makes that
    message unreadable rather than half-read, and a malformed packet is dropped
    without touching memory outside it — this parses bytes from the network on the
    poll thread, so every length is checked before it is used.

    No allocation and no JUCE, so the same header runs in the offline test.
*/
struct Arg
{
    enum class Kind : std::uint8_t { number, text };

    Kind  kind = Kind::number;
    float number = 0.0f;            // ints are converted
    char  text[16] = {};            // truncated, always terminated
};

struct Message
{
    static constexpr int kMaxArgs = 16;     // /landmarks carries 63; the rest are ignored

    char address[64] = {};
    int  argc = 0;                  // how many are stored in args
    Arg  args[kMaxArgs];

    bool is (const char* a) const noexcept { return std::strcmp (address, a) == 0; }
};

namespace detail
{
    inline std::size_t pad4 (std::size_t n) noexcept { return (n + 4) & ~std::size_t (3); }

    /** Length of the NUL-terminated string at p including its padding, or 0 if it
        does not terminate inside the buffer. */
    inline std::size_t paddedStringLength (const std::uint8_t* p, std::size_t avail) noexcept
    {
        std::size_t n = 0;
        while (n < avail && p[n] != 0)
            ++n;

        if (n == avail)
            return 0;

        const auto total = pad4 (n);
        return total <= avail ? total : 0;
    }

    /** Copies at most cap-1 characters and always terminates. */
    inline void copyText (char* dst, std::size_t cap, const char* src) noexcept
    {
        std::size_t i = 0;
        for (; i + 1 < cap && src[i] != '\0'; ++i)
            dst[i] = src[i];
        dst[i] = '\0';
    }

    inline std::uint32_t be32 (const std::uint8_t* p) noexcept
    {
        return (std::uint32_t (p[0]) << 24) | (std::uint32_t (p[1]) << 16)
             | (std::uint32_t (p[2]) << 8)  |  std::uint32_t (p[3]);
    }

    inline bool parseMessage (const std::uint8_t* p, std::size_t size, Message& out) noexcept
    {
        out = Message{};

        const auto addrLen = paddedStringLength (p, size);
        if (addrLen == 0 || p[0] != '/')
            return false;

        copyText (out.address, sizeof (out.address), reinterpret_cast<const char*> (p));
        p += addrLen;
        size -= addrLen;

        if (size == 0)
            return true;                    // no type tag string: no arguments

        if (p[0] != ',')
            return false;

        const auto tagLen = paddedStringLength (p, size);
        if (tagLen == 0)
            return false;

        const char* tags = reinterpret_cast<const char*> (p) + 1;
        const auto  tagCount = std::strlen (tags);
        p += tagLen;
        size -= tagLen;

        for (std::size_t i = 0; i < tagCount; ++i)
        {
            Arg a;

            switch (tags[i])
            {
                case 'i':
                {
                    if (size < 4)
                        return false;
                    a.number = (float) (std::int32_t) be32 (p);
                    p += 4;
                    size -= 4;
                    break;
                }
                case 'f':
                {
                    if (size < 4)
                        return false;
                    const auto bits = be32 (p);
                    std::memcpy (&a.number, &bits, 4);
                    p += 4;
                    size -= 4;
                    break;
                }
                case 's':
                {
                    const auto len = paddedStringLength (p, size);
                    if (len == 0)
                        return false;
                    a.kind = Arg::Kind::text;
                    copyText (a.text, sizeof (a.text), reinterpret_cast<const char*> (p));
                    p += len;
                    size -= len;
                    break;
                }
                default:
                    return false;
            }

            if (out.argc < Message::kMaxArgs)
                out.args[out.argc++] = a;
        }

        return true;
    }
}

/** Calls onMessage (const Message&) for every readable message in the packet,
    descending into bundles. Returns the number delivered. */
template <typename Fn>
int parsePacket (const std::uint8_t* data, std::size_t size, Fn&& onMessage, int depth = 0) noexcept
{
    if (size < 4)
        return 0;

    if (size >= 8 && std::memcmp (data, "#bundle", 8) == 0)
    {
        if (depth > 4 || size < 16)             // 8 tag + 8 timetag
            return 0;

        int delivered = 0;
        std::size_t at = 16;

        while (at + 4 <= size)
        {
            const std::size_t len = detail::be32 (data + at);
            at += 4;

            if (len > size - at)
                break;

            delivered += parsePacket (data + at, len, onMessage, depth + 1);
            at += len;
        }

        return delivered;
    }

    Message m;
    if (! detail::parseMessage (data, size, m))
        return 0;

    onMessage (m);
    return 1;
}

} // namespace chordtroller::osc
