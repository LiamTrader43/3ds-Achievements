#include "devtools.hpp"

#include <3ds.h>

#include <string>

using namespace CTRPluginFramework;

namespace DevTools
{
    namespace
    {
        const char  DUMP_DIR[] = "/achievements/dumps";

        std::string Hex8(u32 value)
        {
            static const char hex[] = "0123456789ABCDEF";
            std::string out(8, '0');

            for (int i = 7; i >= 0; i--, value >>= 4)
                out[i] = hex[value & 0xF];
            return out;
        }

        bool Overlaps(u32 start, u32 end, u32 otherStart, u32 otherSize)
        {
            return start < otherStart + otherSize && otherStart < end;
        }

        // Writable game memory only: skips the plugin itself and hardware areas
        // (VRAM, DSP RAM, kernel shared pages) that change every frame.
        bool IsGameMemory(const MemInfo &info)
        {
            u32 start = info.base_addr;
            u32 end = info.base_addr + info.size;

            if (info.state == MEMSTATE_FREE || (info.perm & MEMPERM_READWRITE) != MEMPERM_READWRITE)
                return false;

            PluginHeader *header = FwkSettings::Header;
            if (header != nullptr && (Overlaps(start, end, 0x07000000, header->exeSize)
                                      || Overlaps(start, end, header->heapVA, header->heapSize)))
                return false;

            if (start >= 0x1E800000 && start < 0x20000000)
                return false;
            return true;
        }

        bool WriteRegion(const std::string &path, u32 addr, u32 size)
        {
            const u32   CHUNK = 0x40000;
            File        file;

            if (File::Open(file, path, File::WRITE | File::CREATE | File::TRUNCATE) != File::SUCCESS)
                return false;

            bool ok = true;
            for (u32 off = 0; ok && off < size; off += CHUNK)
            {
                u32 len = size - off < CHUNK ? size - off : CHUNK;
                ok = file.Write(reinterpret_cast<const void *>(addr + off), len) == File::SUCCESS;
            }
            file.Close();
            return ok;
        }

        // Runs from the menu, so the game is paused and the snapshot is consistent.
        void DumpSnapshot(MenuEntry *)
        {
            Directory::Create("/achievements");
            Directory::Create(DUMP_DIR);

            u32 n = 0;
            while (File::Exists(std::string(DUMP_DIR) + "/snap" + std::to_string(n) + ".txt") == 1)
                n++;

            std::string prefix = std::string(DUMP_DIR) + "/snap" + std::to_string(n);
            std::string index;
            u32         total = 0, failed = 0;

            for (u32 addr = 0x00100000; addr < 0x40000000; )
            {
                MemInfo  info;
                PageInfo page;

                if (R_FAILED(svcQueryMemory(&info, &page, addr)) || info.size == 0)
                    break;

                if (IsGameMemory(info))
                {
                    std::string name = "snap" + std::to_string(n) + "_" + Hex8(info.base_addr) + ".bin";

                    if (WriteRegion(std::string(DUMP_DIR) + "/" + name, info.base_addr, info.size))
                    {
                        index += Hex8(info.base_addr) + " " + Hex8(info.size) + " " + name + "\n";
                        total += info.size;
                    }
                    else
                        failed++;
                }
                u32 next = info.base_addr + info.size;
                if (next <= addr)
                    break;
                addr = next;
            }

            // Written last, so an index file means the snapshot is complete.
            File file;
            if (File::Open(file, prefix + ".txt", File::WRITE | File::CREATE | File::TRUNCATE) == File::SUCCESS)
            {
                file.Write(index.c_str(), index.size());
                file.Close();
            }

            std::string text = std::to_string(total / 1024) + " KB saved to\n" + prefix + "_*.bin";
            if (failed)
                text += "\n" + std::to_string(failed) + " region(s) failed to write";
            MessageBox("Snapshot " + std::to_string(n), text)();
        }
    }

    void AddMenuEntries(PluginMenu &menu)
    {
        menu += new MenuEntry("Dump memory snapshot", nullptr, DumpSnapshot,
                              "Writes all game memory to /achievements/dumps/ for finding achievement addresses.");
    }
}
