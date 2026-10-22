#include "achievements_menu.hpp"

#include "achievement.hpp"

#include <3ds.h>
#include <CTRPluginFramework.hpp>

#include <cstring>
#include <string>
#include <vector>

using namespace CTRPluginFramework;

namespace AchievementsMenu
{
    namespace
    {
        const Color TITLE_TEXT = Color::DodgerBlue;
        const Color HINT_TEXT = Color::DodgerBlue;
        const Color HEADER_TEXT = Color::White;
        const Color UNLOCKED_TEXT = Color::LimeGreen;
        const Color LOCKED_TEXT = Color::White;
        const Color SCROLLBAR(110, 110, 120);

        const u32   WIDTH = 320;
        const u32   HEIGHT = 240;
        const u32   TITLE_Y = 8;
        const u32   SUBTITLE_Y = 28;
        const u32   LIST_TOP = 56;
        const u32   ROW_HEIGHT = 20;
        const u32   VISIBLE_ROWS = 8;       // leaves room for the hints line
        const u32   HINT_Y = HEIGHT - 20;
        const u32   TEXT_LEFT = 10;
        const u32   TEXT_WIDTH = WIDTH - TEXT_LEFT - 14;
        const u32   LIST_LEFT = TEXT_LEFT + 16;     // achievements are indented under the game title
        const u32   LIST_WIDTH = WIDTH - LIST_LEFT - 14;

        const u32   DIRECTION_KEYS = Key::Up | Key::Down | Key::Left | Key::Right;

        u32 TextWidth(const std::string &text)
        {
            return static_cast<u32>(OSD::GetTextWidth(true, text));
        }

        // Removes the last UTF-8 character.
        void PopChar(std::string &text)
        {
            while (!text.empty() && (text.back() & 0xC0) == 0x80)
                text.pop_back();
            if (!text.empty())
                text.pop_back();
        }

        std::string Ellipsize(const std::string &text, u32 maxWidth)
        {
            if (TextWidth(text) <= maxWidth)
                return text;

            std::string out = text;
            while (!out.empty() && TextWidth(out + "...") > maxWidth)
                PopChar(out);
            return out + "...";
        }

        // The game's own title, from the SMDH ("icon") in its ExeFS, read
        // through the SelfNCCH archive. English if present, else Japanese,
        // else the process name.
        std::string ReadGameTitle(void)
        {
            struct { u32 type; char name[8]; } path = { 2, "icon" };    // 2 = ExeFS file
            FS_Path archivePath = { PATH_EMPTY, 1, "" };
            FS_Path filePath = { PATH_BINARY, sizeof(path), &path };
            Handle  file;
            u64     size = 0;
            u32     read = 0;

            if (R_SUCCEEDED(FSUSER_OpenFileDirectly(&file, ARCHIVE_ROMFS, archivePath, filePath, FS_OPEN_READ, 0)))
            {
                // ExeFS files must be read whole in one call (FS rejects partial reads).
                std::vector<u8> smdh;
                Result res = FSFILE_GetSize(file, &size);
                if (R_SUCCEEDED(res) && size >= 0x408 && size <= 0x10000)
                {
                    smdh.resize(size);
                    res = FSFILE_Read(file, &read, 0, smdh.data(), size);
                }
                FSFILE_Close(file);

                if (R_SUCCEEDED(res) && !smdh.empty() && read == size && std::memcmp(smdh.data(), "SMDH", 4) == 0)
                {
                    for (int language : { 1, 0 })
                    {
                        char16_t shortTitle[0x41] = {};
                        std::memcpy(shortTitle, smdh.data() + 8 + language * 0x200, 0x80);

                        std::string title;
                        Utils::ConvertUTF16ToUTF8(title, shortTitle);
                        if (!title.empty())
                            return title;
                    }
                }
            }

            std::string name;
            Process::GetName(name);
            return name;
        }

        void Draw(const Screen &screen, const std::string &gameTitle,
                  const std::vector<Achievements::Info> &list, u32 scroll)
        {
            screen.DrawRect(0, 0, WIDTH, HEIGHT, Color::Black);

            u32 unlocked = 0;
            for (const Achievements::Info &a : list)
                if (a.unlocked)
                    unlocked++;

            std::string count = " (" + std::to_string(unlocked) + "/" + std::to_string(list.size()) + ")";
            const std::string title = "3DS Achievements";
            screen.DrawSysfont(title, (WIDTH - TextWidth(title)) / 2, TITLE_Y, TITLE_TEXT);
            screen.DrawSysfont(Ellipsize(gameTitle, TEXT_WIDTH - TextWidth(":" + count)) + ":" + count,
                               TEXT_LEFT, SUBTITLE_Y, HEADER_TEXT);

            const std::string scrollHint = "Up/Down: Scroll";
            screen.DrawSysfont("B: Close", TEXT_LEFT, HINT_Y, HINT_TEXT);
            screen.DrawSysfont(scrollHint, WIDTH - TEXT_LEFT - TextWidth(scrollHint), HINT_Y, HINT_TEXT);

            if (list.empty())
            {
                screen.DrawSysfont("No achievements for this game.", LIST_LEFT, LIST_TOP, LOCKED_TEXT);
                return;
            }

            for (u32 row = 0; row < VISIBLE_ROWS && scroll + row < list.size(); row++)
            {
                const Achievements::Info &a = list[scroll + row];
                u32 y = LIST_TOP + row * ROW_HEIGHT;

                screen.DrawSysfont(Ellipsize(a.name, LIST_WIDTH), LIST_LEFT, y, a.unlocked ? UNLOCKED_TEXT : LOCKED_TEXT);
            }

            if (list.size() > VISIBLE_ROWS)
            {
                u32 track = VISIBLE_ROWS * ROW_HEIGHT;
                u32 thumb = track * VISIBLE_ROWS / list.size();
                if (thumb < 12)
                    thumb = 12;
                u32 thumbY = LIST_TOP + (track - thumb) * scroll / (list.size() - VISIBLE_ROWS);

                screen.DrawRect(WIDTH - 6, thumbY, 3, thumb, SCROLLBAR);
            }
        }

        // Up/Down scroll one row, Left/Right one page.
        int Direction(u32 keys)
        {
            if (keys & Key::Up)    return -1;
            if (keys & Key::Down)  return 1;
            if (keys & Key::Left)  return -static_cast<int>(VISIBLE_ROWS);
            if (keys & Key::Right) return VISIBLE_ROWS;
            return 0;
        }

        // Keeps the buttons that closed the menu from reaching the game.
        void WaitForRelease(void)
        {
            do
            {
                Sleep(Milliseconds(16));
                Controller::Update();
            } while (Controller::GetKeysDown() != 0);
        }
    }

    void (*OnXPressed)(void) = nullptr;

    bool Open(void)
    {
        static std::string gameTitle = ReadGameTitle();

        std::vector<Achievements::Info> list = Achievements::List();
        u32 maxScroll = list.size() > VISIBLE_ROWS ? list.size() - VISIBLE_ROWS : 0;

        Process::Pause();

        u32     scroll = 0;
        u32     redraw = 2;     // once per framebuffer
        bool    closeArmed = false;
        Clock   repeatClock;
        Time    repeatDelay;

        while (true)
        {
            Controller::Update();
            u32 pressed = Controller::GetKeysPressed();
            u32 held = Controller::GetKeysDown();

            // The hotkey that opened the menu is still held at first.
            if (!(held & (Key::Select | Key::B)))
                closeArmed = true;
            if (closeArmed && (pressed & (Key::Select | Key::B)))
                break;

            if ((pressed & Key::X) && OnXPressed != nullptr)
            {
                OnXPressed();
                redraw = 2;
            }

            int move = 0;
            if (pressed & DIRECTION_KEYS)
            {
                move = Direction(pressed);
                repeatClock.Restart();
                repeatDelay = Milliseconds(400);
            }
            else if ((held & DIRECTION_KEYS) && repeatClock.HasTimePassed(repeatDelay))
            {
                move = Direction(held);
                repeatClock.Restart();
                repeatDelay = Milliseconds(80);
            }

            if (move != 0)
            {
                int next = static_cast<int>(scroll) + move;
                if (next < 0)
                    next = 0;
                if (next > static_cast<int>(maxScroll))
                    next = maxScroll;

                if (static_cast<u32>(next) != scroll)
                {
                    scroll = next;
                    redraw = 2;
                }
            }

            if (redraw > 0)
            {
                Draw(OSD::GetBottomScreen(), gameTitle, list, scroll);
                OSD::SwapBuffers();
                redraw--;
            }
            Sleep(Milliseconds(16));
        }

        WaitForRelease();
        Process::Play();
        return false;     // keeps CTRPF's menu closed
    }
}
