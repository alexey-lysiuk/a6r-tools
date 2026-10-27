// tinySA Ultra screenshot text recognizer
//
// Loads a capture (PNG, BMP, etc. supported by stb_image) and recognizes texts
// rendered with the small 5x7 font in the status column (left part of the screen)
// and in the status row (bottom of the screen) by exact glyph bitmap matching.

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#include "stb_image.h"

#include "font5x7.h"
#include "font7x11b.h"

namespace
{

constexpr int STATUS_COLUMN_WIDTH = 30; // OFFSETX in tinySA firmware
constexpr int STATUS_ROW_HEIGHT = 8;    // FREQUENCIES_YPOS = LCD_HEIGHT - 8
constexpr int MARKER_ROWS_HEIGHT = 64;  // 4 marker rows, 16 pixels each
constexpr int MAX_FONT_HEIGHT = MARKER_FONT_HEIGHT;
constexpr int SEGMENT_GAP_SPACES = 3;   // gap of this many spaces or more splits texts
constexpr int INK_THRESHOLD = 100;      // sum of per-channel differences from background
constexpr double MIN_QUALITY = 0.85;    // minimal share of ink explained by recognized glyphs
constexpr uint32_t GRID_COLOR = 0x848284; // chart grid shows through marker texts, not ink
constexpr uint32_t NO_IGNORED_COLOR = 0xFFFFFFFF;

struct Glyph
{
    char ch;
    int width;
    int ink;
    uint8_t rows[MAX_FONT_HEIGHT]; // bit 7 is leftmost column
};

struct Font
{
    int height = 0;
    int spaceWidth = 4;
    std::vector<Glyph> glyphs;
};

struct Rgb
{
    uint8_t r, g, b;
    bool operator==(const Rgb& o) const { return r == o.r && g == o.g && b == o.b; }
};

struct Image
{
    int width = 0;
    int height = 0;
    std::vector<Rgb> pixels;
    const Rgb& at(int x, int y) const { return pixels[size_t(y) * width + x]; }
};

struct Region
{
    const char* name;
    int x0, y0, x1, y1; // [x0, x1) x [y0, y1)
    const Font* font;
    uint32_t ignoredColor; // pixels of this color are never ink
};

struct Token
{
    int x;
    int width;
    char ch; // '?' for unrecognized column
    std::string color;
};

struct Text
{
    int x;
    int y;
    std::string color;
    std::string value;
};

Font g_smallFont;
Font g_markerFont;

void addGlyph(Font& font, int code, int width, const uint8_t* rows)
{
    if (code == ' ')
    {
        font.spaceWidth = width;
    }

    // Skip icons and space, they are not text glyphs
    // Skip 'O' as well, its glyph is identical to '0' and is resolved by resolveZeroO()
    if (code <= ' ' || code > 0x7E || code == 'O')
    {
        return;
    }

    Glyph glyph{};
    glyph.ch = char(code);
    glyph.width = width;

    for (int r = 0; r < font.height; ++r)
    {
        // First row also stores the width in its low bits, drop everything past the glyph width
        const uint8_t row = rows[r] & uint8_t(0xFF << (8 - width));
        glyph.rows[r] = row;

        for (int c = 0; c < width; ++c)
        {
            glyph.ink += (row >> (7 - c)) & 1;
        }
    }

    if (glyph.ink > 0)
    {
        font.glyphs.push_back(glyph);
    }
}

void loadFonts()
{
    g_smallFont.height = FONT_HEIGHT;

    for (int i = 0, count = int(sizeof(x5x7_bits) / FONT_HEIGHT); i < count; ++i)
    {
        addGlyph(g_smallFont, FONT_START_CHAR + i, FONT_GET_WIDTH(x5x7_bits, i), &x5x7_bits[i * FONT_HEIGHT]);
    }

    g_markerFont.height = MARKER_FONT_HEIGHT;

    for (int i = 0, count = int(sizeof(x7x11b_bits) / MARKER_FONT_HEIGHT); i < count; ++i)
    {
        addGlyph(g_markerFont, MARKER_FONT_START_CHAR + i, MARKER_FONT_GET_WIDTH(x7x11b_bits, i),
            &x7x11b_bits[i * MARKER_FONT_HEIGHT]);
    }
}

bool loadImage(const char* path, Image& image)
{
    int channels = 0;
    stbi_uc* data = stbi_load(path, &image.width, &image.height, &channels, 3);

    if (data == nullptr)
    {
        fprintf(stderr, "Failed to load '%s': %s\n", path, stbi_failure_reason());
        return false;
    }

    image.pixels.resize(size_t(image.width) * image.height);
    memcpy(image.pixels.data(), data, image.pixels.size() * sizeof(Rgb));
    stbi_image_free(data);
    return true;
}

std::string colorName(int r, int g, int b)
{
    const int maxc = std::max(r, std::max(g, b));
    const int minc = std::min(r, std::min(g, b));
    const int delta = maxc - minc;

    // Low saturation is white or gray, the tolerance covers pale colors such as pink
    if (delta * 4 < maxc)
    {
        return maxc >= 192 ? "white" : "gray";
    }

    double hue;

    if (maxc == r)
    {
        hue = 60.0 * ((g - b) / double(delta));
    }
    else if (maxc == g)
    {
        hue = 60.0 * ((b - r) / double(delta) + 2.0);
    }
    else
    {
        hue = 60.0 * ((r - g) / double(delta) + 4.0);
    }

    if (hue < 0.0)
    {
        hue += 360.0;
    }

    if (hue < 15.0 || hue >= 330.0) return "red";
    if (hue < 45.0) return "orange";
    if (hue < 75.0) return "yellow";
    if (hue < 165.0) return "green";
    if (hue < 200.0) return "cyan";
    if (hue < 260.0) return "blue";
    return "magenta";
}

class Recognizer
{
public:
    Recognizer(const Image& image, const Region& region)
        : m_image(image)
        , m_region(region)
        , m_font(*region.font)
        , m_background(findBackground())
    {
    }

    std::vector<Text> run() const
    {
        std::vector<Text> result;
        const Region& rg = m_region;

        int y = rg.y0;
        const int fontHeight = m_font.height;

        while (y < rg.y1)
        {
            if (rowInk(y) == 0)
            {
                ++y;
                continue;
            }

            // Top ink row may belong to any glyph row, so try all line positions
            const int lastTop = rg.y1 - fontHeight;
            int bestTop = -1;
            double bestScore = 0.0;
            std::vector<Token> bestTokens;
            double bestQuality = 0.0;

            for (int top = std::max(rg.y0, y - (fontHeight - 1)); top <= std::min(y, lastTop); ++top)
            {
                std::vector<Token> tokens;
                double quality = 0.0;
                const double score = decodeLine(top, tokens, quality);

                if (score > bestScore)
                {
                    bestScore = score;
                    bestTop = top;
                    bestTokens = std::move(tokens);
                    bestQuality = quality;
                }
            }

            if (bestTop < 0 || bestQuality < MIN_QUALITY)
            {
                ++y;
                continue;
            }

            splitTexts(bestTop, bestTokens, result);
            y = bestTop + fontHeight;
        }

        return result;
    }

private:
    const Image& m_image;
    Region m_region;
    const Font& m_font;
    Rgb m_background;

    Rgb findBackground() const
    {
        std::map<uint32_t, int> histogram;

        for (int y = m_region.y0; y < m_region.y1; ++y)
        {
            for (int x = m_region.x0; x < m_region.x1; ++x)
            {
                const Rgb& p = m_image.at(x, y);
                ++histogram[(p.r << 16) | (p.g << 8) | p.b];
            }
        }

        uint32_t best = 0;
        int bestCount = -1;

        for (const auto& entry : histogram)
        {
            if (entry.second > bestCount)
            {
                bestCount = entry.second;
                best = entry.first;
            }
        }

        return Rgb{ uint8_t(best >> 16), uint8_t(best >> 8), uint8_t(best) };
    }

    bool isInk(int x, int y) const
    {
        const Rgb& p = m_image.at(x, y);

        if (uint32_t((p.r << 16) | (p.g << 8) | p.b) == m_region.ignoredColor)
        {
            return false;
        }

        const int diff = abs(p.r - m_background.r) + abs(p.g - m_background.g) + abs(p.b - m_background.b);
        return diff >= INK_THRESHOLD;
    }

    int rowInk(int y) const
    {
        int count = 0;

        for (int x = m_region.x0; x < m_region.x1; ++x)
        {
            count += isInk(x, y);
        }

        return count;
    }

    int columnInk(int x, int top) const
    {
        int count = 0;

        for (int r = 0; r < m_font.height; ++r)
        {
            count += isInk(x, top + r);
        }

        return count;
    }

    bool matches(const Glyph& glyph, int x, int top) const
    {
        if (x + glyph.width > m_region.x1)
        {
            return false;
        }

        for (int r = 0; r < m_font.height; ++r)
        {
            for (int c = 0; c < glyph.width; ++c)
            {
                const bool expected = (glyph.rows[r] >> (7 - c)) & 1;

                if (expected != isInk(x + c, top + r))
                {
                    return false;
                }
            }
        }

        return true;
    }

    std::string cellColor(int x, int width, int top) const
    {
        long r = 0, g = 0, b = 0;
        int count = 0;

        for (int row = 0; row < m_font.height; ++row)
        {
            for (int c = 0; c < width; ++c)
            {
                if (isInk(x + c, top + row))
                {
                    const Rgb& p = m_image.at(x + c, top + row);
                    r += p.r;
                    g += p.g;
                    b += p.b;
                    ++count;
                }
            }
        }

        return count > 0 ? colorName(int(r / count), int(g / count), int(b / count)) : "white";
    }

    static bool anyInk(const std::vector<int>& colInk, int from, int count)
    {
        for (int i = from; i < from + count; ++i)
        {
            if (colInk[i] > 0)
            {
                return true;
            }
        }

        return false;
    }

    // Finds the best sequence of glyphs for a line starting at the given row,
    // returns its score, tokens are appended in left to right order
    double decodeLine(int top, std::vector<Token>& tokens, double& quality) const
    {
        const int x0 = m_region.x0;
        const int n = m_region.x1 - x0;

        struct Node
        {
            double score = -1e30;
            int prev = -1;
            const Glyph* glyph = nullptr; // null for single column step
            bool unknown = false;
        };

        std::vector<Node> nodes(n + 1);
        std::vector<int> colInk(n);
        int totalInk = 0;

        for (int i = 0; i < n; ++i)
        {
            colInk[i] = columnInk(x0 + i, top);
            totalInk += colInk[i];
        }

        if (totalInk == 0)
        {
            return 0.0;
        }

        // Decoding may start at any column
        for (auto& node : nodes)
        {
            node.score = 0.0;
        }

        for (int i = 0; i < n; ++i)
        {
            const double base = nodes[i].score;

            Node& step = nodes[i + 1];
            const double stepScore = base - 2.0 * colInk[i];

            if (stepScore > step.score || (colInk[i] == 0 && stepScore >= step.score))
            {
                step.score = stepScore;
                step.prev = i;
                step.glyph = nullptr;
                step.unknown = colInk[i] > 0;
            }

            // Glyph cell may start with blank columns, so only skip fully blank cells
            for (const Glyph& glyph : m_font.glyphs)
            {
                if (i + glyph.width <= n && matches(glyph, x0 + i, top) && anyInk(colInk, i, glyph.width))
                {
                    Node& next = nodes[i + glyph.width];
                    const double score = base + glyph.ink;

                    if (score > next.score)
                    {
                        next.score = score;
                        next.prev = i;
                        next.glyph = &glyph;
                        next.unknown = false;
                    }
                }
            }
        }

        int end = 0;

        for (int i = 0; i <= n; ++i)
        {
            if (nodes[i].score >= nodes[end].score)
            {
                end = i;
            }
        }

        const double total = nodes[end].score;
        int matchedInk = 0;
        std::vector<Token> reversed;

        for (int i = end; i > 0 && nodes[i].prev >= 0;)
        {
            const Node& node = nodes[i];
            const int from = node.prev;

            if (node.glyph != nullptr)
            {
                matchedInk += node.glyph->ink;
                reversed.push_back({ x0 + from, node.glyph->width, node.glyph->ch,
                    cellColor(x0 + from, node.glyph->width, top) });
            }
            else if (node.unknown)
            {
                reversed.push_back({ x0 + from, 1, '?', cellColor(x0 + from, 1, top) });
            }

            i = from;
        }

        tokens.assign(reversed.rbegin(), reversed.rend());
        quality = double(matchedInk) / totalInk;

        return matchedInk > 0 ? total : 0.0;
    }

    // Glyphs of '0' and 'O' are identical, use uppercase neighbors as a hint
    static void resolveZeroO(std::string& value)
    {
        for (size_t i = 0; i < value.size(); ++i)
        {
            if (value[i] != '0')
            {
                continue;
            }

            const char before = i > 0 ? value[i - 1] : ' ';
            const char after = i + 1 < value.size() ? value[i + 1] : ' ';
            const auto numeric = [](char c) { return isdigit((unsigned char)c) || c == '.'; };
            const auto upper = [](char c) { return isupper((unsigned char)c) != 0; };

            if (!numeric(before) && !numeric(after) && (upper(before) || upper(after)))
            {
                value[i] = 'O';
            }
        }
    }

    static void flush(Text& text, bool& active, std::vector<Text>& out)
    {
        if (active)
        {
            resolveZeroO(text.value);
            out.push_back(text);
            active = false;
        }
    }

    void splitTexts(int top, const std::vector<Token>& tokens, std::vector<Text>& out) const
    {
        Text text{};
        bool active = false;
        int prevEnd = 0;

        for (const Token& token : tokens)
        {
            if (active)
            {
                const int gap = token.x - prevEnd;

                if (gap >= SEGMENT_GAP_SPACES * m_font.spaceWidth || token.color != text.color)
                {
                    flush(text, active, out);
                }
                else
                {
                    text.value.append(size_t((gap + m_font.spaceWidth / 2) / m_font.spaceWidth), ' ');
                }
            }

            if (!active)
            {
                text = Text{ token.x, top, token.color, std::string() };
                active = true;
            }

            // Merge adjacent unrecognized columns
            if (!(token.ch == '?' && !text.value.empty() && text.value.back() == '?' && token.x == prevEnd))
            {
                text.value.push_back(token.ch);
            }

            prevEnd = token.x + token.width;
        }

        flush(text, active, out);
    }
};

void usage(const char* program)
{
    fprintf(stderr,
        "Usage: %s [--plain | --json] <capture.png|capture.bmp>\n"
        "Recognizes texts in the marker rows (marker font), the status column and the status row (small font).\n"
        "Default output is tab-separated: region, x, y, color, text.\n"
        "  --plain  output recognized texts only\n"
        "  --json   output a JSON array of recognized text objects\n",
        program);
}

void printJsonString(const std::string& value)
{
    static const char hex[] = "0123456789abcdef";

    putchar('"');

    for (unsigned char ch : value)
    {
        switch (ch)
        {
        case '"': fputs("\\\"", stdout); break;
        case '\\': fputs("\\\\", stdout); break;
        case '\b': fputs("\\b", stdout); break;
        case '\f': fputs("\\f", stdout); break;
        case '\n': fputs("\\n", stdout); break;
        case '\r': fputs("\\r", stdout); break;
        case '\t': fputs("\\t", stdout); break;
        default:
            if (ch < 0x20)
            {
                fputs("\\u00", stdout);
                putchar(hex[ch >> 4]);
                putchar(hex[ch & 0x0f]);
            }
            else
            {
                putchar(ch);
            }
            break;
        }
    }

    putchar('"');
}

} // namespace

int main(int argc, char** argv)
{
    bool plain = false;
    bool json = false;
    const char* path = nullptr;

    for (int i = 1; i < argc; ++i)
    {
        if (strcmp(argv[i], "--plain") == 0)
        {
            plain = true;
        }
        else if (strcmp(argv[i], "--json") == 0)
        {
            json = true;
        }
        else if (path == nullptr && argv[i][0] != '\0')
        {
            path = argv[i];
        }
        else
        {
            usage(argv[0]);
            return 2;
        }
    }

    if (path == nullptr || (plain && json))
    {
        usage(argv[0]);
        return 2;
    }

    Image image;

    if (!loadImage(path, image))
    {
        return 1;
    }

    if (image.width <= STATUS_COLUMN_WIDTH || image.height <= STATUS_ROW_HEIGHT * 2)
    {
        fprintf(stderr, "Image size %dx%d is too small for a tinySA capture\n", image.width, image.height);
        return 1;
    }

    loadFonts();

    const Region regions[] =
    {
        { "markers", STATUS_COLUMN_WIDTH, 0, image.width, std::min(MARKER_ROWS_HEIGHT, image.height),
            &g_markerFont, GRID_COLOR },
        { "column", 0, 0, STATUS_COLUMN_WIDTH, image.height, &g_smallFont, NO_IGNORED_COLOR },
        { "row", STATUS_COLUMN_WIDTH, image.height - STATUS_ROW_HEIGHT, image.width, image.height,
            &g_smallFont, NO_IGNORED_COLOR },
    };

    bool firstJsonItem = true;

    if (json)
    {
        puts("[");
    }

    for (const Region& region : regions)
    {
        for (const Text& text : Recognizer(image, region).run())
        {
            if (plain)
            {
                printf("%s\n", text.value.c_str());
            }
            else if (json)
            {
                if (!firstJsonItem)
                {
                    printf(",\n");
                }

                printf("  {\"region\":");
                printJsonString(region.name);
                printf(",\"x\":%d,\"y\":%d,\"color\":", text.x, text.y);
                printJsonString(text.color);
                printf(",\"text\":");
                printJsonString(text.value);
                printf("}");
                firstJsonItem = false;
            }
            else
            {
                printf("%s\t%d\t%d\t%s\t%s\n", region.name, text.x, text.y, text.color.c_str(), text.value.c_str());
            }
        }
    }

    if (json)
    {
        puts("\n]");
    }

    return 0;
}
