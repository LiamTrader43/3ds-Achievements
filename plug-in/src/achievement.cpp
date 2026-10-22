#include "achievement.hpp"

#include <CTRPluginFramework.hpp>

#include <cstdlib>
#include <string>
#include <vector>

using namespace CTRPluginFramework;

namespace Achievements
{
    namespace
    {
        enum class Compare { Equal, AtLeast };

        struct Achievement
        {
            std::string id;
            u32         addr;
            u8          size;
            Compare     compare;
            u32         expected;
            std::string name;
            std::string description;
        };

        // A value must hold for this many consecutive Update() calls before it
        // unlocks, so garbage memory during loading screens can't trigger it.
        const u32   FRAME_THRESHOLD = 10;
        const char  ROOT_DIR[] = "/achievements/";

        std::vector<Achievement>    g_ach;
        std::vector<bool>           g_unlocked;
        std::vector<u32>            g_matchFrames;
        std::string                 g_savePath;

        //----------------------------------------------------------------------
        // Platform layer: the only code that calls CTRPluginFramework. Port
        // these functions to move the system to another framework.
        //----------------------------------------------------------------------

        std::string GetTitleIdString(void)
        {
            static const char hex[] = "0123456789ABCDEF";

            u64 tid = Process::GetTitleID();
            std::string out(16, '0');

            for (int i = 15; i >= 0; i--, tid >>= 4)
                out[i] = hex[tid & 0xF];
            return out;
        }

        // Returns false instead of crashing if the address isn't mapped.
        bool ReadMemory(u32 addr, u8 size, u32 &out)
        {
            u8  v8;
            u16 v16;

            switch (size)
            {
                case 1:
                    if (!Process::Read8(addr, v8)) return false;
                    out = v8;
                    return true;
                case 2:
                    if (!Process::Read16(addr, v16)) return false;
                    out = v16;
                    return true;
                default:
                    return Process::Read32(addr, out);
            }
        }

        bool ReadWholeFile(const std::string &path, std::string &out)
        {
            File file;

            if (File::Open(file, path, File::READ) != File::SUCCESS)
                return false;

            out.resize(file.GetSize());
            bool ok = out.empty() || file.Read(&out[0], out.size()) == File::SUCCESS;
            file.Close();
            return ok;
        }

        bool AppendLine(const std::string &path, const std::string &line)
        {
            File file;

            if (File::Open(file, path, File::WRITE | File::CREATE | File::APPEND | File::SYNC) != File::SUCCESS)
                return false;

            std::string data = line + "\n";
            bool ok = file.Write(data.c_str(), data.size()) == File::SUCCESS;
            file.Close();
            return ok;
        }

        void Notify(const std::string &text)
        {
            OSD::Notify(text);
        }

        //----------------------------------------------------------------------
        // Achievement logic: plain C++, no framework calls.
        //----------------------------------------------------------------------

        bool IsSpace(char c)
        {
            return c == ' ' || c == '\t' || c == '\r' || c == '\n';
        }

        std::string Trim(const std::string &s)
        {
            size_t start = 0, end = s.size();

            while (start < end && IsSpace(s[start])) start++;
            while (end > start && IsSpace(s[end - 1])) end--;
            return s.substr(start, end - start);
        }

        // Splits on '\n' and trims each line, so CRLF files from Windows work.
        std::vector<std::string> SplitLines(const std::string &text)
        {
            std::vector<std::string> lines;
            size_t start = 0;

            while (start <= text.size())
            {
                size_t end = text.find('\n', start);
                if (end == std::string::npos)
                    end = text.size();
                lines.push_back(Trim(text.substr(start, end - start)));
                start = end + 1;
            }
            return lines;
        }

        // Reads the next whitespace-separated token, starting at pos.
        bool NextToken(const std::string &line, size_t &pos, std::string &token)
        {
            while (pos < line.size() && IsSpace(line[pos])) pos++;

            size_t start = pos;
            while (pos < line.size() && !IsSpace(line[pos])) pos++;

            token = line.substr(start, pos - start);
            return !token.empty();
        }

        // Accepts hex with a 0x prefix, otherwise decimal (never octal, so
        // "010" means ten).
        bool ParseU32(const std::string &token, u32 &out)
        {
            const char *str = token.c_str();
            int         base = 10;
            char        *end;

            if (token.size() > 2 && token[0] == '0' && (token[1] == 'x' || token[1] == 'X'))
            {
                str += 2;
                base = 16;
            }

            out = std::strtoul(str, &end, base);
            return end != str && *end == '\0';
        }

        // Parses "<id> <address> <size> <expected> <name> | <description>",
        // where expected may start with ">=" to match that value or higher and
        // the "| <description>" part is optional.
        bool ParseDefinition(const std::string &line, Achievement &a)
        {
            std::string addrTok, sizeTok, expectedTok;
            size_t      pos = 0;
            u32         size;

            if (!NextToken(line, pos, a.id) || !NextToken(line, pos, addrTok)
                || !NextToken(line, pos, sizeTok) || !NextToken(line, pos, expectedTok))
                return false;

            a.compare = Compare::Equal;
            if (expectedTok.compare(0, 2, ">=") == 0)
            {
                a.compare = Compare::AtLeast;
                expectedTok.erase(0, 2);
            }

            if (!ParseU32(addrTok, a.addr) || !ParseU32(sizeTok, size) || !ParseU32(expectedTok, a.expected))
                return false;

            if (size != 1 && size != 2 && size != 4)
                return false;

            // An expected value too big for the size could never match.
            if (size < 4 && (a.expected >> (size * 8)) != 0)
                return false;

            a.size = size;

            // The first '|' separates the name from the description.
            std::string text = line.substr(pos);
            size_t      bar = text.find('|');
            a.name = Trim(text.substr(0, bar));
            a.description = bar == std::string::npos ? "" : Trim(text.substr(bar + 1));
            if (a.name.empty())
                a.name = a.id;
            return true;
        }

        bool Matches(const Achievement &a, u32 value)
        {
            return a.compare == Compare::AtLeast ? value >= a.expected : value == a.expected;
        }

        // True for the first line of each achievement ID.
        bool IsFirstWithId(u32 i)
        {
            for (u32 j = 0; j < i; j++)
                if (g_ach[j].id == g_ach[i].id)
                    return false;
            return true;
        }

        void Unlock(u32 i)
        {
            // Lines sharing an ID are alternative conditions for one achievement.
            for (u32 j = 0; j < g_ach.size(); j++)
                if (g_ach[j].id == g_ach[i].id)
                    g_unlocked[j] = true;

            // Save before notifying, so a crash right after still keeps it.
            bool saved = AppendLine(g_savePath, g_ach[i].id);

            Notify("Achievement unlocked: " + g_ach[i].name + (saved ? "" : " (not saved)"));
        }
    }

    bool Init(void)
    {
        g_ach.clear();
        g_unlocked.clear();
        g_matchFrames.clear();

        std::string base = ROOT_DIR + GetTitleIdString();
        std::string text;

        if (!ReadWholeFile(base + ".txt", text))
            return false;

        u32 badLines = 0;

        for (const std::string &line : SplitLines(text))
        {
            if (line.empty() || line[0] == '#')
                continue;

            Achievement a;
            if (ParseDefinition(line, a))
                g_ach.push_back(a);
            else
                badLines++;
        }

        std::string badNote = badLines ? ", " + std::to_string(badLines) + " bad line(s) skipped" : "";

        if (g_ach.empty())
        {
            if (badLines)
                Notify("Achievements: no valid definitions" + badNote);
            return false;
        }

        g_unlocked.assign(g_ach.size(), false);
        g_matchFrames.assign(g_ach.size(), 0);
        g_savePath = base + "_unlocked.txt";

        // Unknown IDs (e.g. removed achievements) simply match nothing.
        if (ReadWholeFile(g_savePath, text))
        {
            for (const std::string &id : SplitLines(text))
                for (u32 i = 0; i < g_ach.size(); i++)
                    if (g_ach[i].id == id)
                        g_unlocked[i] = true;
        }

        u32 total = 0, unlockedCount = 0;
        for (u32 i = 0; i < g_ach.size(); i++)
        {
            if (!IsFirstWithId(i))
                continue;
            total++;
            if (g_unlocked[i])
                unlockedCount++;
        }

        Notify("Achievements: " + std::to_string(unlockedCount) + "/" + std::to_string(total)
               + " unlocked" + badNote);
        return true;
    }

    std::vector<Info> List(void)
    {
        std::vector<Info> list;

        for (u32 i = 0; i < g_ach.size(); i++)
            if (IsFirstWithId(i))
                list.push_back({ g_ach[i].id, g_ach[i].name, g_ach[i].description, g_unlocked[i] });
        return list;
    }

    void Update(void)
    {
        for (u32 i = 0; i < g_ach.size(); i++)
        {
            if (g_unlocked[i])
                continue;

            u32 value;
            if (!ReadMemory(g_ach[i].addr, g_ach[i].size, value) || !Matches(g_ach[i], value))
            {
                g_matchFrames[i] = 0;
                continue;
            }

            if (++g_matchFrames[i] < FRAME_THRESHOLD)
                continue;

            Unlock(i);
        }
    }
}
